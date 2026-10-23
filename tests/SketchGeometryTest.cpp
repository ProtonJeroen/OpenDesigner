#include "SketchGeometry.h"
#include "NativeDocument.h"
#include <QTemporaryDir>
#include <QFile>
#include <BRepAdaptor_Curve.hxx>
#include <BRep_Tool.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <TopExp.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Vertex.hxx>
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
#include <limits>
#include <iostream>
#include <stdexcept>
#include <cmath>

namespace {
void check(bool ok,const char* message) {if(!ok) throw std::runtime_error(message);}
constexpr double pi=3.14159265358979323846;
std::vector<Sketch::Entity> sample() {
    std::vector<Sketch::Entity> entities;
    for(int i=0;i<7;++i) {
        Sketch::Entity e;e.workplaneId=i%2?"wp2":"wp1";e.name=QString::fromUtf8("Géométrie α %1").arg(i);
        e.role=i<4?Sketch::Role::Construction:Sketch::Role::Profile;
        if(i==0 || i==4) e.geometry=Sketch::Segment{{-1.23456789123,3.1},{35.00000000001,7.4}};
        if(i==1) e.geometry=Sketch::InfiniteLine{{-123,45},{0.6,0.8}};
        if(i==2 || i==5) e.geometry=Sketch::Circle{{3.123456789,-7.8},12.345678912345};
        if(i==3 || i==6) e.geometry=Sketch::Arc{{-8,9},17.7654321,5.9,i==3?0.8:-1.3};
        if(i==6) e.name.clear();
        e.visible=i!=2;e.selectable=i!=4;entities.push_back(e);
    }
    return entities;
}
}
int main() {
    try {
        QString error;const auto entities=sample();
        Construction::Object wp;wp.id="wp1";wp.name="WP1";wp.kind=Construction::Kind::Workplane;
        wp.frame=gp_Ax3(gp_Pnt(10,20,30),gp_Dir(0,1,0),gp_Dir(0,0,1));wp.displayOffset=gp_Vec(80,90,0);
        auto wp2=wp;wp2.id="wp2";wp2.name="WP2";wp2.frame.SetLocation(gp_Pnt(-5,7,100));
        const std::vector<Construction::Object> owners{wp,wp2};
        check(Sketch::validate(entities,owners,{wp.id,wp2.id},error),"valid entity set refused");
        const auto point=Sketch::worldPoint({2,3},wp.frame);
        check(point.IsEqual(gp_Pnt(13,20,32),1.e-12),"local XY frame or display offset leaked into coordinates");
        gp_Trsf transform;transform.SetRotation(gp_Ax1(gp_Pnt(3,4,5),gp_Dir(1,2,3)),0.7);
        auto moved=wp.frame;moved.Transform(transform);
        check(Sketch::worldPoint({2,3},moved).IsEqual(point.Transformed(transform),1.e-12),"Workplane transform mismatch");
        for(const auto& e:entities) {
            auto shape=Sketch::shape(e,wp.frame,47);check(!shape.IsNull(),"missing exact edge");
            BRepAdaptor_Curve curve(TopoDS::Edge(shape));
            check(curve.GetType()==(e.geometry.index()<2?GeomAbs_Line:GeomAbs_Circle),"geometry lost analytic type");
            if(const auto* arc=std::get_if<Sketch::Arc>(&e.geometry)) {
                const auto start=Sketch::worldPoint({arc->center.x+arc->radius*std::cos(arc->startAngle),arc->center.y+arc->radius*std::sin(arc->startAngle)},wp.frame);
                const auto vertex=TopExp::FirstVertex(TopoDS::Edge(shape),Standard_True);
                check(BRep_Tool::Pnt(vertex).IsEqual(start,1.e-9),"signed arc start or seam crossing incorrect");
                GProp_GProps properties;BRepGProp::LinearProperties(shape,properties);
                check(std::abs(properties.Mass()-arc->radius*std::abs(arc->sweepAngle))<1.e-8,"arc sweep changed");
            }
        }
        auto invalid=entities.front();invalid.geometry=Sketch::Segment{{1,1},{1,1}};
        check(!Sketch::valid(invalid,error),"zero length segment accepted");
        invalid.geometry=Sketch::Circle{{},-1};check(!Sketch::valid(invalid,error),"negative radius accepted");
        invalid.geometry=Sketch::InfiniteLine{};invalid.role=Sketch::Role::Profile;
        check(!Sketch::valid(invalid,error),"infinite Profile accepted");
        invalid.role=Sketch::Role::Construction;invalid.geometry=Sketch::InfiniteLine{{},{1,1}};
        check(!Sketch::valid(invalid,error),"non-unit direction accepted");
        invalid.geometry=Sketch::Arc{{},1,0,2*pi};check(!Sketch::valid(invalid,error),"full-circle arc accepted");
        invalid.geometry=Sketch::Circle{{std::numeric_limits<double>::quiet_NaN(),0},1};
        check(!Sketch::valid(invalid,error),"non-finite coordinate accepted");
        invalid=entities.front();invalid.workplaneId="missing";
        check(!Sketch::validate({invalid},owners,{},error),"orphan entity accepted");
        check(!Sketch::validate({entities[0],entities[0]},owners,{},error),"duplicate ID accepted");
        NativeDocumentSnapshot snapshot;snapshot.constructions=owners;snapshot.sketchEntities=entities;
        QTemporaryDir dir;check(dir.isValid(),"temporary directory failed");
        const auto path=dir.filePath("sketch.odc");NativeDocumentSnapshot loaded;
        check(NativeDocumentStorage::save(path,snapshot,error),qPrintable(error));
        check(NativeDocumentStorage::load(path,loaded,error),qPrintable(error));
        check(loaded.schemaVersion==NativeDocumentStorage::CurrentSchemaVersion && loaded.sketchEntities==entities,"2D values, IDs, flags or order changed in roundtrip");
        for(int version:{1,2}) {
            NativeDocumentSnapshot legacy;legacy.schemaVersion=version;
            if(version==2) legacy.constructions=owners;
            const auto legacyPath=dir.filePath(QString("v%1.odc").arg(version));
            check(NativeDocumentStorage::save(legacyPath,legacy,error),qPrintable(error));
            check(NativeDocumentStorage::load(legacyPath,loaded,error) && loaded.schemaVersion==NativeDocumentStorage::CurrentSchemaVersion
                && loaded.sketchEntities.empty() && loaded.constructions.size()==(version==2?2:0),"legacy migration failed");
            legacy.sketchEntities=entities;
            check(!NativeDocumentStorage::save(legacyPath,legacy,error),"legacy writer silently discarded sketches");
        }
        check(NativeDocumentStorage::load(path,loaded,error),qPrintable(error));
        QFile original(path);check(original.open(QIODevice::ReadOnly),"cannot read saved bytes");const auto bytes=original.readAll();original.close();
        auto bad=snapshot;bad.sketchEntities[0].workplaneId="missing";
        check(!NativeDocumentStorage::save(path,bad,error),"invalid save accepted");
        check(original.open(QIODevice::ReadOnly) && original.readAll()==bytes,"failed save damaged file");original.close();
        // Malform real BinXCAF records to exercise the reader, not only validation of input values.
        for(int failure=0;failure<9;++failure) {
            const auto malformed=dir.filePath(QString("bad-%1.xbf").arg(failure));check(QFile::copy(path,malformed),"copy failed");
            auto app=XCAFApp_Application::GetApplication();Handle(TDocStd_Document) document;
            check(app->Open(TCollection_ExtendedString(malformed.toStdU16String().c_str()),document)==PCDM_RS_OK,"cannot reopen BinXCAF");
            TDF_LabelSequence roots;XCAFDoc_DocumentTool::ShapeTool(document->Main())->GetFreeShapes(roots);
            const auto root=roots.Value(1);TDF_Label group;
            for(TDF_ChildIterator it(root);it.More();it.Next()) {
                Handle(TDataStd_AsciiString) marker;
                if(it.Value().FindAttribute(TDataStd_AsciiString::GetID(),marker) && marker->Get().IsEqual(TCollection_AsciiString("OpenDesigner.Sketch.v1"))) group=it.Value();
            }
            check(!group.IsNull(),"sketch group missing");
            const auto plane=group.FindChild(1,Standard_False),record=plane.FindChild(1,Standard_False);
            if(failure==0) TDataStd_AsciiString::Set(root,TCollection_AsciiString("OpenDesigner.RootAssembly.v99"));
            if(failure==1) TDataStd_Integer::Set(root,99);
            if(failure==2) TDataStd_AsciiString::Set(plane,TCollection_AsciiString("missing"));
            if(failure==3) {Handle(TDataStd_IntegerArray) flags;record.FindAttribute(TDataStd_IntegerArray::GetID(),flags);flags->SetValue(0,99);}
            if(failure==4) {Handle(TDataStd_IntegerArray) flags;record.FindAttribute(TDataStd_IntegerArray::GetID(),flags);flags->SetValue(1,99);}
            if(failure==5) {Handle(TDataStd_IntegerArray) flags;record.FindAttribute(TDataStd_IntegerArray::GetID(),flags);flags->SetValue(3,2);}
            if(failure==6) TDataStd_AsciiString::Set(record,TCollection_AsciiString("wp1"));
            if(failure==7) group.ForgetAllAttributes(Standard_True);
            if(failure==8) {Handle(TDataStd_RealArray) values;record.FindAttribute(TDataStd_RealArray::GetID(),values);values->SetValue(2,values->Value(0));values->SetValue(3,values->Value(1));}
            TCollection_ExtendedString status;
            check(app->SaveAs(document,TCollection_ExtendedString(malformed.toStdU16String().c_str()),status)==PCDM_SS_OK,"cannot write malformed fixture");app->Close(document);
            check(!NativeDocumentStorage::load(malformed,loaded,error) && !error.isEmpty() && loaded.sketchEntities==entities,"malformed Open replaced existing snapshot");
        }
        std::cout<<"Sketch geometry and native storage checks passed.\n";return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
