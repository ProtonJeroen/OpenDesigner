#include "NativeDocument.h"
#include "SketchSnapping.h"
#include "SketchOverdraw.h"
#include "SketchConstruction.h"
#include "SketchProjection.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <BRepAdaptor_Curve.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <BRep_Tool.hxx>
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
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>

using namespace Sketch;
namespace {
constexpr double pi=std::numbers::pi;
void check(bool ok,const char* message) {if(!ok) throw std::runtime_error(message);}
bool near(Point a,Point b,double tolerance=1.e-10) {return std::hypot(a.x-b.x,a.y-b.y)<tolerance;}
Entity entity(Geometry geometry,Role role=Role::Construction) {
    Entity e;e.workplaneId="wp";e.geometry=geometry;e.role=role;return e;
}
Construction::Object plane() {
    Construction::Object wp;wp.id="wp";wp.name="WP1";wp.kind=Construction::Kind::Workplane;
    wp.frame=gp_Ax3(gp_Pnt(10,20,30),gp_Dir(1,2,3),gp_Dir(2,-1,0));
    wp.displayOffset=gp_Vec(90,-80,0);return wp;
}
QByteArray bytes(const QString& path) {QFile f(path);check(f.open(QIODevice::ReadOnly),"cannot read saved file");return f.readAll();}
void inspect(const QString& path,const std::function<void(const TDF_Label&)>& operation,const QString& output={}) {
    QTemporaryDir temp;const auto copy=temp.filePath("inspect.xbf");check(QFile::copy(path,copy),"copy for inspection failed");
    const auto app=XCAFApp_Application::GetApplication();Handle(TDocStd_Document) doc;
    check(app->Open(TCollection_ExtendedString(copy.toStdU16String().c_str()),doc)==PCDM_RS_OK,"cannot inspect native document");
    TDF_LabelSequence roots;XCAFDoc_DocumentTool::ShapeTool(doc->Main())->GetFreeShapes(roots);
    check(roots.Length()==1,"invalid root count");operation(roots.Value(1));
    if(!output.isEmpty()) {TCollection_ExtendedString status;check(app->SaveAs(doc,TCollection_ExtendedString(output.toStdU16String().c_str()),status)==PCDM_SS_OK,"cannot write malformed fixture");}
    app->Close(doc);
}
TDF_Label firstRecord(const TDF_Label& root) {
    for(TDF_ChildIterator it(root);it.More();it.Next()) {
        Handle(TDataStd_AsciiString) marker;
        if(it.Value().FindAttribute(TDataStd_AsciiString::GetID(),marker) && marker->Get().IsEqual(TCollection_AsciiString("OpenDesigner.Sketch.v1")))
            return it.Value().FindChild(1,Standard_False).FindChild(1,Standard_False);
    }
    throw std::runtime_error("missing sketch record");
}
void schema(const QString& path,int version) {
    inspect(path,[&](const TDF_Label& root) {
        Handle(TDataStd_AsciiString) marker;check(root.FindAttribute(TDataStd_AsciiString::GetID(),marker),"missing root marker");
        check(marker->Get().IsEqual(TCollection_AsciiString(QString("OpenDesigner.RootAssembly.v%1").arg(version).toUtf8().constData())),"wrong disk schema marker");
        if(version>=2) {Handle(TDataStd_Integer) v;check(root.FindAttribute(TDataStd_Integer::GetID(),v) && v->Get()==version,"wrong disk schema integer");}
    });
}
void geometry() {
    QString error;const Ellipse ellipse{{12.345678901234567,-8.987654321012345},17.123456789,4.7654321,0.37};
    check(entity(Segment{}).geometry.index()==0 && entity(InfiniteLine{}).geometry.index()==1
        && entity(Circle{}).geometry.index()==2 && entity(Arc{}).geometry.index()==3
        && entity(ConstructionPoint{}).geometry.index()==4 && entity(ellipse).geometry.index()==5
        && entity(EllipticalArc{}).geometry.index()==6,"existing geometry tags shifted");
    check(typeName(ellipse)=="Ellipse" && typeName(EllipticalArc{})=="Elliptical Arc","incorrect conic labels");
    for(auto role:{Role::Construction,Role::Profile}) check(valid(entity(ellipse,role),error),"ellipse role rejected");
    auto same=ellipse;same.minorRadius=same.majorRadius;check(valid(entity(same),error),"circular-limit ellipse rejected");
    for(double t:{0.0,0.3,pi/2,pi,5.9,2*pi+0.7}) {
        const auto p=ellipsePoint(ellipse,t);const auto parameter=ellipseParameter(ellipse,p);
        check(parameter && near(ellipsePoint(ellipse,*parameter),p),"ellipse inverse parameter is polar angle or lost exact position");
        const auto derivative=ellipseDerivative(ellipse,t);const double h=1.e-5;
        const auto before=ellipsePoint(ellipse,t-h),after=ellipsePoint(ellipse,t+h);
        check(std::hypot((after.x-before.x)/(2*h)-derivative.x,(after.y-before.y)/(2*h)-derivative.y)<1.e-7,"ellipse derivative incorrect");
    }
    check(!ellipseParameter(ellipse,ellipse.center),"ellipse center has a fabricated unique parameter");
    for(double sweep:{0.8,-0.8,4.7,-4.7,2*pi-1.e-8}) {
        const EllipticalArc arc{ellipse,6.0,sweep};const auto endpoints=ellipticalArcEndpoints(arc);
        check(valid(entity(arc),error),"valid signed elliptical arc rejected");
        check(ellipticalArcContainsParameter(arc,arc.startParameter)
            && ellipticalArcContainsParameter(arc,arc.startParameter+sweep)
            && ellipticalArcContainsParameter(arc,arc.startParameter+sweep/2),"seam/major/minor signed domain incorrect");
        check(!ellipticalArcContainsParameter(arc,arc.startParameter+sweep+(2*pi-std::abs(sweep))*(sweep>0?0.5:-0.5),0),"complementary route entered arc domain");
        const auto reversed=reversedEllipticalArc(arc),twice=reversedEllipticalArc(reversed);
        const auto reverseEndpoints=ellipticalArcEndpoints(reversed);
        check(near(endpoints[0],reverseEndpoints[1]) && near(endpoints[1],reverseEndpoints[0])
            && near(ellipsePoint(twice.ellipse,twice.startParameter),endpoints[0]) && twice.sweepParameter==sweep,"arc reversal changed route");
        const auto wp=plane();const auto shape=Sketch::shape(entity(arc),wp.frame);
        const auto edge=TopoDS::Edge(shape);BRepAdaptor_Curve curve(edge);
        check(BRepCheck_Analyzer(edge).IsValid(),"elliptical arc presentation edge is invalid B-Rep");
        check(curve.GetType()==GeomAbs_Ellipse,"elliptical arc became a spline/circle");
        const auto startError=BRep_Tool::Pnt(TopExp::FirstVertex(edge,Standard_True)).Distance(worldPoint(endpoints[0],wp.frame));
        const auto endError=BRep_Tool::Pnt(TopExp::LastVertex(edge,Standard_True)).Distance(worldPoint(endpoints[1],wp.frame));
        if(startError>=1.e-8 || endError>=1.e-8) throw std::runtime_error(QString("Exact edge endpoint errors %1 / %2 for sweep %3").arg(startError,0,'g',12).arg(endError,0,'g',12).arg(sweep,0,'g',16).toStdString());
    }
    auto wp=plane();const auto conic=entity(ellipse);const auto shape=Sketch::shape(conic,wp.frame);
    BRepAdaptor_Curve curve(TopoDS::Edge(shape));check(curve.GetType()==GeomAbs_Ellipse,"ellipse lost analytic type");
    for(double t:{0.0,0.3,2.7,5.4}) check(curve.Value(t).IsEqual(worldPoint(ellipsePoint(ellipse,t),wp.frame),1.e-10),"rotated Workplane conic position incorrect");
    gp_Trsf move;move.SetRotation(gp_Ax1(gp_Pnt(),gp_Dir(1,2,3)),0.8);move.SetTranslationPart(gp_Vec(100,-50,20));
    auto frame=wp.frame;frame.Transform(move);BRepAdaptor_Curve transformed(TopoDS::Edge(Sketch::shape(conic,frame)));
    check(transformed.Value(1.2).IsEqual(curve.Value(1.2).Transformed(move),1.e-10),"Workplane transform changed conic geometry");
    for(int invalid=0;invalid<8;++invalid) {
        auto bad=ellipse;
        if(invalid==0) bad.minorRadius=0;if(invalid==1) bad.minorRadius=-1;
        if(invalid==2) bad.minorRadius=bad.majorRadius+1;if(invalid==3) bad.majorRadius=std::numeric_limits<double>::infinity();
        if(invalid==4) bad.axisAngle=std::numeric_limits<double>::quiet_NaN();if(invalid==5) bad.center.x=std::numeric_limits<double>::infinity();
        if(invalid==6) bad.minorRadius=1.e-10;if(invalid==7) bad.minorRadius=std::numeric_limits<double>::quiet_NaN();
        check(!valid(entity(bad),error) && !valid(entity(EllipticalArc{bad,0,1}),error),"invalid ellipse accepted");
    }
    for(double sweep:{0.0,1.e-13,2*pi,-2*pi,std::numeric_limits<double>::infinity()})
        check(!valid(entity(EllipticalArc{ellipse,0,sweep}),error),"invalid elliptical sweep accepted");
    check(!valid(entity(EllipticalArc{ellipse,1.e300,1}),error),"unrepresentable end parameter accepted");
    auto orphan=conic;orphan.workplaneId="missing";
    check(!validate({orphan},{wp},{},error) && !validate({conic,conic},{wp},{},error),"orphan or duplicate stable ID accepted");
}
void safeTools() {
    QString error;const Ellipse support{{},10,5,0.2};const auto ellipse=entity(support),arc=entity(EllipticalArc{support,5.8,-2});
    for(const auto& e:{ellipse,arc}) {
        check(isElliptical(e.geometry) && !supportedSketchToolCurve(e.geometry),"ellipse mislabeled as supported circle tool source");
        check(!contains(e.geometry,{0,0}) && nearestPoint(e.geometry,{1,1}) && tangentPoints({20,20},e.geometry).empty()
            && intersect(e.geometry,Circle{{},5}).kind==IntersectionKind::Unsupported && !snapTargets({e},e.workplaneId,{0,0}).empty(),"ellipse snap support or explicit conic/tangent limits incorrect");
        Geometry offset=Segment{};const auto old=offset;std::vector<CommonTangent> tangents;
        check(!offsetConstructionLine(e.geometry,2,LineOutput::Finite,{},offset,error) && offset==old,"ellipse became an offset line");
        check(!commonTangents(e.geometry,Circle{{},3},tangents,error),"ellipse became circular tangent source");
        OverdrawGraph graph;
        if(std::holds_alternative<Ellipse>(e.geometry)) check(!overdrawCandidates({e},e.workplaneId,graph,error) && error.contains("two distinct"),"uncut ellipse needs certified boundaries");
        else check(overdrawCandidates({e},e.workplaneId,graph,error) && graph.pieces.size()==1 && graph.pieces[0].geometry==e.geometry,"bounded elliptic Overdraw changed exact values");
        auto line=entity(Segment{{-20,-10},{20,-10}});
        check(overdrawCandidates({e,line},e.workplaneId,graph,error) && graph.pieces.size()==(std::holds_alternative<Ellipse>(e.geometry)?1:2),"ellipse broke existing Overdraw line workflow");
        const SketchProjection::Source vertex{BRepBuilderAPI_MakeVertex(gp_Pnt(1,2,3)).Shape(),{},"vertex"};
        const auto result=SketchProjection::project({vertex},plane(),{e});
        check(result.success && result.additions.size()==1,"existing ellipse incorrectly blocked independent point projection");
        auto profile=e;profile.role=Role::Profile;check(SketchProjection::project({vertex},plane(),{profile}).success,"Profile ellipse blocked Construction projection");
        check(SketchProjection::project({},plane(),{e}).success,"empty projection request changed into an error");
    }
}
void storage() {
    QString error;QTemporaryDir temp;check(temp.isValid(),"temporary directory failed");
    NativeDocumentSnapshot document;document.constructions={plane()};
    const Ellipse ellipse{{12.345678901234567,-8.987654321012345},17.123456789,4.7654321,-0.37};
    auto a=entity(ellipse);a.name=QString::fromUtf8("Ellipse α");a.visible=false;
    auto b=entity(EllipticalArc{ellipse,5.9,-4.3},Role::Profile);b.name="Profile elliptical arc";b.selectable=false;
    document.sketchEntities={a,b,entity(ConstructionPoint{{3,4}}),entity(Segment{{-3,0},{3,0}}),entity(InfiniteLine{}),entity(Circle{}),entity(Arc{})};
    auto other=plane();other.id="other-wp";other.frame.SetLocation(gp_Pnt(100,0,0));document.constructions.push_back(other);
    auto extra=entity(EllipticalArc{ellipse,-1.2,1.7});extra.workplaneId=other.id;document.sketchEntities.push_back(extra);
    const auto path=temp.filePath("ellipses.odc");NativeDocumentSnapshot loaded;
    check(NativeDocumentStorage::save(path,document,error),qPrintable(error));schema(path,5);
    check(NativeDocumentStorage::load(path,loaded,error) && loaded.schemaVersion==NativeDocumentStorage::CurrentSchemaVersion && loaded.sketchEntities==document.sketchEntities,"v5 conic values/IDs/properties/owners changed");
    auto mixed=document;NativePartState part;part.id="part-a";part.name="Box with conics";
    part.shape=BRepPrimAPI_MakeBox(10,20,30).Shape();part.color=QColor(40,80,160);part.visible=false;
    part.transform.SetRotation(gp_Ax1(gp_Pnt(),gp_Dir(1,2,3)),0.7);part.transform.SetTranslationPart(gp_Vec(30,-20,80));
    mixed.parts.push_back(part);part.id="part-b";part.name="Other box";part.color=QColor(160,80,40);part.visible=true;mixed.parts.push_back(part);
    mixed.camera.eye=gp_Pnt(300,400,500);mixed.camera.center=gp_Pnt(1,2,3);mixed.camera.scale=2.75;
    const auto mixedPath=temp.filePath("mixed-v5.odc");
    check(NativeDocumentStorage::save(mixedPath,mixed,error) && NativeDocumentStorage::load(mixedPath,loaded,error)
        && loaded.parts.size()==2 && loaded.sketchEntities==document.sketchEntities,"mixed Part/conic document lost values");schema(mixedPath,5);
    for(size_t i=0;i<mixed.parts.size();++i) {
        const auto& expected=mixed.parts[i];const auto& actual=loaded.parts[i];GProp_GProps properties;BRepGProp::VolumeProperties(actual.shape,properties);
        check(actual.id==expected.id && actual.name==expected.name && actual.color==expected.color && actual.visible==expected.visible
            && std::abs(properties.Mass()-6000)<1.e-7,"v5 lost Part identity/properties/exact B-Rep volume");
        for(int r=1;r<=3;++r) for(int c=1;c<=4;++c) check(std::abs(actual.transform.Value(r,c)-expected.transform.Value(r,c))<1.e-12,"v5 changed Part placement");
    }
    check(loaded.camera.eye.IsEqual(mixed.camera.eye,1.e-12) && loaded.camera.center.IsEqual(mixed.camera.center,1.e-12)
        && loaded.camera.up.IsEqual(mixed.camera.up,1.e-12) && loaded.camera.scale==mixed.camera.scale,"v5 changed camera");
    inspect(path,[&](const TDF_Label& root) {
        const auto planeLabel=firstRecord(root).Father();
        for(int i=1;i<=7;++i) {
            const auto record=planeLabel.FindChild(i,Standard_False);Handle(TDataStd_IntegerArray) flags;Handle(TDataStd_RealArray) values;
            check(record.FindAttribute(TDataStd_IntegerArray::GetID(),flags) && record.FindAttribute(TDataStd_RealArray::GetID(),values),"missing sketch metadata");
            const int tags[]={5,6,4,0,1,2,3},counts[]={5,7,2,4,4,3,5};
            check(flags->Value(0)==tags[i-1] && values->Length()==counts[i-1],"persisted tags/counts changed");
        }
    });
    const auto original=bytes(path);
    for(int version:{1,2,3,4}) {
        auto bad=document;bad.schemaVersion=version;
        check(!NativeDocumentStorage::save(path,bad,error) && bytes(path)==original,"legacy request silently lost ellipse or damaged file");
        NativeDocumentSnapshot legacy;legacy.schemaVersion=version;
        if(version>=2) legacy.constructions={plane()};
        if(version>=3) legacy.sketchEntities={entity(Circle{}),entity(Arc{})};
        if(version>=4) legacy.sketchEntities.push_back(entity(ConstructionPoint{{1,2}}));
        const auto file=temp.filePath(QString("legacy-v%1.odc").arg(version));
        check(NativeDocumentStorage::save(file,legacy,error),qPrintable(error));schema(file,version);
        check(NativeDocumentStorage::load(file,loaded,error) && loaded.schemaVersion==NativeDocumentStorage::CurrentSchemaVersion && loaded.sketchEntities==legacy.sketchEntities
            && loaded.constructions.size()==legacy.constructions.size(),"v1-v4 compatibility lost values");
    }
    auto olderContent=document;olderContent.sketchEntities={entity(ConstructionPoint{{1,2}})};
    const auto autoPath=temp.filePath("auto.odc");check(NativeDocumentStorage::save(autoPath,olderContent,error),qPrintable(error));schema(autoPath,4);
    olderContent.sketchEntities={entity(Circle{})};check(NativeDocumentStorage::save(autoPath,olderContent,error),qPrintable(error));schema(autoPath,3);
    olderContent.sketchEntities.clear();check(NativeDocumentStorage::save(autoPath,olderContent,error),qPrintable(error));schema(autoPath,3);
    NativeDocumentSnapshot empty;check(NativeDocumentStorage::save(autoPath,empty,error),qPrintable(error));schema(autoPath,3);
    check(NativeDocumentStorage::load(autoPath,loaded,error) && loaded.sketchEntities.empty(),"empty document failed");
    check(NativeDocumentStorage::load(path,loaded,error),qPrintable(error));const auto retained=loaded.sketchEntities;
    for(int failure=0;failure<12;++failure) {
        const auto malformed=temp.filePath(QString("malformed-%1.xbf").arg(failure));
        inspect(path,[&](const TDF_Label& root) {
            const auto record=firstRecord(root);Handle(TDataStd_RealArray) values;Handle(TDataStd_IntegerArray) flags;
            check(record.FindAttribute(TDataStd_RealArray::GetID(),values) && record.FindAttribute(TDataStd_IntegerArray::GetID(),flags),"invalid fixture record");
            if(failure==0) {const int future=NativeDocumentStorage::CurrentSchemaVersion+1;TDataStd_AsciiString::Set(root,TCollection_AsciiString(QString("OpenDesigner.RootAssembly.v%1").arg(future).toUtf8().constData()));TDataStd_Integer::Set(root,future);}
            if(failure==1) TDataStd_Integer::Set(root,4);
            if(failure==2 || failure==3) {const int v=failure==2?3:4;TDataStd_AsciiString::Set(root,TCollection_AsciiString(QString("OpenDesigner.RootAssembly.v%1").arg(v).toUtf8().constData()));TDataStd_Integer::Set(root,v);}
            if(failure==4) flags->SetValue(0,7);
            if(failure==5) TDataStd_RealArray::Set(record,0,3);
            if(failure==6) values->SetValue(3,0);
            if(failure==7) values->SetValue(2,values->Value(3)/2);
            if(failure==8) values->SetValue(4,std::numeric_limits<double>::quiet_NaN());
            if(failure==9) TDataStd_AsciiString::Set(record,TCollection_AsciiString("wp"));
            if(failure==10) TDataStd_AsciiString::Set(record.Father(),TCollection_AsciiString("missing"));
            if(failure==11) {Handle(TDataStd_RealArray) arc;check(record.Father().FindChild(2,Standard_False).FindAttribute(TDataStd_RealArray::GetID(),arc),"missing arc record");arc->SetValue(6,2*pi);}
        },malformed);
        check(!NativeDocumentStorage::load(malformed,loaded,error) && !error.isEmpty() && loaded.sketchEntities==retained,"malformed/future record replaced current snapshot");
    }
    auto bad=document;bad.sketchEntities[0].geometry=Ellipse{{},1,-1,0};
    check(!NativeDocumentStorage::save(path,bad,error) && bytes(path)==original,"invalid ellipse damaged existing save");
}
}
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    try {geometry();safeTools();storage();std::cout<<"Exact ellipse foundation, safe consumers and v1-v5 storage passed.\n";return 0;}
    catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
