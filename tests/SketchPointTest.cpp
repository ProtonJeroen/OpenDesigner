#include "NativeDocument.h"
#include "SketchSnapping.h"
#include "SketchOverdraw.h"
#include "SketchConstruction.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <BRep_Tool.hxx>
#include <TopoDS.hxx>
#include <XCAFApp_Application.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <TDocStd_Document.hxx>
#include <TDF_LabelSequence.hxx>
#include <TDF_ChildIterator.hxx>
#include <TDataStd_AsciiString.hxx>
#include <TDataStd_Integer.hxx>
#include <TDataStd_IntegerArray.hxx>
#include <TDataStd_RealArray.hxx>
#include <algorithm>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace Sketch;
namespace {
void check(bool ok,const char* message) {if(!ok) throw std::runtime_error(message);}
Entity point(QString owner,Point p) {Entity e;e.workplaneId=owner;e.role=Role::Construction;e.geometry=ConstructionPoint{p};return e;}
NativeDocumentSnapshot fixture() {
    NativeDocumentSnapshot s;Construction::Object a;a.id="wp-a";a.name="WP1";a.kind=Construction::Kind::Workplane;
    a.frame=gp_Ax3(gp_Pnt(10,20,30),gp_Dir(1,2,3),gp_Dir(2,-1,0));a.displayOffset=gp_Vec(7,-8,0);
    auto b=a;b.id="wp-b";b.name="WP2";b.frame=gp_Ax3(gp_Pnt(120,0,0),gp_Dir(0,0,1));s.constructions={a,b};
    auto e=point(a.id,{12.345678901234567,-7.987654321012345});e.name=QString::fromUtf8("Construction point α");s.sketchEntities.push_back(e);
    e=point(a.id,{-25,15});e.name="Second point";s.sketchEntities.push_back(e);
    e=point(a.id,{20,15});e.name="Hidden point";e.visible=false;s.sketchEntities.push_back(e);
    e=point(a.id,{0,-25});e.name="Locked point";e.selectable=false;s.sketchEntities.push_back(e);
    e=point(b.id,{10,10});e.name="Other Workplane point";s.sketchEntities.push_back(e);
    Entity line;line.workplaneId=a.id;line.geometry=Segment{{-35,0},{35,0}};line.role=Role::Construction;line.name="Construction line";s.sketchEntities.push_back(line);
    line.id="circle";line.geometry=Circle{{},10};line.name="Construction circle";s.sketchEntities.push_back(line);
    s.camera.eye=gp_Pnt(600,-800,900);s.camera.up=gp_Dir(0,0,1);s.camera.scale=400;return s;
}
// Inspect actual BinXCAF records, rather than inferring on-disk schema from the
// upgraded in-memory snapshot version. An optional result writes a corrupt file.
void inspect(const QString& path,const std::function<void(const TDF_Label&)>& operation,const QString& result={}) {
    QTemporaryDir temp;const auto copy=temp.filePath("inspect.xbf");check(QFile::copy(path,copy),"inspection copy failed");
    const auto app=XCAFApp_Application::GetApplication();Handle(TDocStd_Document) doc;
    check(app->Open(TCollection_ExtendedString(copy.toStdU16String().c_str()),doc)==PCDM_RS_OK,"cannot inspect BinXCAF");
    TDF_LabelSequence roots;XCAFDoc_DocumentTool::ShapeTool(doc->Main())->GetFreeShapes(roots);check(roots.Length()==1,"wrong root count");
    operation(roots.Value(1));
    if(!result.isEmpty()) {TCollection_ExtendedString status;check(app->SaveAs(doc,TCollection_ExtendedString(result.toStdU16String().c_str()),status)==PCDM_SS_OK,"cannot write corrupt record fixture");}
    app->Close(doc);
}
void schema(const QString& path,int version) {
    inspect(path,[&](const TDF_Label& root) {
        Handle(TDataStd_AsciiString) marker;check(root.FindAttribute(TDataStd_AsciiString::GetID(),marker),"missing root marker");
        check(marker->Get().IsEqual(TCollection_AsciiString(QString("OpenDesigner.RootAssembly.v%1").arg(version).toUtf8().constData())),"wrong on-disk root version");
        if(version>=2) {Handle(TDataStd_Integer) n;check(root.FindAttribute(TDataStd_Integer::GetID(),n) && n->Get()==version,"wrong explicit schema integer");}
    });
}
QByteArray bytes(const QString& path) {QFile f(path);check(f.open(QIODevice::ReadOnly),"cannot read file");return f.readAll();}
}
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    try {
        auto source=fixture();QString error;const auto& p=source.sketchEntities.front();const auto exact=std::get<ConstructionPoint>(p.geometry).position;
        check(valid(p,error) && typeName(p.geometry)=="Construction Point","valid point refused or misnamed");
        auto invalid=p;invalid.role=Role::Profile;check(!valid(invalid,error),"point accepted as Profile");
        invalid=p;invalid.geometry=ConstructionPoint{{std::numeric_limits<double>::quiet_NaN(),0}};check(!valid(invalid,error),"NaN point accepted");
        invalid.geometry=ConstructionPoint{{0,std::numeric_limits<double>::infinity()}};check(!valid(invalid,error),"infinite point accepted");
        const auto shape=Sketch::shape(p,source.constructions[0].frame);
        check(shape.ShapeType()==TopAbs_VERTEX && BRep_Tool::Pnt(TopoDS::Vertex(shape)).IsEqual(worldPoint(exact,source.constructions[0].frame),1.e-12),"point shape lost exact located coordinates");
        check(contains(p.geometry,exact) && nearestPoint(p.geometry,{99,98})==exact,"point location query incorrect");
        const auto targets=snapTargets(source.sketchEntities,p.workplaneId,exact);
        check(std::count_if(targets.begin(),targets.end(),[](const auto& t){return t.kind==SnapKind::ConstructionPoint;})==2,"hidden/locked/other-owner points entered snap targets");
        for(double zoom:{0.01,1.0,1000.0}) {
            const auto project=[&](Point q){return Point{q.x*zoom+30,q.y*zoom-20};};const auto screen=project(exact);
            const auto hit=closestSnap({{exact,SnapKind::ConstructionPoint,"Construction point"}},{screen.x+8,screen.y},10,project);
            check(hit && hit->point==exact && !closestSnap({{exact,SnapKind::ConstructionPoint,{}}},{screen.x+11,screen.y},10,project),"point snap is not exact/pixel based");
        }
        const auto identity=[](Point q){return q;};
        auto hit=closestSnap({{{0,0},SnapKind::Nearest,{}},{{8,0},SnapKind::ConstructionPoint,{}}},{0,0},10,identity);
        check(hit && hit->kind==SnapKind::ConstructionPoint,"nearest curve displaced strong point snap");
        hit=closestSnap({{exact,SnapKind::ConstructionPoint,{}},{exact,SnapKind::Endpoint,{}}},exact,10,identity);
        check(hit && hit->kind==SnapKind::Endpoint,"existing snap tie priority changed");
        check(intersect(p.geometry,source.sketchEntities[5].geometry).kind==IntersectionKind::None && tangentPoints({},p.geometry).empty(),"point processed as curve");
        auto curves=source.sketchEntities;std::erase_if(curves,[](const auto& e){return std::holds_alternative<ConstructionPoint>(e.geometry);});
        OverdrawGraph before,after;check(overdrawCandidates(curves,p.workplaneId,before,error) && overdrawCandidates(source.sketchEntities,p.workplaneId,after,error),"points broke Overdraw");
        check(before.nodes==after.nodes && before.pieces.size()==after.pieces.size(),"points cut Overdraw curves");
        auto onCurve=p;onCurve.geometry=ConstructionPoint{{5,0}};auto withOnCurve=curves;withOnCurve.push_back(onCurve);
        check(intersect(onCurve.geometry,curves.front().geometry).kind==IntersectionKind::None
            && intersect(curves.front().geometry,onCurve.geometry).kind==IntersectionKind::None,"on-curve point became an intersection");
        check(overdrawCandidates(withOnCurve,p.workplaneId,after,error) && before.nodes==after.nodes
            && before.pieces.size()==after.pieces.size(),"on-curve point split Overdraw pieces");
        check(!overdrawCandidates({p},p.workplaneId,after,error),"point created an Overdraw piece");
        Geometry output=Circle{{},3};const auto original=output;
        check(!offsetConstructionLine(p.geometry,5,LineOutput::Infinite,{},output,error) && output==original,"point became offset line");
        std::vector<CommonTangent> tangents;check(!commonTangents(p.geometry,Circle{{},10},tangents,error),"point became common tangent reference");
        check(missingConstructionAxes({p},p.workplaneId,true,true).size()==2,"point suppressed construction axes");
        QTemporaryDir temp;const auto path=temp.filePath("points.odc");NativeDocumentSnapshot loaded;
        check(NativeDocumentStorage::save(path,source,error),qPrintable(error));schema(path,4);
        check(NativeDocumentStorage::load(path,loaded,error) && loaded.schemaVersion==4 && loaded.sketchEntities==source.sketchEntities,"v4 roundtrip changed point values/identity/flags/order");
        const auto savedBytes=bytes(path);auto bad=source;bad.schemaVersion=3;
        check(!NativeDocumentStorage::save(path,bad,error) && bytes(path)==savedBytes,"v3 save lost points or damaged existing file");
        bad=source;bad.sketchEntities.front().role=Role::Profile;
        check(!NativeDocumentStorage::save(path,bad,error) && bytes(path)==savedBytes,"invalid point save damaged file");
        for(int version:{1,2,3}) {
            NativeDocumentSnapshot legacy;legacy.schemaVersion=version;
            if(version>=2) legacy.constructions=source.constructions;
            if(version>=3) legacy.sketchEntities=curves;
            const auto file=temp.filePath(QString("legacy-%1.odc").arg(version));
            check(NativeDocumentStorage::save(file,legacy,error),qPrintable(error));schema(file,version);
            check(NativeDocumentStorage::load(file,loaded,error) && loaded.schemaVersion==4 && !hasConstructionPoints(loaded.sketchEntities)
                && loaded.sketchEntities==legacy.sketchEntities,"v1/v2/v3 migration lost curve data");
        }
        auto curveOnly=source;curveOnly.sketchEntities=curves;
        const auto v3=temp.filePath("automatic-v3.odc");check(NativeDocumentStorage::save(v3,curveOnly,error),qPrintable(error));schema(v3,3);
        // Removing the last point restores v3 output, including empty documents.
        curveOnly={};check(NativeDocumentStorage::save(v3,curveOnly,error),qPrintable(error));schema(v3,3);
        check(NativeDocumentStorage::load(path,loaded,error),qPrintable(error));const auto retained=loaded.sketchEntities;
        for(int failure=0;failure<8;++failure) {
            const auto malformed=temp.filePath(QString("malformed-%1.xbf").arg(failure));
            inspect(path,[&](const TDF_Label& root) {
                TDF_Label group;
                for(TDF_ChildIterator i(root);i.More();i.Next()) {Handle(TDataStd_AsciiString) m;if(i.Value().FindAttribute(TDataStd_AsciiString::GetID(),m) && m->Get().IsEqual(TCollection_AsciiString("OpenDesigner.Sketch.v1"))) group=i.Value();}
                check(!group.IsNull(),"missing sketch group");const auto record=group.FindChild(1,Standard_False).FindChild(1,Standard_False);
                if(failure==0) TDataStd_AsciiString::Set(root,TCollection_AsciiString("OpenDesigner.RootAssembly.v99"));
                if(failure==1) TDataStd_Integer::Set(root,99);
                if(failure==2) {TDataStd_AsciiString::Set(root,TCollection_AsciiString("OpenDesigner.RootAssembly.v3"));TDataStd_Integer::Set(root,3);}
                if(failure==3) {Handle(TDataStd_IntegerArray) f;record.FindAttribute(TDataStd_IntegerArray::GetID(),f);f->SetValue(0,5);}
                if(failure==4) {Handle(TDataStd_IntegerArray) f;record.FindAttribute(TDataStd_IntegerArray::GetID(),f);f->SetValue(1,1);}
                if(failure==5) TDataStd_RealArray::Set(record,0,2);
                if(failure==6) {Handle(TDataStd_RealArray) v;record.FindAttribute(TDataStd_RealArray::GetID(),v);v->SetValue(0,std::numeric_limits<double>::quiet_NaN());}
                if(failure==7) TDataStd_AsciiString::Set(record,TCollection_AsciiString("wp-a"));
            },malformed);
            check(!NativeDocumentStorage::load(malformed,loaded,error) && !error.isEmpty() && loaded.sketchEntities==retained,"malformed/future schema replaced valid point document");
        }
        if(app.arguments().size()==3 && app.arguments()[1]=="--fixture") {
            check(NativeDocumentStorage::save(app.arguments()[2],source,error),qPrintable(error));std::cout<<"GUI fixture: "<<app.arguments()[2].toStdString()<<'\n';
        }
        std::cout<<"Construction Points, curve isolation, snapping, v4 storage and v1/v2/v3 compatibility passed.\n";return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
