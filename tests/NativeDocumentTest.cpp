#include "NativeDocument.h"

#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepGProp.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <TopExp_Explorer.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <GProp_GProps.hxx>
#include <QDir>
#include <QTemporaryDir>
#include <QDebug>
#include <QFile>
#include <gp_Ax1.hxx>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <XCAFApp_Application.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <TDocStd_Document.hxx>
#include <TDataStd_AsciiString.hxx>
#include <TDataStd_Integer.hxx>
#include <TDataStd_IntegerArray.hxx>
#include <TDataStd_RealArray.hxx>
#include <TDF_ChildIterator.hxx>
#include <TDF_LabelSequence.hxx>

namespace {
double volume(const TopoDS_Shape& shape) {
    GProp_GProps props;
    BRepGProp::VolumeProperties(shape, props);
    return props.Mass();
}
bool close(double a, double b) { return std::abs(a - b) <= 1.0e-8 * std::max({1.0, std::abs(a), std::abs(b)}); }
int count(const TopoDS_Shape& shape, TopAbs_ShapeEnum type) {
    int result = 0;
    for (TopExp_Explorer it(shape, type); it.More(); it.Next()) ++result;
    return result;
}
}

int main() {
    QTemporaryDir directory;
    if (!directory.isValid()) return 1;
    NativeDocumentSnapshot source;
    NativePartState box;
    box.id = QStringLiteral("part-box-id"); box.name = QStringLiteral("Main Box");
    box.shape = BRepPrimAPI_MakeBox(100.0, 80.0, 60.0).Shape();
    box.transform.SetTranslation(gp_Vec(12.0, -4.0, 8.0));
    box.color = QColor(40, 120, 220); box.visible = true;
    NativePartState cylinder;
    cylinder.id = QStringLiteral("part-cylinder-id"); cylinder.name = QStringLiteral("Verborgen β");
    cylinder.shape = BRepPrimAPI_MakeCylinder(15.0, 50.0).Shape();
    cylinder.cylinderPrimitive = true;
    cylinder.cylinderRadius = 15.0;
    cylinder.cylinderHeight = 50.0;
    cylinder.transform.SetTranslation(gp_Vec(-30.0, 10.0, 0.0));
    gp_Trsf rotation; rotation.SetRotation(gp_Ax1(gp_Pnt(),gp_Dir(1,1,0)),0.7);
    cylinder.transform=cylinder.transform*rotation;
    cylinder.color = QColor(220, 80, 40); cylinder.visible = false;
    source.parts = {box, cylinder};
    source.camera.eye = gp_Pnt(350.0, 420.0, 280.0);
    source.camera.center = gp_Pnt(20.0, 30.0, 10.0);
    source.camera.up = gp_Dir(0.0, 0.0, 1.0);
    source.camera.scale = 2.75;
    source.camera.projectionType = 1;

    const QString path = QDir(directory.path()).filePath(QStringLiteral("roundtrip.odc"));
    QString error;
    for(int i=0;i<3;++i) {
        Construction::Object object;object.id=QString("construction-%1").arg(i);object.name=QString::fromUtf8("Référence β ")+QString::number(i);
        object.kind=static_cast<Construction::Kind>(i);object.visible=i!=1;object.selectable=i!=2;object.extent=123.456;
        if(i==0) object.displayOffset=gp_Vec(23,-45,0);
        if(i==1) object.displayOffset=gp_Vec(0,0,345);
        object.frame=gp_Ax3(gp_Pnt(i*15,23,-12),gp_Dir(1,2,3));source.constructions.push_back(object);
    }
    source.constructionNameCounters={7,9,12};
    if (!NativeDocumentStorage::save(path, source, error)) { std::fprintf(stderr, "save: %s\n", error.toUtf8().constData()); return 2; }
    NativeDocumentSnapshot loaded;
    if (!NativeDocumentStorage::load(path, loaded, error)) { std::fprintf(stderr, "load: %s\n", error.toUtf8().constData()); return 3; }
    if (loaded.schemaVersion != NativeDocumentStorage::CurrentSchemaVersion || loaded.parts.size() != 2) return 4;
    if (loaded.parts[0].id != box.id || loaded.parts[0].name != box.name || loaded.parts[0].color != box.color || !loaded.parts[0].visible) return 5;
    if (loaded.parts[1].id != cylinder.id || loaded.parts[1].name != cylinder.name
        || loaded.parts[1].color != cylinder.color || loaded.parts[1].visible) return 6;
    if (!loaded.parts[1].cylinderPrimitive || !close(loaded.parts[1].cylinderRadius, cylinder.cylinderRadius)
        || !close(loaded.parts[1].cylinderHeight, cylinder.cylinderHeight)) return 15;
    if (!BRepCheck_Analyzer(loaded.parts[0].shape).IsValid() || !BRepCheck_Analyzer(loaded.parts[1].shape).IsValid()) return 7;
    if (count(loaded.parts[0].shape, TopAbs_FACE) != count(box.shape, TopAbs_FACE)
        || count(loaded.parts[0].shape, TopAbs_EDGE) != count(box.shape, TopAbs_EDGE)
        || count(loaded.parts[1].shape, TopAbs_FACE) != count(cylinder.shape, TopAbs_FACE)) return 8;
    if (!close(volume(loaded.parts[0].shape), volume(box.shape)) || !close(volume(loaded.parts[1].shape), volume(cylinder.shape))) return 9;
    for (size_t i=0;i<source.parts.size();++i) {
        GProp_GProps a,b,aa,bb;
        BRepGProp::VolumeProperties(source.parts[i].shape,a); BRepGProp::VolumeProperties(loaded.parts[i].shape,b);
        BRepGProp::SurfaceProperties(source.parts[i].shape,aa); BRepGProp::SurfaceProperties(loaded.parts[i].shape,bb);
        if (!a.CentreOfMass().IsEqual(b.CentreOfMass(),1.e-9) || !close(aa.Mass(),bb.Mass())) return 28;
    }
    if (!close(loaded.parts[0].transform.TranslationPart().X(), 12.0) || !close(loaded.parts[0].transform.TranslationPart().Y(), -4.0)) return 10;
    for (int row=1;row<=3;++row) for (int column=1;column<=4;++column)
        if (!close(loaded.parts[1].transform.Value(row,column),cylinder.transform.Value(row,column))) return 16;
    if (!loaded.camera.eye.IsEqual(source.camera.eye, 1.0e-9) || !loaded.camera.center.IsEqual(source.camera.center, 1.0e-9)
        || !loaded.camera.up.IsEqual(source.camera.up,1.e-9)
        || !close(loaded.camera.scale, source.camera.scale) || loaded.camera.projectionType != source.camera.projectionType) return 11;
    if(loaded.constructions.size()!=3) return 30;
    if(loaded.constructionNameCounters!=source.constructionNameCounters) return 60;
    for(size_t i=0;i<3;++i) if(!Construction::equivalent(source.constructions[i],loaded.constructions[i])) return 31;
    // Early v2 documents had no presentation offsets or name counters.
    const auto earlyPath=directory.filePath("early-v2.xbf");
    auto earlySource=source;earlySource.schemaVersion=2;
    if(!NativeDocumentStorage::save(earlyPath,earlySource,error)) return 61;
    {
        auto app=XCAFApp_Application::GetApplication();Handle(TDocStd_Document) document;
        if(app->Open(TCollection_ExtendedString(earlyPath.toStdU16String().c_str()),document)!=PCDM_RS_OK) return 62;
        TDF_LabelSequence roots;XCAFDoc_DocumentTool::ShapeTool(document->Main())->GetFreeShapes(roots);
        for(TDF_ChildIterator group(roots.Value(1));group.More();group.Next()) {
            Handle(TDataStd_AsciiString) marker;
            if(!group.Value().FindAttribute(TDataStd_AsciiString::GetID(),marker)
                || !marker->Get().IsEqual(TCollection_AsciiString("OpenDesigner.Construction.v1"))) continue;
            group.Value().ForgetAttribute(TDataStd_IntegerArray::GetID());
            for(TDF_ChildIterator record(group.Value());record.More();record.Next()) {
                for(TDF_ChildIterator child(record.Value());child.More();child.Next()) child.Value().ForgetAllAttributes(Standard_True);
            }
        }
        TCollection_ExtendedString status;
        if(app->SaveAs(document,TCollection_ExtendedString(earlyPath.toStdU16String().c_str()),status)!=PCDM_SS_OK) return 63;
        app->Close(document);
        NativeDocumentSnapshot early;
        if(!NativeDocumentStorage::load(earlyPath,early,error) || early.constructions.size()!=3
            || early.constructionNameCounters!=std::array<int,3>{}) return 64;
        for(size_t i=0;i<3;++i) {
            auto expected=source.constructions[i];expected.displayOffset=gp_Vec();
            if(!Construction::equivalent(expected,early.constructions[i])) return 65;
        }
    }
    auto legacy=source;legacy.schemaVersion=1;legacy.constructions.clear();
    const QString legacyPath=directory.filePath("legacy-v1.odc");NativeDocumentSnapshot legacyLoaded;
    if(!NativeDocumentStorage::save(legacyPath,legacy,error) || !NativeDocumentStorage::load(legacyPath,legacyLoaded,error)
        || !legacyLoaded.constructions.empty() || legacyLoaded.parts.size()!=2 || legacyLoaded.parts[0].id!=box.id) return 32;
    auto unsupported=source;unsupported.schemaVersion=99;
    if(NativeDocumentStorage::save(directory.filePath("future.odc"),unsupported,error)) return 33;
    auto duplicate=source;duplicate.constructions[0].id=box.id;
    if(NativeDocumentStorage::save(path,duplicate,error)) return 34;
    auto bad=source;bad.constructions[0].extent=-1;
    if(NativeDocumentStorage::save(path,bad,error)) return 35;
    NativeDocumentSnapshot unchanged=loaded;
    if (NativeDocumentStorage::load(QDir(directory.path()).filePath(QStringLiteral("missing.odc")), unchanged, error)) return 12;
    if (error.isEmpty() || unchanged.parts.size()!=2 || !unchanged.parts[0].shape.IsSame(loaded.parts[0].shape)) return 17;
    QFile saved(path); if (!saved.open(QIODevice::ReadOnly)) return 18;
    const QByteArray savedBytes=saved.readAll(); saved.close();
    auto invalid=source; invalid.camera.scale=0;
    if (NativeDocumentStorage::save(path,invalid,error) || error.isEmpty()) return 19;
    if (!saved.open(QIODevice::ReadOnly) || saved.readAll()!=savedBytes) return 20;
    saved.close();
    invalid=source; invalid.parts[1].id=invalid.parts[0].id;
    if (NativeDocumentStorage::save(path,invalid,error)) return 21;
    const QString corruptPath=directory.filePath("corrupt.odc");
    QFile corrupt(corruptPath); if (!corrupt.open(QIODevice::WriteOnly)) return 22;
    corrupt.write("not an OpenDesigner document"); corrupt.close();
    if (NativeDocumentStorage::load(corruptPath,unchanged,error)
        || !unchanged.parts[0].shape.IsSame(loaded.parts[0].shape)) return 23;
    // Corrupt real BinXCAF metadata, not merely the input snapshot. Every
    // reader rejection must leave the caller's existing document untouched.
    for(int failure=0;failure<9;++failure) {
        const auto malformed=directory.filePath(QString("malformed-%1.xbf").arg(failure));
        if(!QFile::copy(path,malformed)) return 40;
        auto app=XCAFApp_Application::GetApplication();Handle(TDocStd_Document) document;
        if(app->Open(TCollection_ExtendedString(malformed.toStdU16String().c_str()),document)!=PCDM_RS_OK) return 41;
        TDF_LabelSequence roots;XCAFDoc_DocumentTool::ShapeTool(document->Main())->GetFreeShapes(roots);
        const auto root=roots.Value(1);TDF_Label group;
        for(TDF_ChildIterator it(root);it.More();it.Next()) {
            Handle(TDataStd_AsciiString) marker;
            if(it.Value().FindAttribute(TDataStd_AsciiString::GetID(),marker) && marker->Get().IsEqual(TCollection_AsciiString("OpenDesigner.Construction.v1"))) group=it.Value();
        }
        if(group.IsNull()) return 42;
        const auto record=group.FindChild(1,Standard_False);
        if(failure==0) TDataStd_AsciiString::Set(root,TCollection_AsciiString("OpenDesigner.RootAssembly.v99"));
        if(failure==1) TDataStd_Integer::Set(root,99);
        if(failure==2) TDataStd_Integer::Set(record,99);
        if(failure==3) {
            Handle(TDataStd_RealArray) values;record.FindAttribute(TDataStd_RealArray::GetID(),values);
            for(int i=0;i<3;++i) values->SetValue(i+6,values->Value(i+3));
        }
        if(failure==4) TDataStd_AsciiString::Set(record,TCollection_AsciiString(box.id.toUtf8().constData()));
        if(failure==5) {
            Handle(TDataStd_IntegerArray) flags;record.FindAttribute(TDataStd_IntegerArray::GetID(),flags);flags->SetValue(1,2);
        }
        if(failure==6) {
            Handle(TDataStd_RealArray) offset;record.FindChild(1,Standard_False).FindAttribute(TDataStd_RealArray::GetID(),offset);
            offset->SetValue(2,1.0); // Workplane center must remain in its support plane.
        }
        if(failure==7) {
            Handle(TDataStd_IntegerArray) counters;group.FindAttribute(TDataStd_IntegerArray::GetID(),counters);counters->SetValue(0,-1);
        }
        if(failure==8) {
            const auto duplicate=record.NewChild();TDataStd_AsciiString::Set(duplicate,TCollection_AsciiString("OpenDesigner.Presentation.v1"));
            TDataStd_RealArray::Set(duplicate,0,2);
        }
        TCollection_ExtendedString status;
        if(app->SaveAs(document,TCollection_ExtendedString(malformed.toStdU16String().c_str()),status)!=PCDM_SS_OK) return 43;
        app->Close(document);
        if(NativeDocumentStorage::load(malformed,unchanged,error) || error.isEmpty()
            || !unchanged.parts[0].shape.IsSame(loaded.parts[0].shape) || unchanged.constructions.size()!=3) return 44+failure;
    }
    // Open must work without writing beside the source document.
    const QString readOnly=directory.filePath("readonly"); QDir().mkdir(readOnly);
    const QString readOnlyFile=readOnly+"/model.odc";
    if (!QFile::copy(path,readOnlyFile)) return 24;
    QFile::setPermissions(readOnly,QFile::ReadOwner|QFile::ExeOwner);
    const bool opened=NativeDocumentStorage::load(readOnlyFile,unchanged,error);
    QFile::setPermissions(readOnly,QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner);
    if (!opened) return 25;
    // A failed destination replacement cleans both staging and Qt temp files.
    if (NativeDocumentStorage::save(directory.path(),source,error)) return 26;
    if (!QDir(directory.path()).entryList({".opendesigner-*"},QDir::AllEntries|QDir::Hidden).empty()) return 27;
    NativeDocumentSnapshot empty = source;
    empty.parts.clear();
    const QString emptyPath = QDir(directory.path()).filePath(QStringLiteral("empty.odc"));
    if (!NativeDocumentStorage::save(emptyPath, empty, error)) return 13;
    NativeDocumentSnapshot emptyLoaded;
    if (!NativeDocumentStorage::load(emptyPath, emptyLoaded, error) || !emptyLoaded.parts.empty() || emptyLoaded.constructions.size()!=3) return 14;
    empty.constructions.clear();
    if(!NativeDocumentStorage::save(emptyPath,empty,error) || !NativeDocumentStorage::load(emptyPath,emptyLoaded,error)
        || !emptyLoaded.parts.empty() || !emptyLoaded.constructions.empty()) return 36;
    return 0;
}
