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
#include <TDataStd_IntegerArray.hxx>
#include <TDF_ChildIterator.hxx>
#include <TDF_LabelSequence.hxx>
#include <TDocStd_Document.hxx>
#include <Quantity_Color.hxx>
#include <Graphic3d_Camera.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS_Compound.hxx>
#include <BRep_Builder.hxx>
#include <BRepBuilderAPI_Copy.hxx>
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
#include <QJsonDocument>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <type_traits>

namespace {
constexpr char kRootMarker[] = "OpenDesigner.RootAssembly.v6";
constexpr char kV5RootMarker[] = "OpenDesigner.RootAssembly.v5";
constexpr char kV4RootMarker[] = "OpenDesigner.RootAssembly.v4";
constexpr char kV3RootMarker[] = "OpenDesigner.RootAssembly.v3";
constexpr char kV2RootMarker[] = "OpenDesigner.RootAssembly.v2";
constexpr char kSketchMarker[] = "OpenDesigner.Sketch.v1";
constexpr char kLegacyRootMarker[] = "OpenDesigner.RootAssembly.v1";
constexpr char kConstructionMarker[] = "OpenDesigner.Construction.v1";
constexpr char kPartIdPrefix[] = "OpenDesigner.PartId.v1:";

QString ensureId(const QString& id) {
    return id.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : id;
}
template<class> constexpr bool unhandledSketchType=false;
int sketchTag(const Sketch::Geometry& geometry) {
    return std::visit([](const auto& g) {
        using T=std::decay_t<decltype(g)>;
        if constexpr(std::is_same_v<T,Sketch::Segment>) return 0;
        else if constexpr(std::is_same_v<T,Sketch::InfiniteLine>) return 1;
        else if constexpr(std::is_same_v<T,Sketch::Circle>) return 2;
        else if constexpr(std::is_same_v<T,Sketch::Arc>) return 3;
        else if constexpr(std::is_same_v<T,Sketch::ConstructionPoint>) return 4;
        else if constexpr(std::is_same_v<T,Sketch::Ellipse>) return 5;
        else if constexpr(std::is_same_v<T,Sketch::EllipticalArc>) return 6;
        else if constexpr(std::is_same_v<T,Sketch::Spline>) return 7;
        else if constexpr(std::is_same_v<T,Sketch::BSpline>) return 8;
        else static_assert(unhandledSketchType<T>,"Assign an explicit native Sketch tag.");
    },geometry);
}

void putMetadata(const TDF_Label& label, const NativePartState& part) {
    if(!part.edgeBlendRecipe.isEmpty()) {
        const auto recipe=label.NewChild();
        TDataStd_Name::Set(recipe,TCollection_ExtendedString("OpenDesignerEdgeBlendRecipeV1"));
        const auto data=QJsonDocument(part.edgeBlendRecipe).toJson(QJsonDocument::Compact);
        TDataStd_AsciiString::Set(recipe,TCollection_AsciiString(data.constData()));
    }
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
        Handle(TDataStd_Name) recipeName;Handle(TDataStd_AsciiString) recipe;
        if(it.Value().FindAttribute(TDataStd_Name::GetID(),recipeName)
            &&recipeName->Get().IsEqual(TCollection_ExtendedString("OpenDesignerEdgeBlendRecipeV1"))
            &&it.Value().FindAttribute(TDataStd_AsciiString::GetID(),recipe)) {
            // Optional hint only; viewport reconstruction certifies it against
            // the authoritative current B-Rep after Open. Old readers ignore it.
            const auto data=QByteArray(recipe->Get().ToCString());
            if(data.size()<=4*1024*1024) part.edgeBlendRecipe=QJsonDocument::fromJson(data).object();
        }
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

void putConstructions(const TDF_Label& root,const std::vector<Construction::Object>& objects,const std::array<int,3>& counters) {
    const auto group=root.NewChild();
    TDataStd_AsciiString::Set(group,TCollection_AsciiString(kConstructionMarker));
    auto numbers=TDataStd_IntegerArray::Set(group,0,2);
    auto savedCounters=counters;Construction::advanceNameCounters(objects,savedCounters);
    for(int i=0;i<3;++i) numbers->SetValue(i,savedCounters[size_t(i)]);
    for(const auto& object:objects) {
        const auto label=group.NewChild();
        TDataStd_AsciiString::Set(label,TCollection_AsciiString(object.id.toUtf8().constData()));
        TDataStd_Name::Set(label,TCollection_ExtendedString(object.name.toStdU16String().c_str()));
        TDataStd_Integer::Set(label,static_cast<int>(object.kind));
        auto values=TDataStd_RealArray::Set(label,0,9);
        for(int i=1;i<=3;++i) {
            values->SetValue(i-1,object.frame.Location().Coord(i));
            values->SetValue(i+2,object.frame.Direction().Coord(i));
            values->SetValue(i+5,object.frame.XDirection().Coord(i));
        }
        values->SetValue(9,object.extent);
        auto flags=TDataStd_IntegerArray::Set(label,0,1);
        flags->SetValue(0,object.visible?1:0);flags->SetValue(1,object.selectable?1:0);
        const auto presentation=label.NewChild();
        TDataStd_AsciiString::Set(presentation,TCollection_AsciiString("OpenDesigner.Presentation.v1"));
        auto offset=TDataStd_RealArray::Set(presentation,0,2);
        for(int i=0;i<3;++i) offset->SetValue(i,object.displayOffset.Coord(i+1));
    }
}
void readConstructions(const TDF_Label& root,std::vector<Construction::Object>& objects,std::array<int,3>& counters) {
    bool found=false;
    for(TDF_ChildIterator group(root);group.More();group.Next()) {
        Handle(TDataStd_AsciiString) marker;
        if(!group.Value().FindAttribute(TDataStd_AsciiString::GetID(),marker)
            || !marker->Get().IsEqual(TCollection_AsciiString(kConstructionMarker))) continue;
        if(found) throw std::runtime_error("Duplicate construction metadata group.");
        found=true;
        Handle(TDataStd_IntegerArray) numbers;
        if(group.Value().FindAttribute(TDataStd_IntegerArray::GetID(),numbers)) {
            if(numbers->Lower()!=0 || numbers->Upper()!=2) throw std::runtime_error("Invalid construction name counters.");
            for(int i=0;i<3;++i) {
                if(numbers->Value(i)<0) throw std::runtime_error("Invalid construction name counters.");
                counters[size_t(i)]=numbers->Value(i);
            }
        }
        for(TDF_ChildIterator it(group.Value());it.More();it.Next()) {
            const auto label=it.Value();
            Handle(TDataStd_Name) name;Handle(TDataStd_AsciiString) id;Handle(TDataStd_Integer) kind;
            Handle(TDataStd_RealArray) values;Handle(TDataStd_IntegerArray) flags;
            if(!label.FindAttribute(TDataStd_Name::GetID(),name) || !label.FindAttribute(TDataStd_AsciiString::GetID(),id)
                || !label.FindAttribute(TDataStd_Integer::GetID(),kind) || kind->Get()<0 || kind->Get()>2
                || !label.FindAttribute(TDataStd_RealArray::GetID(),values) || values->Lower()!=0 || values->Upper()!=9
                || !label.FindAttribute(TDataStd_IntegerArray::GetID(),flags) || flags->Lower()!=0 || flags->Upper()!=1
                || flags->Value(0)<0 || flags->Value(0)>1 || flags->Value(1)<0 || flags->Value(1)>1)
                throw std::runtime_error("Invalid construction metadata.");
            for(int i=0;i<=9;++i) if(!std::isfinite(values->Value(i))) throw std::runtime_error("Non-finite construction data.");
            gp_Vec z(values->Value(3),values->Value(4),values->Value(5)),x(values->Value(6),values->Value(7),values->Value(8));
            if(std::abs(z.Magnitude()-1.0)>1.e-10 || std::abs(x.Magnitude()-1.0)>1.e-10 || std::abs(z.Dot(x))>1.e-10)
                throw std::runtime_error("Invalid construction frame directions.");
            Construction::Object object;
            object.id=QString::fromUtf8(id->Get().ToCString());object.name=QString::fromUtf16(name->Get().ToExtString());
            object.kind=static_cast<Construction::Kind>(kind->Get());
            object.frame=gp_Ax3(gp_Pnt(values->Value(0),values->Value(1),values->Value(2)),gp_Dir(z),gp_Dir(x));
            object.extent=values->Value(9);object.visible=flags->Value(0)!=0;object.selectable=flags->Value(1)!=0;
            bool foundPresentation=false;
            for(TDF_ChildIterator child(label);child.More();child.Next()) {
                Handle(TDataStd_AsciiString) type;
                if(!child.Value().FindAttribute(TDataStd_AsciiString::GetID(),type)
                    || !type->Get().IsEqual(TCollection_AsciiString("OpenDesigner.Presentation.v1"))) continue;
                Handle(TDataStd_RealArray) offset;
                if(foundPresentation || !child.Value().FindAttribute(TDataStd_RealArray::GetID(),offset)
                    || offset->Lower()!=0 || offset->Upper()!=2) throw std::runtime_error("Invalid construction presentation metadata.");
                foundPresentation=true;
                object.displayOffset=gp_Vec(offset->Value(0),offset->Value(1),offset->Value(2));
            }
            objects.push_back(object);
        }
    }
    if(!found) throw std::runtime_error("Missing construction metadata group.");
    Construction::advanceNameCounters(objects,counters);
}

void putSketches(const TDF_Label& root,const std::vector<Sketch::Entity>& entities) {
    const auto group=root.NewChild();
    TDataStd_AsciiString::Set(group,TCollection_AsciiString(kSketchMarker));
    // One 2D collection per Workplane. Entity values contain no source references.
    std::map<QString,TDF_Label> planes;
    int order=0;
    for(const auto& entity:entities) {
        if(!planes.contains(entity.workplaneId)) {
            const auto plane=group.NewChild();planes[entity.workplaneId]=plane;
            TDataStd_AsciiString::Set(plane,TCollection_AsciiString(entity.workplaneId.toUtf8().constData()));
        }
        const auto label=planes.at(entity.workplaneId).NewChild();
        TDataStd_Integer::Set(label,order++);
        TDataStd_AsciiString::Set(label,TCollection_AsciiString(entity.id.toUtf8().constData()));
        TDataStd_Name::Set(label,TCollection_ExtendedString(entity.name.toStdU16String().c_str()));
        auto flags=TDataStd_IntegerArray::Set(label,0,3);
        flags->SetValue(0,sketchTag(entity.geometry));flags->SetValue(1,int(entity.role));
        flags->SetValue(2,entity.visible?1:0);flags->SetValue(3,entity.selectable?1:0);
        std::vector<double> data;
        std::visit([&](const auto& g) {
            using T=std::decay_t<decltype(g)>;
            if constexpr(std::is_same_v<T,Sketch::Segment>) data={g.start.x,g.start.y,g.end.x,g.end.y};
            else if constexpr(std::is_same_v<T,Sketch::InfiniteLine>) data={g.origin.x,g.origin.y,g.direction.x,g.direction.y};
            else if constexpr(std::is_same_v<T,Sketch::Circle>) data={g.center.x,g.center.y,g.radius};
            else if constexpr(std::is_same_v<T,Sketch::ConstructionPoint>) data={g.position.x,g.position.y};
            else if constexpr(std::is_same_v<T,Sketch::Arc>) data={g.center.x,g.center.y,g.radius,g.startAngle,g.sweepAngle};
            else if constexpr(std::is_same_v<T,Sketch::Ellipse>) data={g.center.x,g.center.y,g.majorRadius,g.minorRadius,g.axisAngle};
            else if constexpr(std::is_same_v<T,Sketch::EllipticalArc>) data={g.ellipse.center.x,g.ellipse.center.y,g.ellipse.majorRadius,g.ellipse.minorRadius,g.ellipse.axisAngle,g.startParameter,g.sweepParameter};
            else if constexpr(std::is_same_v<T,Sketch::Spline>) {
                const auto a=g.startTangent.value_or(Sketch::Point{}),b=g.endTangent.value_or(Sketch::Point{});
                data={double(g.closed),double(g.points.size()),double(g.startTangent.has_value()),double(g.endTangent.has_value()),a.x,a.y,b.x,b.y};
                for(auto p:g.points) {data.push_back(p.x);data.push_back(p.y);}
            }
            else if constexpr(std::is_same_v<T,Sketch::BSpline>) {
                data={double(g.degree),double(g.periodic),double(g.reversed),g.first,g.last,double(g.poles.size()),double(g.knots.size())};
                for(size_t i=0;i<g.poles.size();++i) {data.push_back(g.poles[i].x);data.push_back(g.poles[i].y);data.push_back(g.weights[i]);}
                for(size_t i=0;i<g.knots.size();++i) {data.push_back(g.knots[i]);data.push_back(g.multiplicities[i]);}
            }
            else static_assert(unhandledSketchType<T>,"Serialize the new Sketch type explicitly.");
        },entity.geometry);
        auto values=TDataStd_RealArray::Set(label,0,int(data.size())-1);
        for(int i=0;i<int(data.size());++i) values->SetValue(i,data[size_t(i)]);
    }
}
void readSketches(const TDF_Label& root,std::vector<Sketch::Entity>& entities,const std::vector<Construction::Object>& constructions,int schema) {
    bool found=false;QSet<QString> owners;
    std::map<int,Sketch::Entity> ordered;
    for(TDF_ChildIterator group(root);group.More();group.Next()) {
        Handle(TDataStd_AsciiString) marker;
        if(!group.Value().FindAttribute(TDataStd_AsciiString::GetID(),marker)
           || !marker->Get().IsEqual(TCollection_AsciiString(kSketchMarker))) continue;
        if(found) throw std::runtime_error("Duplicate 2D geometry group.");
        found=true;
        for(TDF_ChildIterator plane(group.Value());plane.More();plane.Next()) {
            Handle(TDataStd_AsciiString) owner;
            if(!plane.Value().FindAttribute(TDataStd_AsciiString::GetID(),owner)) throw std::runtime_error("Missing 2D Workplane ID.");
            const auto ownerId=QString::fromUtf8(owner->Get().ToCString());
            if(owners.contains(ownerId) || ownerId.isEmpty()) throw std::runtime_error("Invalid or duplicate 2D Workplane collection.");
            if(std::none_of(constructions.begin(),constructions.end(),[&](const auto& o){return o.id==ownerId && o.kind==Construction::Kind::Workplane;}))
                throw std::runtime_error("2D collection has no owning Workplane.");
            owners.insert(ownerId);
            for(TDF_ChildIterator record(plane.Value());record.More();record.Next()) {
                const auto label=record.Value();
                Handle(TDataStd_AsciiString) id;Handle(TDataStd_Name) name;Handle(TDataStd_Integer) order;
                Handle(TDataStd_IntegerArray) flags;Handle(TDataStd_RealArray) values;
                if(!label.FindAttribute(TDataStd_AsciiString::GetID(),id) || !label.FindAttribute(TDataStd_Name::GetID(),name)
                   || !label.FindAttribute(TDataStd_Integer::GetID(),order) || order->Get()<0 || ordered.contains(order->Get())
                   || !label.FindAttribute(TDataStd_IntegerArray::GetID(),flags) || flags->Lower()!=0 || flags->Upper()!=3
                   || !label.FindAttribute(TDataStd_RealArray::GetID(),values) || values->Lower()!=0)
                    throw std::runtime_error("Invalid 2D geometry metadata.");
                const int kind=flags->Value(0);const int sizes[]={4,4,3,5,2,5,7};
                if(kind<0 || kind>(schema>=6?8:schema>=5?6:schema>=4?4:3) || (kind<7 && values->Upper()!=sizes[kind]-1)
                   || (kind==8 && (values->Upper()<16 || values->Upper()>50010))
                   || (kind==7 && (values->Upper()<11 || values->Upper()>20007 || (values->Upper()-7)%2))) throw std::runtime_error("Unsupported 2D geometry type or data size.");
                for(int i=1;i<=3;++i) if(flags->Value(i)<0 || flags->Value(i)>1) throw std::runtime_error("Invalid 2D flags.");
                for(int i=0;i<=values->Upper();++i) if(!std::isfinite(values->Value(i))) throw std::runtime_error("Non-finite 2D geometry.");
                Sketch::Entity entity;entity.id=QString::fromUtf8(id->Get().ToCString());entity.workplaneId=ownerId;
                entity.name=QString::fromUtf16(name->Get().ToExtString());entity.role=static_cast<Sketch::Role>(flags->Value(1));
                entity.visible=flags->Value(2)!=0;entity.selectable=flags->Value(3)!=0;
                const Sketch::Point point{values->Value(0),values->Value(1)};
                if(kind==0) entity.geometry=Sketch::Segment{point,{values->Value(2),values->Value(3)}};
                if(kind==1) entity.geometry=Sketch::InfiniteLine{point,{values->Value(2),values->Value(3)}};
                if(kind==2) entity.geometry=Sketch::Circle{point,values->Value(2)};
                if(kind==3) entity.geometry=Sketch::Arc{point,values->Value(2),values->Value(3),values->Value(4)};
                if(kind==4) entity.geometry=Sketch::ConstructionPoint{point};
                if(kind==5) entity.geometry=Sketch::Ellipse{point,values->Value(2),values->Value(3),values->Value(4)};
                if(kind==6) entity.geometry=Sketch::EllipticalArc{{point,values->Value(2),values->Value(3),values->Value(4)},values->Value(5),values->Value(6)};
                if(kind==7) {
                    for(int index:{0,2,3}) if(values->Value(index)!=0 && values->Value(index)!=1) throw std::runtime_error("Invalid spline flag.");
                    const int count=(values->Upper()-7)/2;
                    if(values->Value(1)!=count) throw std::runtime_error("Invalid spline point count.");
                    Sketch::Spline spline;spline.closed=values->Value(0)!=0;
                    if(values->Value(2)) spline.startTangent=Sketch::Point{values->Value(4),values->Value(5)};
                    if(values->Value(3)) spline.endTangent=Sketch::Point{values->Value(6),values->Value(7)};
                    for(int i=0;i<count;++i) spline.points.push_back({values->Value(8+2*i),values->Value(9+2*i)});
                    entity.geometry=std::move(spline);
                }
                if(kind==8) {
                    const auto integer=[&](int index,int min,int max) {
                        const double v=values->Value(index);if(v<min || v>max || v!=std::floor(v)) throw std::runtime_error("Invalid B-spline integer field.");return int(v);
                    };
                    Sketch::BSpline s;s.degree=integer(0,1,25);s.periodic=integer(1,0,1);s.reversed=integer(2,0,1);s.first=values->Value(3);s.last=values->Value(4);
                    const int poles=integer(5,2,10000),knots=integer(6,2,10002);
                    if(values->Upper()!=6+3*poles+2*knots) throw std::runtime_error("Invalid B-spline data length.");
                    for(int i=0;i<poles;++i) {s.poles.push_back({values->Value(7+3*i),values->Value(8+3*i)});s.weights.push_back(values->Value(9+3*i));}
                    for(int i=0;i<knots;++i) {s.knots.push_back(values->Value(7+3*poles+2*i));s.multiplicities.push_back(integer(8+3*poles+2*i,1,s.degree+1));}
                    entity.geometry=std::move(s);
                }
                ordered.emplace(order->Get(),std::move(entity));
            }
        }
    }
    if(!found) throw std::runtime_error("Missing 2D geometry group.");
    for(auto& [order,entity]:ordered) {
        if(order!=int(entities.size())) throw std::runtime_error("Invalid 2D entity order.");
        entities.push_back(std::move(entity));
    }
}

Handle(XCAFApp_Application) application() {
    auto app = XCAFApp_Application::GetApplication();
    static bool defined = false;
    if (!defined) { BinXCAFDrivers::DefineFormat(app); defined = true; }
    return app;
}

void validateSnapshot(const NativeDocumentSnapshot& snapshot) {
    if ((snapshot.schemaVersion < 1 || snapshot.schemaVersion > NativeDocumentStorage::CurrentSchemaVersion))
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
    if(snapshot.schemaVersion<NativeDocumentStorage::requiredSchemaVersion(snapshot)) {
        // One shared predicate decides the refusal; the specific reasons stay distinct.
        if(snapshot.schemaVersion==1 && !snapshot.constructions.empty())
            throw std::runtime_error("Schema v1 cannot store construction geometry.");
        if(snapshot.schemaVersion<3 && !snapshot.sketchEntities.empty())
            throw std::runtime_error("Schema v1/v2 cannot store 2D geometry.");
        throw std::runtime_error("The requested schema cannot store these 2D geometry types (points require v4; ellipses require v5; splines require v6).");
    }
    QSet<QString> ids;
    for(int counter:snapshot.constructionNameCounters) if(counter<0) throw std::runtime_error("Invalid construction name counters.");
    for(const auto& object:snapshot.constructions) {
        QString error;
        if(!Construction::valid(object,error)) throw std::runtime_error(error.toStdString());
        if(ids.contains(object.id)) throw std::runtime_error("Duplicate document object ID.");
        ids.insert(object.id);
    }
    for (const auto& part : snapshot.parts) {
        if (part.shape.IsNull()) throw std::runtime_error("Document contains a null Part shape.");
        if (!BRepCheck_Analyzer(part.shape).IsValid()) throw std::runtime_error("Document contains invalid B-Rep geometry.");
        if (part.id.isEmpty()) throw std::runtime_error("Document contains a Part without an ID.");
        if (ids.contains(part.id)) throw std::runtime_error("Document contains duplicate Part IDs.");
        ids.insert(part.id);
        for (int row=1;row<=3;++row) for (int column=1;column<=4;++column)
            if (!std::isfinite(part.transform.Value(row,column))) throw std::runtime_error("Invalid Part placement.");
    }
    if(!snapshot.activePartId.isEmpty() && std::none_of(snapshot.parts.begin(),snapshot.parts.end(),[&](const auto& p){return p.id==snapshot.activePartId;}))
        throw std::runtime_error("Active Part does not exist.");
    QString error;
    if(!Sketch::validate(snapshot.sketchEntities,snapshot.constructions,ids.values(),error))
        throw std::runtime_error(error.toStdString());
}
}

int NativeDocumentStorage::sketchSchemaVersion(const std::vector<Sketch::Entity>& entities) {
    if(std::any_of(entities.begin(),entities.end(),[](const auto& e){return std::holds_alternative<Sketch::Spline>(e.geometry) || std::holds_alternative<Sketch::BSpline>(e.geometry);})) return 6;
    return Sketch::hasEllipses(entities)?5:Sketch::hasConstructionPoints(entities)?4:3;
}

int NativeDocumentStorage::requiredSchemaVersion(const NativeDocumentSnapshot& snapshot) {
    const int content=snapshot.constructions.empty()?1:2;
    if(snapshot.sketchEntities.empty()) return content;
    return std::max(content,sketchSchemaVersion(snapshot.sketchEntities));
}

bool NativeDocumentStorage::save(const QString& path, const NativeDocumentSnapshot& snapshot, QString& error) {
    auto app = application();
    Handle(TDocStd_Document) document;
    try {
        validateSnapshot(snapshot);
        const int schema=snapshot.schemaVersion>=4?sketchSchemaVersion(snapshot.sketchEntities):snapshot.schemaVersion;
        const QFileInfo targetInfo(path);
        QDir dir = targetInfo.absoluteDir();
        if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) throw std::runtime_error("Cannot create document directory.");
        QTemporaryDir staging(dir.filePath(QStringLiteral(".opendesigner-save-XXXXXX")));
        if (!staging.isValid()) throw std::runtime_error("Cannot create temporary document directory.");
        const QString temporaryPath=staging.filePath(QStringLiteral("document.xbf"));

        app->NewDocument(TCollection_ExtendedString("BinXCAF"), document);
        const auto shapeTool = XCAFDoc_DocumentTool::ShapeTool(document->Main());
        TopoDS_Compound compound; BRep_Builder builder; builder.MakeCompound(compound);
        for (const auto& part : snapshot.parts) {
            // Exported snapshots may share the live document's TShapes. Add()
            // freezes its child; XCAF/storage must only receive owned topology.
            // Copy geometry and meshes as well so serialization cannot mutate
            // shared support data. Keep caller identity and placement untouched.
            BRepBuilderAPI_Copy owned(part.shape, Standard_True, Standard_True);
            if (!owned.IsDone() || owned.Shape().IsNull())
                throw std::runtime_error("Could not isolate Part geometry for saving.");
            builder.Add(compound, owned.Shape().Moved(TopLoc_Location(part.transform)));
        }
        const auto root = shapeTool->AddShape(compound, Standard_True, Standard_True);
        TDataStd_Name::Set(root, TCollection_ExtendedString("Assembly 1"));
        TDataStd_AsciiString::Set(root, TCollection_AsciiString(schema==1?kLegacyRootMarker:schema==2?kV2RootMarker:schema==3?kV3RootMarker:schema==4?kV4RootMarker:schema==5?kV5RootMarker:kRootMarker));
        if(schema>=2) TDataStd_Integer::Set(root,schema);
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
        if (!snapshot.activePartId.isEmpty()) {
            auto label=root.NewChild();
            TDataStd_Name::Set(label,TCollection_ExtendedString("OpenDesignerActivePart.v1"));
            TDataStd_AsciiString::Set(label,TCollection_AsciiString(snapshot.activePartId.toUtf8().constData()));
        }
        putCamera(root, snapshot.camera);
        if(snapshot.schemaVersion>=2) putConstructions(root,snapshot.constructions,snapshot.constructionNameCounters);
        if(snapshot.schemaVersion>=3) putSketches(root,snapshot.sketchEntities);
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
        int schema=0;
        for (int i = 1; i <= freeShapes.Length(); ++i) {
            Handle(TDataStd_AsciiString) marker;
            if (!freeShapes.Value(i).FindAttribute(TDataStd_AsciiString::GetID(), marker)) continue;
            const bool legacy=marker->Get().IsEqual(TCollection_AsciiString(kLegacyRootMarker));
            const bool v2=marker->Get().IsEqual(TCollection_AsciiString(kV2RootMarker));
            const bool v3=marker->Get().IsEqual(TCollection_AsciiString(kV3RootMarker));
            const bool v4=marker->Get().IsEqual(TCollection_AsciiString(kV4RootMarker));
            const bool v5=marker->Get().IsEqual(TCollection_AsciiString(kV5RootMarker));
            if(!legacy && !v2 && !v3 && !v4 && !v5 && !marker->Get().IsEqual(TCollection_AsciiString(kRootMarker))) {
                if(marker->Get().Search("OpenDesigner.RootAssembly.")==1)
                    throw std::runtime_error("Unsupported OpenDesigner document schema.");
                continue;
            }
            if(!root.IsNull()) throw std::runtime_error("Multiple OpenDesigner root assemblies.");
            root=freeShapes.Value(i);schema=legacy?1:v2?2:v3?3:v4?4:v5?5:6;
            Handle(TDataStd_Integer) version;
            if(!legacy && (!root.FindAttribute(TDataStd_Integer::GetID(),version) || version->Get()!=schema))
                throw std::runtime_error("Unsupported OpenDesigner document schema.");
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
        if(schema>=2) readConstructions(root,loaded.constructions,loaded.constructionNameCounters);
        if(schema>=3) readSketches(root,loaded.sketchEntities,loaded.constructions,schema);
        bool foundActivePart=false;
        for(TDF_ChildIterator it(root);it.More();it.Next()) {
            Handle(TDataStd_Name) name; Handle(TDataStd_AsciiString) value;
            if(it.Value().FindAttribute(TDataStd_Name::GetID(),name)
                && name->Get()==TCollection_ExtendedString("OpenDesignerActivePart.v1")) {
                if(foundActivePart || !it.Value().FindAttribute(TDataStd_AsciiString::GetID(),value))
                    throw std::runtime_error("Invalid Active Part metadata.");
                foundActivePart=true;
                loaded.activePartId=QString::fromUtf8(value->Get().ToCString());
            }
        }
        validateSnapshot(loaded);
        app->Close(document);
        snapshot = std::move(loaded); error.clear(); return true;
    } catch (const Standard_Failure& e) { error = QString::fromUtf8(e.GetMessageString()); }
      catch (const std::exception& e) { error = QString::fromUtf8(e.what()); }
    if (!document.IsNull()) app->Close(document);
    return false;
}
