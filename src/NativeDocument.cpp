#include "NativeDocument.h"

#include <BinXCAFDrivers.hxx>
#include <XCAFApp_Application.hxx>
#include <XCAFDoc_ColorTool.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <TDataStd_AsciiString.hxx>
#include <TDataStd_Integer.hxx>
#include <TDataStd_Name.hxx>
#include <TDataStd_RealArray.hxx>
#include <TDF_ChildIterator.hxx>
#include <TDF_LabelSequence.hxx>
#include <TDocStd_Document.hxx>
#include <Quantity_Color.hxx>
#include <Graphic3d_Camera.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS_Compound.hxx>
#include <BRep_Builder.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <FSD_BinaryFile.hxx>
#include <PCDM_ReadWriter.hxx>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryFile>
#include <QTemporaryDir>
#include <QSaveFile>
#include <QSet>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
constexpr char kRootMarker[] = "OpenDesigner.RootAssembly.v1";
constexpr char kPartIdPrefix[] = "OpenDesigner.PartId.v1:";

QString ensureId(const QString& id) {
    return id.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : id;
}

void putMetadata(const TDF_Label& label, const NativePartState& part) {
    auto meta = label.NewChild();
    TDataStd_Name::Set(meta, QStringLiteral("OpenDesignerPartMetadata").toStdU16String().c_str());
    TDataStd_AsciiString::Set(label, TCollection_AsciiString((QString(kPartIdPrefix) + ensureId(part.id)).toLatin1().constData()));
    TDataStd_Integer::Set(meta, part.geometryKind);
    auto values = TDataStd_RealArray::Set(meta, 0, 8);
    values->SetValue(0, part.sphereRadius);
    values->SetValue(1, part.cylinderRadius);
    values->SetValue(2, part.cylinderHeight);
    values->SetValue(3, part.cylinderBaseOffset);
    values->SetValue(4, part.coneBottomRadius);
    values->SetValue(5, part.coneTopRadius);
    values->SetValue(6, part.coneHeight);
    values->SetValue(7, part.coneBaseOffset);
    const auto flags = meta.NewChild();
    TDataStd_Name::Set(flags, TCollection_ExtendedString("OpenDesignerPrimitiveFlags"));
    TDataStd_Integer::Set(flags, (part.cylinderPrimitive ? 1 : 0)
                                      | (part.spherePrimitive ? 2 : 0)
                                      | (part.conePrimitive ? 4 : 0));
}

void putCamera(const TDF_Label& root, const NativeCameraState& camera) {
    const auto label = root.NewChild();
    TDataStd_Name::Set(label, QStringLiteral("OpenDesignerCamera").toStdU16String().c_str());
    auto values = TDataStd_RealArray::Set(label, 0, 10);
    values->SetValue(0, camera.eye.X()); values->SetValue(1, camera.eye.Y()); values->SetValue(2, camera.eye.Z());
    values->SetValue(3, camera.center.X()); values->SetValue(4, camera.center.Y()); values->SetValue(5, camera.center.Z());
    values->SetValue(6, camera.up.X()); values->SetValue(7, camera.up.Y()); values->SetValue(8, camera.up.Z());
    values->SetValue(9, camera.scale);
    TDataStd_Integer::Set(label, camera.projectionType);
}

bool readCamera(const TDF_Label& root, NativeCameraState& camera) {
    for (TDF_ChildIterator it(root); it.More(); it.Next()) {
        const auto label = it.Value();
        Handle(TDataStd_Name) name;
        if (!label.FindAttribute(TDataStd_Name::GetID(), name) ||
            name->Get().IsEqual(TCollection_ExtendedString("OpenDesignerCamera")) == Standard_False) continue;
        Handle(TDataStd_RealArray) values;
        if (!label.FindAttribute(TDataStd_RealArray::GetID(), values) || values->Lower()!=0 || values->Upper() < 9) return false;
        camera.eye.SetCoord(values->Value(0), values->Value(1), values->Value(2));
        camera.center.SetCoord(values->Value(3), values->Value(4), values->Value(5));
        gp_Dir up(values->Value(6), values->Value(7), values->Value(8));
        camera.up = up;
        camera.scale = values->Value(9);
        Handle(TDataStd_Integer) projection;
        if (label.FindAttribute(TDataStd_Integer::GetID(), projection)) camera.projectionType = projection->Get();
        return std::isfinite(camera.scale) && camera.scale > 0.0;
    }
    return false;
}

QString idFromLabel(const TDF_Label& label) {
    Handle(TDataStd_AsciiString) id;
    if (!label.FindAttribute(TDataStd_AsciiString::GetID(), id)) return {};
    const QString value = QString::fromLatin1(id->Get().ToCString());
    return value.startsWith(kPartIdPrefix) ? value.mid(int(std::strlen(kPartIdPrefix))) : QString();
}

NativePartState readPart(const TDF_Label& label, const Handle(XCAFDoc_ColorTool)& colors) {
    NativePartState part;
    part.id = idFromLabel(label);
    Handle(TDataStd_Name) name;
    if (label.FindAttribute(TDataStd_Name::GetID(), name)) part.name = QString::fromUtf16(name->Get().ToExtString());
    TDF_Label referred;
    if (XCAFDoc_ShapeTool::GetReferredShape(label, referred)) {
        part.shape = XCAFDoc_ShapeTool::GetShape(referred);
        part.transform = XCAFDoc_ShapeTool::GetLocation(label).Transformation();
    } else {
        part.shape = XCAFDoc_ShapeTool::GetShape(label);
        part.transform = XCAFDoc_ShapeTool::GetLocation(label).Transformation();
    }
    Quantity_Color color;
    if (colors->GetColor(label, XCAFDoc_ColorSurf, color))
        part.color = QColor::fromRgbF(color.Red(), color.Green(), color.Blue());
    part.visible = colors->IsVisible(label) != Standard_False;
    for (TDF_ChildIterator it(label); it.More(); it.Next()) {
        Handle(TDataStd_RealArray) values;
        if (!it.Value().FindAttribute(TDataStd_RealArray::GetID(), values) || values->Lower()!=0 || values->Upper() < 7) continue;
        Handle(TDataStd_Integer) kind;
        if (it.Value().FindAttribute(TDataStd_Integer::GetID(), kind)) part.geometryKind = kind->Get();
        part.sphereRadius = values->Value(0); part.cylinderRadius = values->Value(1);
        part.cylinderHeight = values->Value(2); part.cylinderBaseOffset = values->Value(3);
        part.coneBottomRadius = values->Value(4); part.coneTopRadius = values->Value(5);
        part.coneHeight = values->Value(6); part.coneBaseOffset = values->Value(7);
        for (TDF_ChildIterator nested(it.Value()); nested.More(); nested.Next()) {
            Handle(TDataStd_Integer) flags;
            Handle(TDataStd_Name) flagName;
            if (nested.Value().FindAttribute(TDataStd_Name::GetID(), flagName)
                && flagName->Get().IsEqual(TCollection_ExtendedString("OpenDesignerPrimitiveFlags"))
                && nested.Value().FindAttribute(TDataStd_Integer::GetID(), flags)) {
                const int value = flags->Get();
                if (value >= 0 && value <= 7) {
                    part.cylinderPrimitive = (value & 1) != 0;
                    part.spherePrimitive = (value & 2) != 0;
                    part.conePrimitive = (value & 4) != 0;
                }
            }
        }
    }
    return part;
}

Handle(XCAFApp_Application) application() {
    auto app = XCAFApp_Application::GetApplication();
    static bool defined = false;
    if (!defined) { BinXCAFDrivers::DefineFormat(app); defined = true; }
    return app;
}

void validateSnapshot(const NativeDocumentSnapshot& snapshot) {
    if (snapshot.schemaVersion != NativeDocumentStorage::CurrentSchemaVersion)
        throw std::runtime_error("Unsupported OpenDesigner document schema.");
    if (!std::isfinite(snapshot.camera.scale) || snapshot.camera.scale <= 0.0)
        throw std::runtime_error("Invalid camera scale.");
    const auto& camera=snapshot.camera;
    for (int axis=1;axis<=3;++axis)
        if (!std::isfinite(camera.eye.Coord(axis)) || !std::isfinite(camera.center.Coord(axis))
            || !std::isfinite(camera.up.Coord(axis))) throw std::runtime_error("Invalid camera coordinates.");
    const gp_Vec direction(camera.eye,camera.center);
    if (direction.SquareMagnitude()<1.e-16 || direction.Crossed(gp_Vec(camera.up)).SquareMagnitude()<1.e-16
        || camera.projectionType<0 || camera.projectionType>4) throw std::runtime_error("Invalid camera orientation or projection.");
    QSet<QString> ids;
    for (const auto& part : snapshot.parts) {
        if (part.shape.IsNull()) throw std::runtime_error("Document contains a null Part shape.");
        if (!BRepCheck_Analyzer(part.shape).IsValid()) throw std::runtime_error("Document contains invalid B-Rep geometry.");
        if (part.id.isEmpty()) throw std::runtime_error("Document contains a Part without an ID.");
        if (ids.contains(part.id)) throw std::runtime_error("Document contains duplicate Part IDs.");
        ids.insert(part.id);
        for (int row=1;row<=3;++row) for (int column=1;column<=4;++column)
            if (!std::isfinite(part.transform.Value(row,column))) throw std::runtime_error("Invalid Part placement.");
    }
}
}

bool NativeDocumentStorage::save(const QString& path, const NativeDocumentSnapshot& snapshot, QString& error) {
    auto app = application();
    Handle(TDocStd_Document) document;
    try {
        validateSnapshot(snapshot);
        const QFileInfo targetInfo(path);
        QDir dir = targetInfo.absoluteDir();
        if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) throw std::runtime_error("Cannot create document directory.");
        QTemporaryDir staging(dir.filePath(QStringLiteral(".opendesigner-save-XXXXXX")));
        if (!staging.isValid()) throw std::runtime_error("Cannot create temporary document directory.");
        const QString temporaryPath=staging.filePath(QStringLiteral("document.xbf"));

        app->NewDocument(TCollection_ExtendedString("BinXCAF"), document);
        const auto shapeTool = XCAFDoc_DocumentTool::ShapeTool(document->Main());
        TopoDS_Compound compound; BRep_Builder builder; builder.MakeCompound(compound);
        for (const auto& part : snapshot.parts)
            builder.Add(compound, part.shape.Moved(TopLoc_Location(part.transform)));
        const auto root = shapeTool->AddShape(compound, Standard_True, Standard_True);
        TDataStd_Name::Set(root, TCollection_ExtendedString("Assembly 1"));
        TDataStd_AsciiString::Set(root, TCollection_AsciiString(kRootMarker));
        TDF_LabelSequence components;
        if (!XCAFDoc_ShapeTool::GetComponents(root, components, Standard_False) || components.Length() != int(snapshot.parts.size()))
            throw std::runtime_error("Failed to create the document assembly.");
        const auto colors = XCAFDoc_DocumentTool::ColorTool(document->Main());
        for (int i = 1; i <= components.Length(); ++i) {
            const auto& part = snapshot.parts[size_t(i - 1)];
            const auto label = components.Value(i);
            TDataStd_Name::Set(label, TCollection_ExtendedString(part.name.toStdU16String().c_str()));
            colors->SetColor(label, Quantity_Color(part.color.redF(), part.color.greenF(), part.color.blueF(), Quantity_TOC_RGB), XCAFDoc_ColorSurf);
            colors->SetVisibility(label, part.visible);
            putMetadata(label, part);
        }
        putCamera(root, snapshot.camera);
        TCollection_ExtendedString status;
        const auto tempName = temporaryPath.toStdU16String();
        const auto result = app->SaveAs(document, TCollection_ExtendedString(tempName.c_str()), status);
        if (result != PCDM_SS_OK) throw std::runtime_error(QString::fromUtf16(status.ToExtString()).toStdString());

        app->Close(document);
        document.Nullify();
        QFile input(temporaryPath);
        QSaveFile output(path);
        output.setDirectWriteFallback(false);
        if (!input.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly))
            throw std::runtime_error("Cannot prepare atomic document replacement.");
        while (!input.atEnd()) {
            const auto bytes=input.read(1024*1024);
            if (bytes.isEmpty() || output.write(bytes)!=bytes.size())
                throw std::runtime_error("Cannot write the complete document.");
        }
        if (!output.commit()) throw std::runtime_error("Cannot atomically replace the document.");
        error.clear(); return true;
    } catch (const Standard_Failure& e) { error = QString::fromUtf8(e.GetMessageString()); }
      catch (const std::exception& e) { error = QString::fromUtf8(e.what()); }
    if (!document.IsNull()) app->Close(document);
    return false;
}

bool NativeDocumentStorage::load(const QString& path, NativeDocumentSnapshot& snapshot, QString& error) {
    auto app = application();
    Handle(TDocStd_Document) document;
    try {
        QFile source(path);
        if (!source.open(QIODevice::ReadOnly) || source.size()<64
            || source.read(7)!=QByteArray(FSD_BinaryFile::MagicNumber()))
            throw std::runtime_error("The file is not a complete BinXCAF document.");
        source.close();
        QString openPath = path;
        QTemporaryDir staging;
        if (!path.endsWith(QStringLiteral(".xbf"), Qt::CaseInsensitive)) {
            if (!staging.isValid()) throw std::runtime_error("Cannot prepare temporary OpenDesigner document.");
            openPath = staging.filePath(QStringLiteral("document.xbf"));
            if (!QFile::copy(path, openPath))
                throw std::runtime_error("Cannot prepare the BinXCAF input stream.");
        }
        const auto nativePath = openPath.toStdU16String();
        if (!PCDM_ReadWriter::FileFormat(TCollection_ExtendedString(nativePath.c_str()))
                .IsEqual(TCollection_ExtendedString("BinXCAF")))
            throw std::runtime_error("The file is not a BinXCAF document.");
        const auto status = app->Open(TCollection_ExtendedString(nativePath.c_str()), document);
        if (status != PCDM_RS_OK || document.IsNull())
            throw std::runtime_error(QString("Cannot open the OpenDesigner document (status %1).").arg(static_cast<int>(status)).toStdString());
        const auto shapeTool = XCAFDoc_DocumentTool::ShapeTool(document->Main());
        const auto colors = XCAFDoc_DocumentTool::ColorTool(document->Main());
        TDF_LabelSequence freeShapes; shapeTool->GetFreeShapes(freeShapes);
        TDF_Label root;
        for (int i = 1; i <= freeShapes.Length(); ++i) {
            Handle(TDataStd_AsciiString) marker;
            if (freeShapes.Value(i).FindAttribute(TDataStd_AsciiString::GetID(), marker) && marker->Get().IsEqual(TCollection_AsciiString(kRootMarker))) { root = freeShapes.Value(i); break; }
        }
        if (root.IsNull()) throw std::runtime_error("Document has no OpenDesigner root assembly.");
        TDF_LabelSequence components;
        if (!shapeTool->GetComponents(root, components, Standard_False)) throw std::runtime_error("Document root is not an assembly.");
        NativeDocumentSnapshot loaded; loaded.schemaVersion = CurrentSchemaVersion;
        if (!readCamera(root, loaded.camera)) throw std::runtime_error("Document contains invalid camera metadata.");
        for (int i = 1; i <= components.Length(); ++i) {
            auto part = readPart(components.Value(i), colors);
            if (part.id.isEmpty()) part.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            if (part.name.isEmpty()) part.name = QStringLiteral("Part %1").arg(i);
            if (part.shape.IsNull()) throw std::runtime_error("Document contains an empty Part.");
            loaded.parts.push_back(std::move(part));
        }
        validateSnapshot(loaded);
        app->Close(document);
        snapshot = std::move(loaded); error.clear(); return true;
    } catch (const Standard_Failure& e) { error = QString::fromUtf8(e.GetMessageString()); }
      catch (const std::exception& e) { error = QString::fromUtf8(e.what()); }
    if (!document.IsNull()) app->Close(document);
    return false;
}
