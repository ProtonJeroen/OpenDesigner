#include "CadViewport.h"
#include "DeleteFaceGeometry.h"
#include <sstream>
#include <BRep_Tool.hxx>
#include <QTimer>
#include <QMessageBox>
#include <BRepTools.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepFilletAPI_MakeChamfer.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include "MoveFaceGeometry.h"
#include "FilletGeometry.h"
#include <QApplication>
#include <QCheckBox>
#include <QMenu>
#include <QAction>
#include <QMouseEvent>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <StdSelect_BRepOwner.hxx>
#include <QJsonDocument>
#include <QLineEdit>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QTemporaryDir>
#include <QKeyEvent>
#include <QLocale>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <stdexcept>
#include <algorithm>
#include <iostream>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <cmath>
namespace {
void check(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
double volume(const TopoDS_Shape& shape) {GProp_GProps p;BRepGProp::VolumeProperties(shape,p);return p.Mass();}
}
struct EdgeBlendSessionTest {
static std::string geometryStamp(const TopoDS_Shape& shape) {
    std::ostringstream stream;BRepTools::Write(shape,stream,false,false,TopTools_FormatVersion_VERSION_3);return stream.str();
}
static void strictTolerances(const TopoDS_Shape& shape) {
    for(auto type:{TopAbs_FACE,TopAbs_EDGE,TopAbs_VERTEX})for(TopExp_Explorer it(shape,type);it.More();it.Next()) {
        const double tolerance=type==TopAbs_FACE?BRep_Tool::Tolerance(TopoDS::Face(it.Current())):
            type==TopAbs_EDGE?BRep_Tool::Tolerance(TopoDS::Edge(it.Current())):BRep_Tool::Tolerance(TopoDS::Vertex(it.Current()));
        check(std::isfinite(tolerance)&&tolerance<=1.e-6,"reconstruction exceeded certified tolerance");
    }
}
static TopoDS_Face cap(const TopoDS_Shape& shape,bool top) {
    TopoDS_Face result;double height=top?-1.e30:1.e30;
    for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next()) {
        auto f=TopoDS::Face(it.Current());BRepAdaptor_Surface surface(f);
        if(surface.GetType()!=GeomAbs_Plane)continue;
        double z=surface.Plane().Location().Z();
        if(top?z>height:z<height) {result=f;height=z;}
    }
    check(!result.IsNull(),"missing cap");return result;
}
static NativeDocumentSnapshot recoveryFixture() {
    NativeDocumentSnapshot doc;NativePartState cylinder,cone;QString error;
    cylinder.id="recovery-cylinder";cylinder.name="Cylinder 2";
    auto sharp=BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(125,0,0),gp_Dir(0,0,1)),40,80).Shape();
    TopExp_Explorer edge(cap(sharp,true),TopAbs_EDGE);EdgeBlendFeatures::State state;
    check(EdgeBlendFeatures::create(sharp,{},TopoDS::Edge(edge.Current()),EdgeBlendFeatures::Kind::Fillet,20,state,cylinder.shape,error),qPrintable(error));
    cylinder.edgeBlendRecipe=EdgeBlendFeatures::encode(state);
    cone.id="recovery-cone";cone.name="Cone 3";
    auto base=BRepPrimAPI_MakeCone(gp_Ax2(gp_Pnt(250,0,0),gp_Dir(0,0,1)),40,20,80).Shape();
    BRepFilletAPI_MakeChamfer chamfer(base);TopExp_Explorer top(cap(base,true),TopAbs_EDGE);
    chamfer.Add(8,TopoDS::Edge(top.Current()));chamfer.Build();check(chamfer.IsDone(),"cone Chamfer fixture failed");
    BRepFilletAPI_MakeFillet fillet(chamfer.Shape());TopExp_Explorer bottom(cap(chamfer.Shape(),false),TopAbs_EDGE);
    fillet.Add(11,TopoDS::Edge(bottom.Current()));fillet.Build();check(fillet.IsDone(),"cone Fillet fixture failed");
    cone.shape=fillet.Shape();check(BRepCheck_Analyzer(cone.shape).IsValid(),"invalid cone fixture");
    doc.parts={cylinder,cone};return doc;
}
static void choose(CadViewport& v,size_t index,const TopoDS_Face& face) {
    v.selectParts({index});
    check(v.selectionContext().kind==CadViewport::SelectionContext::Kind::Parts,"Part selection lost");
    v.context_->ClearSelected(false);v.selectedPartIndices_.clear();
    Handle(StdSelect_BRepOwner) owner=new StdSelect_BRepOwner(face,v.boxes_[index].presentation,5,true);
    v.context_->AddOrRemoveSelected(owner,false);
    auto selection=v.selectionContext();
    check(selection.kind==CadViewport::SelectionContext::Kind::Faces&&selection.faces.size()==1
        &&selection.faces.front().face.IsSame(face)&&selection.faces.front().partIndex==index,"Face after Part has wrong owner or stale topology");
}
static void pickActual(CadViewport& v,size_t index,const TopoDS_Face& face) {
    v.selectParts({index});
    BRepAdaptor_Surface carrier(face);gp_Pnt anchor;gp_Vec du,dw;
    double ua,ub,wa,wb;BRepTools::UVBounds(face,ua,ub,wa,wb);
    carrier.D1(ua+(ub-ua)*0.75,(wa+wb)/2,anchor,du,dw);
    auto normal=du.Crossed(dw);if(face.Orientation()==TopAbs_REVERSED)normal.Reverse();normal.Transform(v.boxes_[index].orientation);
    v.view_->SetProj(normal.X(),normal.Y(),normal.Z());v.fitAll();QApplication::processEvents();
    double u0,u1,w0,w1;BRepTools::UVBounds(face,u0,u1,w0,w1);BRepAdaptor_Surface surface(face);
    for(int u=1;u<8;++u)for(int w=1;w<8;++w) {
        auto p=surface.Value(u0+(u1-u0)*u/8,w0+(w1-w0)*w/8).Transformed(v.boxes_[index].orientation);
        Standard_Integer x,y;v.view_->Convert(p.X(),p.Y(),p.Z(),x,y);
        v.context_->MoveTo(x,y,v.view_,false);
        if(!v.context_->HasDetected())continue;
        auto owner=Handle(StdSelect_BRepOwner)::DownCast(v.context_->DetectedOwner());
        if(owner.IsNull()||!owner->Shape().IsSame(face))continue;
        QMouseEvent press(QEvent::MouseButtonPress,QPointF(x,y),QPointF(x,y),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
        QMouseEvent release(QEvent::MouseButtonRelease,QPointF(x,y),QPointF(x,y),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
        v.mousePressEvent(&press);v.mouseReleaseEvent(&release);
        auto selected=v.selectionContext();
        check(selected.kind==CadViewport::SelectionContext::Kind::Faces&&selected.faces.size()==1&&selected.faces[0].face.IsSame(face),"actual Face pick after Part failed");return;
    }
    throw std::runtime_error("no visible sample for actual Face picking: "+v.boxes_[index].name.toStdString());
}
// documentPath empty: in-memory synthetic fixture. Otherwise the fixture is
// read from that .odc (a synthetic round-trip file or a temporary private copy).
static void recovery(const QString& documentPath={}) {
    auto generated=recoveryFixture(),doc=generated;QString error;
    if(!documentPath.isEmpty()) {doc={};check(NativeDocumentStorage::load(documentPath,doc,error),qPrintable(error));}
    CadViewport v;v.resize(1000,800);v.show();QApplication::processEvents();
    QString status;QObject::connect(&v,&CadViewport::statusMessage,[&](const QString& text){status=text;});
    auto load=[&]{check(v.importDocumentSnapshot(doc,error),qPrintable(error));};load();
    size_t cylinder=doc.parts.size(),cone=doc.parts.size();
    for(size_t i=0;i<doc.parts.size();++i) {
        if(doc.parts[i].name=="Cylinder 2")cylinder=i;
        if(doc.parts[i].name=="Cone 3")cone=i;
    }
    check(cylinder<doc.parts.size()&&cone<doc.parts.size(),"missing reported Parts");
    check(BRepReconstruction::equivalent(doc.parts[cylinder].shape,generated.parts[0].shape)
        &&BRepReconstruction::equivalent(doc.parts[cone].shape,generated.parts[1].shape),"actual fixture differs from deterministic reconstruction");
    check(v.boxes_[cylinder].blends.features.size()==1&&std::abs(v.boxes_[cylinder].blends.features[0].size-20)<1.e-10,"Cylinder recipe/radius lost on Open");
    pickActual(v,cylinder,cap(v.boxes_[cylinder].shape,true));
    for(TopExp_Explorer it(v.boxes_[cylinder].shape,TopAbs_FACE);it.More();it.Next()) {
        auto face=TopoDS::Face(it.Current());if(BRepAdaptor_Surface(face).GetType()!=GeomAbs_Cylinder)continue;
        choose(v,cylinder,face);v.startPrimitivePull();check(!v.commandSession_&&status.contains("not certified"),"circular wall Pull refusal missing");
        choose(v,cylinder,face);v.moveFace();check(!v.commandSession_&&status.contains("not yet certified"),"circular wall Move refusal missing");v.resetModelingTools();
    }
    // Part commands never infer an arbitrary Face.
    v.selectParts({cylinder});v.startPrimitivePull();check(!v.commandSession_&&!v.pullMode_,"Part Pull chose an arbitrary Face");
    v.moveFace();check(!v.commandSession_&&v.moveFacePicking_,"Part Move Face chose an arbitrary Face");v.resetModelingTools();
    // Both caps, both commands and both Keep policies, including the unaffected cap.
    for(bool top:{false,true})for(bool pull:{false,true})for(bool keep:{false,true}) {
        load();auto before=v.boxes_[cylinder];const auto stamp=geometryStamp(before.shape);auto face=cap(before.shape,top);choose(v,cylinder,face);
        if(pull)v.startPrimitivePull();else v.moveFace();
        check(v.commandSession_&&v.commandSession_->face.IsSame(face),"cap command lost selected Face");
        v.keepBlends_->setChecked(keep);v.dragDimension_->setText("1");
        check(v.updateCommandParameter(false),qPrintable(v.commandSession_->geometryError));
        auto candidate=v.commandSession_->changes.front().faceChange->after;
        check(BRepCheck_Analyzer(candidate.shape).IsValid(),"cap produced invalid B-Rep");
        strictTolerances(candidate.shape);strictTolerances(candidate.blends.sharp);
        const size_t expected=(!keep&&top)?0:1;
        check(candidate.blends.features.size()==expected,"Keep removed unrelated or retained affected Fillet");
        if(expected)check(std::abs(candidate.blends.features[0].size-20)<1.e-10,"Keep changed Fillet radius");
        else {auto primitive=candidate;check(v.recognizePullPrimitive(primitive)&&primitive.cylinderPrimitive,"Keep OFF left a split sharp cylinder unusable for primitive Pull");}
        auto expectedState=before.blends;auto sharpCap=cap(expectedState.sharp,top);
        auto sharp=MoveFaceGeometry::move(expectedState.sharp,sharpCap,1,error);check(!sharp.IsNull(),qPrintable(error));
        if(expected) {
            TopExp_Explorer rim(cap(sharp,true),TopAbs_EDGE);
            // The independent oracle normalizes the sharp bands before a kernel Fillet.
            TopoDS_Shape normalized;check(BRepReconstruction::normalize(sharp,normalized,error),qPrintable(error));
            TopExp_Explorer normalizedRim(cap(normalized,true),TopAbs_EDGE);
            sharp=FilletGeometry::apply(normalized,TopoDS::Edge(normalizedRim.Current()),20,error);check(!sharp.IsNull(),qPrintable(error));
        }
        check(BRepReconstruction::equivalent(candidate.shape,sharp),"cap result differs from independently reconstructed solid");
        v.cancelCommandSession();check(v.boxes_[cylinder].shape.IsSame(before.shape)&&geometryStamp(before.shape)==stamp&&v.undoStack_.empty(),"Cancel changed cap/history");
        choose(v,cylinder,face);if(pull)v.startPrimitivePull();else v.moveFace();v.keepBlends_->setChecked(keep);
        v.dragDimension_->setText("1");v.confirmCommandSession();
        check(v.undoStack_.size()==1&&BRepReconstruction::equivalent(v.boxes_[cylinder].shape,sharp),"cap confirmation lost result");
        v.undoPull();check(v.boxes_[cylinder].shape.IsSame(before.shape),"cap Undo failed");v.redoPull();
        check(BRepReconstruction::equivalent(v.boxes_[cylinder].shape,sharp),"cap Redo failed");
    }
    NativeDocumentSnapshot edited,reopened;QTemporaryDir temporary;
    check(v.exportDocumentSnapshot(edited)&&NativeDocumentStorage::save(temporary.filePath("recovery.odc"),edited,error)
        &&NativeDocumentStorage::load(temporary.filePath("recovery.odc"),reopened,error)&&v.importDocumentSnapshot(reopened,error),"recovered cap Save/Open failed");
    auto staleFace=cap(doc.parts[cylinder].shape,true);
    v.context_->ClearSelected(false);v.selectedPartIndices_.clear();
    Handle(StdSelect_BRepOwner) stale=new StdSelect_BRepOwner(staleFace,v.boxes_[cylinder].presentation,5,true);
    v.context_->AddOrRemoveSelected(stale,false);check(v.selectionContext().kind==CadViewport::SelectionContext::Kind::None,"stale topology became a command target");v.selectParts({});
    check(v.boxes_[cylinder].blends.features.size()==1&&v.startEditEdgeBlend(cylinder,v.boxes_[cylinder].blends.features[0].faces.front()),"recovered cap lost editable recipe on Open");v.cancelCommandSession();
    // Two separated circular transitions: Keep OFF removes only the moved rim.
    auto circular=EdgeBlendFeatures::decode(generated.parts[0].edgeBlendRecipe);auto current=generated.parts[0].shape;
    check(EdgeBlendFeatures::resolve(current,circular,error),qPrintable(error));
    TopExp_Explorer lower(cap(current,false),TopAbs_EDGE);EdgeBlendFeatures::State two;TopoDS_Shape twoShape;
    check(EdgeBlendFeatures::create(current,circular,TopoDS::Edge(lower.Current()),EdgeBlendFeatures::Kind::Fillet,10,two,twoShape,error),qPrintable(error));
    for(bool keep:{false,true}) {
        EdgeBlendFeatures::State after;TopoDS_Shape result;
        check(EdgeBlendFeatures::move(twoShape,two,cap(twoShape,true),1,keep,after,result,error),qPrintable(error));
        check(after.features.size()==(keep?2:1)&&BRepCheck_Analyzer(result).IsValid(),"two-rim cap edit lost separated Fillet");
        check(std::any_of(after.features.begin(),after.features.end(),[](const auto& f){return std::abs(f.size-10)<1.e-10;}),"unaffected lower radius changed");strictTolerances(result);
    }
    load();auto before=v.boxes_[cylinder];auto transition=before.blends.features[0].faces.front();
    check(v.startEditEdgeBlend(cylinder,transition),"Cylinder Edit Fillet failed");v.dragDimension_->setText("19");check(v.updateCommandParameter(false),"Cylinder radius edit failed");
    check(BRepCheck_Analyzer(v.commandSession_->candidate).IsValid()&&std::abs(volume(v.commandSession_->candidate)-volume(before.shape))>1,"Cylinder Edit has no valid geometric effect");v.cancelCommandSession();
    choose(v,cylinder,transition);v.deleteFace();check(v.commandSession_&&v.commandSession_->valid,"Cylinder Delete Fillet failed");v.confirmCommandSession();
    check(BRepReconstruction::equivalent(v.boxes_[cylinder].shape,before.blends.sharp)&&v.boxes_[cylinder].blends.features.empty(),"Cylinder Delete did not restore sharp cylinder");
    v.undoPull();check(v.boxes_[cylinder].shape.IsSame(before.shape),"Cylinder Delete Undo failed");v.redoPull();check(BRepCheck_Analyzer(v.boxes_[cylinder].shape).IsValid(),"Delete Redo invalid");
    load();before=v.boxes_[cylinder];choose(v,cylinder,cap(before.shape,true));v.moveFace();v.dragDimension_->setText("-79");
    check(!v.updateCommandParameter(false)&&!v.commandSession_->geometryError.isEmpty()&&v.commandSession_->candidate.IsNull(),"oversized Fillet accepted after cap collapse");
    QTimer::singleShot(0,[]{for(auto* w:QApplication::topLevelWidgets())if(auto* box=qobject_cast<QMessageBox*>(w))box->accept();});
    v.confirmCommandSession();check(v.boxes_[cylinder].shape.IsSame(before.shape)&&v.undoStack_.empty(),"failed cap edit mutated source/history");v.cancelCommandSession();
    // v0.5.2: recipe-free Cone 3 is certified from geometry alone: an 8 mm
    // conical Chamfer and an 11 mm toroidal Fillet on a R40->R20 frustum.
    // Independent oracle: kernel blends on an explicitly built frustum.
    const double slope=20./80.;
    auto coneOracle=[&](double z0,double z1,double chamferSize,double filletSize) {
        TopoDS_Shape shape=BRepPrimAPI_MakeCone(gp_Ax2(gp_Pnt(250,0,z0),gp_Dir(0,0,1)),40-z0*slope,40-z1*slope,z1-z0).Shape();
        if(chamferSize>0) {BRepFilletAPI_MakeChamfer c(shape);TopExp_Explorer e(cap(shape,true),TopAbs_EDGE);c.Add(chamferSize,TopoDS::Edge(e.Current()));c.Build();check(c.IsDone(),"oracle Chamfer");shape=c.Shape();}
        if(filletSize>0) {BRepFilletAPI_MakeFillet f(shape);TopExp_Explorer e(cap(shape,false),TopAbs_EDGE);f.Add(filletSize,TopoDS::Edge(e.Current()));f.Build();check(f.IsDone(),"oracle Fillet");shape=f.Shape();}
        return shape;
    };
    auto blendFace=[&](const CadViewport::CadPart& part,EdgeBlendFeatures::Kind kind,double& size) {
        auto state=part.blends;check(EdgeBlendFeatures::resolve(part.shape,state,error),qPrintable(error));
        for(const auto& f:state.features)if(f.kind==kind) {size=f.size;return f.faces.front();}
        throw std::runtime_error("Cone 3 transition missing");
    };
    load();before=v.boxes_[cone];const auto coneStamp=geometryStamp(before.shape);int cones=0,tori=0,planes=0;
    {auto state=before.blends;check(EdgeBlendFeatures::resolve(before.shape,state,error)&&state.features.size()==2,"Cone 3 transitions not recognized");}
    {double size=0;blendFace(before,EdgeBlendFeatures::Kind::Chamfer,size);check(std::abs(size-8)<1.e-6,"Cone 3 Chamfer is not 8 mm");
     blendFace(before,EdgeBlendFeatures::Kind::Fillet,size);check(std::abs(size-11)<1.e-6,"Cone 3 Fillet is not 11 mm");}
    check(BRepReconstruction::equivalent(before.shape,coneOracle(0,80,8,11)),"Cone 3 differs from measured dimensions");
    for(TopExp_Explorer it(before.shape,TopAbs_FACE);it.More();it.Next()) {
        auto face=TopoDS::Face(it.Current());auto type=BRepAdaptor_Surface(face).GetType();
        cones+=type==GeomAbs_Cone;tori+=type==GeomAbs_Torus;planes+=type==GeomAbs_Plane;
        auto state=before.blends;EdgeBlendFeatures::resolve(before.shape,state,error);
        if(EdgeBlendFeatures::selected(state,face)>=0||type==GeomAbs_Plane) continue;
        // The body cone is ordinary geometry: every command refuses explicitly and atomically.
        status.clear();check(!v.startEditEdgeBlend(cone,face)&&!status.isEmpty(),"body cone Edit accepted silently");
        choose(v,cone,face);status.clear();v.startPrimitivePull();check(!v.commandSession_&&status.contains("not certified"),"body cone Pull not refused");
        choose(v,cone,face);status.clear();v.moveFace();check(!v.commandSession_&&status.contains("not yet certified"),"body cone Move not refused");v.resetModelingTools();
        choose(v,cone,face);status.clear();v.deleteFace();check(!v.commandSession_&&!status.isEmpty(),"body cone Delete not refused");
        check(v.boxes_[cone].shape.IsSame(before.shape)&&geometryStamp(before.shape)==coneStamp&&v.undoStack_.empty()&&v.redoStack_.empty(),"body cone refusal changed geometry/history");
    }
    check(cones==2&&tori==1&&planes==2&&doc.parts[cone].edgeBlendRecipe.isEmpty(),"actual Cone 3 classification changed");
    // Edit Fillet / Edit Chamfer through the context menu, exact input, preview, Cancel, OK, Undo/Redo.
    for(bool fillet:{true,false}) {
        load();before=v.boxes_[cone];double size=0;
        const auto face=blendFace(before,fillet?EdgeBlendFeatures::Kind::Fillet:EdgeBlendFeatures::Kind::Chamfer,size);
        choose(v,cone,face);std::unique_ptr<QMenu> menu(v.createSelectionMenu(QPoint(-10,-10)));
        auto* edit=menu->findChild<QAction*>(fillet?"context.edit_fillet":"context.edit_chamfer");check(edit,"Cone 3 transition missing Edit action");edit->trigger();
        check(v.commandSession_&&!v.moveFaceArrow_.IsNull()&&!v.commandSession_->preview.IsNull(),"Cone 3 Edit missing orange arrow or blue preview");
        check(std::abs(QLocale().toDouble(v.dragDimension_->text())-(fillet?11.:8.))<1.e-6,"Cone 3 Edit did not show the recovered dimension");
        checkFilletDirection(v);
        const double changed=fillet?9:6;
        v.dragDimension_->setText(QLocale().toString(changed));check(v.updateCommandParameter(false),qPrintable(v.commandSession_->geometryError));
        const auto expected=coneOracle(0,80,fillet?8:changed,fillet?changed:11);
        check(BRepReconstruction::equivalent(v.commandSession_->candidate,expected),"Cone 3 Edit preview material wrong");strictTolerances(v.commandSession_->candidate);
        v.cancelCommandSession();check(v.boxes_[cone].shape.IsSame(before.shape)&&v.undoStack_.empty(),"Cone 3 Edit Cancel changed geometry/history");
        check(v.startEditEdgeBlend(cone,face),"Cone 3 Edit restart failed");
        v.dragDimension_->setText(QLocale().toString(changed));v.confirmCommandSession();
        check(v.undoStack_.size()==1&&BRepReconstruction::equivalent(v.boxes_[cone].shape,expected)&&v.boxes_[cone].blends.features.size()==2,"Cone 3 Edit commit failed");
        double other=0;blendFace(v.boxes_[cone],fillet?EdgeBlendFeatures::Kind::Chamfer:EdgeBlendFeatures::Kind::Fillet,other);
        check(std::abs(other-(fillet?8:11))<1.e-10,"Cone 3 Edit changed the unrelated transition");
        v.undoPull();check(v.boxes_[cone].shape.IsSame(before.shape),"Cone 3 Edit Undo failed");
        v.redoPull();check(BRepReconstruction::equivalent(v.boxes_[cone].shape,expected),"Cone 3 Edit Redo failed");
    }
    // Pull and Move on both caps, Keep ON and OFF.
    for(bool top:{false,true})for(bool pull:{false,true})for(bool keep:{false,true}) {
        load();before=v.boxes_[cone];const auto stamp=geometryStamp(before.shape);auto face=cap(before.shape,top);choose(v,cone,face);
        if(pull)v.startPrimitivePull();else v.moveFace();
        check(v.commandSession_&&v.commandSession_->face.IsSame(face),"Cone 3 cap command lost selected Face");
        v.keepBlends_->setChecked(keep);v.dragDimension_->setText("3");
        check(v.updateCommandParameter(false),qPrintable(v.commandSession_->geometryError));
        auto candidate=v.commandSession_->changes.front().faceChange->after;
        strictTolerances(candidate.shape);strictTolerances(candidate.blends.sharp);
        const bool chamferKept=keep||!top,filletKept=keep||top;
        check(candidate.blends.features.size()==size_t(chamferKept)+size_t(filletKept),"Cone 3 Keep removed unrelated or retained affected transition");
        for(const auto& f:candidate.blends.features) check(std::abs(f.size-(f.kind==EdgeBlendFeatures::Kind::Fillet?11:8))<1.e-10,"Cone 3 Keep changed a dimension");
        const auto expected=coneOracle(top?0:-3,top?83:80,chamferKept?8:0,filletKept?11:0);
        check(BRepReconstruction::equivalent(candidate.shape,expected),"Cone 3 cap result differs from independent construction");
        v.cancelCommandSession();check(v.boxes_[cone].shape.IsSame(before.shape)&&geometryStamp(before.shape)==stamp&&v.undoStack_.empty(),"Cone 3 cap Cancel changed geometry/history");
        choose(v,cone,face);if(pull)v.startPrimitivePull();else v.moveFace();v.keepBlends_->setChecked(keep);
        v.dragDimension_->setText("3");v.confirmCommandSession();
        check(v.undoStack_.size()==1&&BRepReconstruction::equivalent(v.boxes_[cone].shape,expected),"Cone 3 cap confirmation lost result");
        v.undoPull();check(v.boxes_[cone].shape.IsSame(before.shape),"Cone 3 cap Undo failed");
        v.redoPull();check(BRepReconstruction::equivalent(v.boxes_[cone].shape,expected),"Cone 3 cap Redo failed");
    }
    // Delete Fillet / Delete Chamfer restore the sharp supports.
    for(bool fillet:{true,false}) {
        load();before=v.boxes_[cone];double size=0;
        const auto face=blendFace(before,fillet?EdgeBlendFeatures::Kind::Fillet:EdgeBlendFeatures::Kind::Chamfer,size);
        choose(v,cone,face);v.deleteFace();check(v.commandSession_&&v.commandSession_->valid,"Cone 3 Delete transition failed");
        v.confirmCommandSession();
        const auto expected=coneOracle(0,80,fillet?8:0,fillet?0:11);
        check(v.undoStack_.size()==1&&BRepReconstruction::equivalent(v.boxes_[cone].shape,expected)&&v.boxes_[cone].blends.features.size()==1,"Cone 3 Delete did not restore sharp supports");
        v.undoPull();check(v.boxes_[cone].shape.IsSame(before.shape),"Cone 3 Delete Undo failed");
        v.redoPull();check(BRepReconstruction::equivalent(v.boxes_[cone].shape,expected),"Cone 3 Delete Redo failed");
    }
    // Rotate Face on a cap breaks coaxiality: explicit atomic refusal.
    load();before=v.boxes_[cone];choose(v,cone,cap(before.shape,true));
    check(v.startRotateFace(cone,cap(before.shape,true)),"Cone 3 Rotate session unavailable");v.chooseFaceRotationAxis(1);
    v.dragDimension_->setText("5");check(!v.updateCommandParameter(false)&&v.commandSession_->geometryError.contains("coaxiality"),"Cone 3 cap Rotate not refused");
    v.cancelCommandSession();check(v.boxes_[cone].shape.IsSame(before.shape)&&v.undoStack_.empty(),"Cone 3 Rotate refusal changed geometry/history");
    // Save/Open of an edited recipe (version 3) and Duplicate.
    load();before=v.boxes_[cone];{double size=0;check(v.startEditEdgeBlend(cone,blendFace(before,EdgeBlendFeatures::Kind::Chamfer,size)),"Cone 3 pre-Save Edit failed");}
    v.dragDimension_->setText(QLocale().toString(6.5,'f',1));v.confirmCommandSession();
    {
        NativeDocumentSnapshot saved,reopened;QTemporaryDir directory;
        check(v.exportDocumentSnapshot(saved)&&saved.parts[cone].edgeBlendRecipe.value("version").toInt()==3,"conical recipe not saved as version 3");
        check(NativeDocumentStorage::save(directory.filePath("cone.odc"),saved,error)&&NativeDocumentStorage::load(directory.filePath("cone.odc"),reopened,error)
            &&v.importDocumentSnapshot(reopened,error),"Cone 3 Save/Open failed");
        double size=0;check(v.boxes_[cone].blends.features.size()==2&&v.boxes_[cone].blends.certified.IsSame(v.boxes_[cone].shape),"Cone 3 recipe not certified after Open");
        blendFace(v.boxes_[cone],EdgeBlendFeatures::Kind::Chamfer,size);check(std::abs(size-6.5)<1.e-10,"Cone 3 edited Chamfer lost on Open");
        check(BRepReconstruction::equivalent(v.boxes_[cone].shape,coneOracle(0,80,6.5,11)),"Cone 3 geometry changed on Open");
    }
    {
        const auto sourceShape=v.boxes_[cone].shape;const auto sourceRecipe=QJsonDocument(EdgeBlendFeatures::encode(v.boxes_[cone].blends)).toJson();
        v.selectParts({cone});v.duplicateSelectedParts();const size_t copy=v.boxes_.size()-1;
        check(copy!=cone&&v.partId(copy)!=v.partId(cone)&&v.boxes_[copy].blends.features.size()==2,"Cone 3 Duplicate lost recipe");
        double size=0;check(v.startEditEdgeBlend(copy,blendFace(v.boxes_[copy],EdgeBlendFeatures::Kind::Fillet,size)),"Cone 3 duplicate not editable");
        v.dragDimension_->setText("7");v.confirmCommandSession();
        blendFace(v.boxes_[copy],EdgeBlendFeatures::Kind::Fillet,size);check(std::abs(size-7)<1.e-10,"Cone 3 duplicate Edit failed");
        check(v.boxes_[cone].shape.IsSame(sourceShape)&&QJsonDocument(EdgeBlendFeatures::encode(v.boxes_[cone].blends)).toJson()==sourceRecipe,"duplicate Edit mutated source Part");
    }
    load();before=v.boxes_[cone];
    for(TopExp_Explorer it(before.shape,TopAbs_FACE);it.More();it.Next())if(BRepAdaptor_Surface(TopoDS::Face(it.Current())).GetType()==GeomAbs_Cone) {pickActual(v,cone,TopoDS::Face(it.Current()));break;}
    // Ordinary cones retain the preexisting certified cap offset route.
    v.resetModelingTools();v.addCone();const size_t ordinary=v.boxes_.size()-1;before=v.boxes_[ordinary];
    auto state=before.blends;check(!EdgeBlendFeatures::resolve(before.shape,state,error),"ordinary cone is a Chamfer");
    choose(v,ordinary,cap(before.shape,true));v.moveFace();check(v.commandSession_.has_value(),"ordinary cone Move unavailable");
    v.dragDimension_->setText("1");check(v.updateCommandParameter(false),qPrintable(v.commandSession_->geometryError));
    auto moved=v.commandSession_->candidate;check(BRepCheck_Analyzer(moved).IsValid()&&volume(moved)>volume(before.shape),"ordinary cone cap Move invalid/unchanged");v.cancelCommandSession();
    choose(v,ordinary,cap(before.shape,true));v.startPrimitivePull();check(v.pullMode_&&v.arrowVisible_,"ordinary cone Pull unavailable");
    v.fitAll();const auto target=CadViewport::ConePullTarget::TopDiameter;
    const auto grab=v.coneArrowOrigin(v.boxes_[ordinary],target).Translated(gp_Vec(v.coneArrowDirection(target))*22).Transformed(v.boxes_[ordinary].orientation);
    Standard_Integer gx,gy;v.view_->Convert(grab.X(),grab.Y(),grab.Z(),gx,gy);
    check(v.beginArrowDrag(QPoint(gx,gy))&&v.conePullTarget_==target,"ordinary cone Pull target failed");
    v.dragDimension_->setText("42");check(v.updateCommandParameter(false),"ordinary cone diameter Pull failed");
    check(BRepCheck_Analyzer(v.boxes_[ordinary].shape).IsValid()&&volume(v.boxes_[ordinary].shape)>volume(before.shape),"ordinary cone Pull has no valid geometric effect");
    v.cancelCommandSession();check(v.boxes_[ordinary].shape.IsSame(before.shape),"ordinary cone Pull Cancel failed");v.resetModelingTools();
    // A conical through-hole is safely deletable, unlike a cone's exterior wall.
    auto box=BRepPrimAPI_MakeBox(100,80,60).Shape();
    auto hole=BRepAlgoAPI_Cut(box,BRepPrimAPI_MakeCone(gp_Ax2(gp_Pnt(30,40,-10),gp_Dir(0,0,1)),6,14,80).Shape()).Shape();
    TopoDS_Face wall;for(TopExp_Explorer it(hole,TopAbs_FACE);it.More();it.Next())if(BRepAdaptor_Surface(TopoDS::Face(it.Current())).GetType()==GeomAbs_Cone)wall=TopoDS::Face(it.Current());
    BRepReconstruction::Result result;check(!wall.IsNull()&&DeleteFaceGeometry::remove(hole,{wall},result,error),qPrintable(error));
    check(BRepCheck_Analyzer(result.solid).IsValid()&&BRepReconstruction::equivalent(result.solid,box),"conical hole deletion did not restore stock");
    auto holePart=v.boxes_[ordinary];holePart.shape=hole;holePart.conePrimitive=false;v.applyFaceState(ordinary,holePart);
    choose(v,ordinary,cap(hole,true));v.startPrimitivePull();check(v.commandSession_.has_value(),"nonprimitive conical-hole cap Pull missing");
    v.dragDimension_->setText("1");check(v.updateCommandParameter(false),qPrintable(v.commandSession_->geometryError));
    check(BRepCheck_Analyzer(v.commandSession_->candidate).IsValid()&&volume(v.commandSession_->candidate)>volume(hole),"nonprimitive cone cap Pull invalid/unchanged");
    v.cancelCommandSession();check(v.boxes_[ordinary].shape.IsSame(hole),"nonprimitive cap Pull Cancel changed geometry");
}
static void checkFilletDirection(CadViewport& v) {
    const auto& s=*v.commandSession_;
    if(s.kind!=CadViewport::CommandSession::Kind::Fillet)return;
    const auto inverse=s.faceBefore->orientation.Inverted();
    const auto anchor=s.editBlendId.isEmpty()?s.pivot:s.pivot.Translated(gp_Vec(s.direction)*s.value);
    const auto inside=anchor.Translated(gp_Vec(s.direction)*0.001).Transformed(inverse);
    const auto outside=anchor.Translated(gp_Vec(s.direction)*(-0.001)).Transformed(inverse);
    check(BRepClass3d_SolidClassifier(s.faceBefore->shape,inside,1.e-7).State()==TopAbs_IN,"increasing radius direction must enter material");
    check(BRepClass3d_SolidClassifier(s.faceBefore->shape,outside,1.e-7).State()==TopAbs_OUT,"decreasing radius direction must leave material");
    bool cone=false;
    for(TopExp_Explorer it(v.moveFaceArrow_->Shape(),TopAbs_FACE);it.More();it.Next()) {
        BRepAdaptor_Surface surface(TopoDS::Face(it.Current()));
        if(surface.GetType()!=GeomAbs_Cone)continue;
        cone=true;
        check(surface.Cone().Axis().Direction().Dot(s.direction)>0.999,"arrowhead disagrees with radius direction");
        const auto tip=surface.Cone().Apex();
        const auto expected=s.pivot.Translated(gp_Vec(s.direction)*s.value);
        check(tip.Distance(expected)<1.e-6,"arrow tip is not at Fillet anchor");
    }
    check(cone,"Fillet arrowhead missing");
}

static void circular() {
    CadViewport v;v.resize(1000,800);v.show();QApplication::processEvents();v.addCylinder();
    gp_Trsf placement;placement.SetRotation(gp_Ax1(gp_Pnt(),gp_Dir(1,2,3)),0.3);
    placement.SetTranslationPart(gp_Vec(12,23,34));
    v.applyPartState(0,v.boxes_[0].min,v.boxes_[0].max,placement);v.fitAll();QApplication::processEvents();
    const auto original=v.boxes_[0];
    TopoDS_Edge circularSelection;
    auto arm=[&](bool preselected=false) {
        if(preselected) {
            v.selectParts({});
            Handle(StdSelect_BRepOwner) owner=new StdSelect_BRepOwner(circularSelection,v.boxes_[0].presentation,5,Standard_True);
            v.context_->AddOrRemoveSelected(owner,Standard_False);
        }
        v.fillet();bool picked=preselected && v.commandSession_->valid;
        for(TopExp_Explorer it(v.boxes_[0].shape,TopAbs_EDGE);it.More()&&!picked;it.Next()) {
            BRepAdaptor_Curve c(TopoDS::Edge(it.Current()));if(c.GetType()!=GeomAbs_Circle) continue;
            for(int sample=0;sample<12&&!picked;++sample) {
                auto p=c.Value(c.FirstParameter()+(c.LastParameter()-c.FirstParameter())*(sample+0.5)/12).Transformed(placement);
                Standard_Integer x,y;v.view_->Convert(p.X(),p.Y(),p.Z(),x,y);
                picked=v.pickEdgeBlend(QPoint(x,y));
            }
        }
        check(picked&&v.commandSession_->valid,"circular typed picking/preselection preview failed");
        circularSelection=v.commandSession_->edge;
        checkFilletDirection(v);
        check(!v.moveFaceArrow_.IsNull()&&!v.commandSession_->preview.IsNull(),"circular arrow/blue preview missing");
    };
    arm();v.cancelCommandSession();check(v.boxes_[0].shape.IsSame(original.shape)&&v.undoStack_.empty(),"circular Cancel changed document");
    v.selectParts({});
    Handle(StdSelect_BRepOwner) circularOwner=new StdSelect_BRepOwner(circularSelection,v.boxes_[0].presentation,5,Standard_True);
    v.context_->AddOrRemoveSelected(circularOwner,Standard_False);v.chamfer();
    check(v.boxes_[0].shape.IsSame(original.shape),"circular Chamfer adoption changed geometry");
    check(!v.commandSession_->valid || BRepCheck_Analyzer(v.commandSession_->candidate).IsValid(),"circular Chamfer produced invalid preview");
    check(v.commandSession_->valid || !v.commandButtons_->button(QDialogButtonBox::Ok)->isEnabled(),"unsupported circular Chamfer can be confirmed");
    v.cancelCommandSession();
    arm(true);v.dragDimension_->setText("2.345");check(v.updateCommandParameter(false),"circular numeric radius failed");
    const auto& session=*v.commandSession_;
    auto mid=session.pivot.Translated(gp_Vec(session.direction)*(session.value-15.5));
    Standard_Integer x,y;v.view_->Convert(mid.X(),mid.Y(),mid.Z(),x,y);QPoint start(x,y);
    check(v.beginMoveFaceDrag(start),"circular orange drag capture failed");
    QPoint end=start+QPoint(int(std::round(v.dragScreenX_/v.dragMmPerPixel_)),int(std::round(v.dragScreenY_/v.dragMmPerPixel_)));
    QMouseEvent drag(QEvent::MouseMove,QPointF(end),QPointF(end),Qt::NoButton,Qt::LeftButton,Qt::ShiftModifier);v.mouseMoveEvent(&drag);
    check(v.commandSession_->valid&&v.commandSession_->value>2.345,"circular drag did not change radius");
    QMouseEvent release(QEvent::MouseButtonRelease,QPointF(end),QPointF(end),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);v.mouseReleaseEvent(&release);
    v.dragDimension_->setText("2.345");check(v.updateCommandParameter(false),"circular exact radius recovery");v.confirmCommandSession();
    check(v.undoStack_.size()==1&&v.boxes_[0].blends.features.size()==1,"circular commit missing recipe/history");
    v.undoPull();check(v.boxes_[0].shape.IsSame(original.shape),"circular Undo failed");v.redoPull();
    TopoDS_Face cap,wall;
    for(TopExp_Explorer it(v.boxes_[0].shape,TopAbs_FACE);it.More();it.Next()) {
        auto f=TopoDS::Face(it.Current());BRepAdaptor_Surface a(f);
        if(a.GetType()==GeomAbs_Plane) cap=f;
        if(a.GetType()==GeomAbs_Cylinder) wall=f;
    }
    check(v.startMoveFace(0,cap),"ordinary circular supporting face selection failed");
    v.dragDimension_->setText("1");check(v.updateCommandParameter(false)&&BRepCheck_Analyzer(v.commandSession_->candidate).IsValid(),"circular cap Keep ON reconstruction failed");
    v.keepBlends_->setChecked(false);check(v.commandSession_->valid&&BRepCheck_Analyzer(v.commandSession_->candidate).IsValid(),"circular cap Keep OFF reconstruction failed");v.cancelCommandSession();
    check(!v.startMoveFace(0,wall),"circular cylindrical support bypassed Keep refusal");
    check(v.startEditEdgeBlend(0,v.boxes_[0].blends.features[0].faces.front()),"circular Edit unavailable");
    checkFilletDirection(v);
    check(std::abs(v.commandSession_->value-2.345)<1.e-9&&!v.moveFaceArrow_.IsNull(),"circular absolute radius/arrow incorrect");
    v.dragDimension_->setText("1.25");v.confirmCommandSession();
    NativeDocumentSnapshot saved,loaded;QTemporaryDir temp;QString error;
    check(v.exportDocumentSnapshot(saved)&&NativeDocumentStorage::save(temp.filePath("circle.odc"),saved,error)
        &&NativeDocumentStorage::load(temp.filePath("circle.odc"),loaded,error)&&v.importDocumentSnapshot(loaded,error),"circular native roundtrip failed");
    check(v.startEditEdgeBlend(0,v.boxes_[0].blends.features[0].faces.front()),"loaded circular Edit unavailable");v.cancelCommandSession();
    v.selectParts({0});v.duplicateSelectedParts();
    check(v.startEditEdgeBlend(1,v.boxes_[1].blends.features[0].faces.front()),"duplicate circular Edit unavailable");
    v.dragDimension_->setText("1.5");v.confirmCommandSession();
    check(std::abs(v.boxes_[0].blends.features[0].size-1.25)<1.e-9,"duplicate circular edit changed original");
    v.context_->ClearSelected(false);v.selectedPartIndices_.clear();
    Handle(StdSelect_BRepOwner) owner=new StdSelect_BRepOwner(v.boxes_[0].blends.features[0].faces.front(),v.boxes_[0].presentation,5,true);
    v.context_->AddOrRemoveSelected(owner,false);v.deleteFace();
    check(v.commandSession_&&v.commandSession_->valid,"circular Delete preview unavailable");v.confirmCommandSession();
    check(v.boxes_[0].blends.features.empty()&&std::abs(volume(v.boxes_[0].shape)-volume(original.shape))<1.e-6,"circular Delete did not restore sharp solid");
    v.undoPull();check(v.boxes_[0].blends.features.size()==1,"circular Delete Undo lost recipe");
}
static void preselection() {
    for(bool chamfer:{false,true}) {
        CadViewport v;v.resize(1000,800);v.show();QApplication::processEvents();v.addBox();
        const auto original=v.boxes_[0].shape;
        TopExp_Explorer it(original,TopAbs_EDGE);const auto first=TopoDS::Edge(it.Current());it.Next();const auto second=TopoDS::Edge(it.Current());
        const auto select=[&](const TopoDS_Shape& shape) {
            Handle(StdSelect_BRepOwner) owner=new StdSelect_BRepOwner(shape,v.boxes_[0].presentation,5,Standard_True);
            v.context_->AddOrRemoveSelected(owner,Standard_False);
        };
        v.startEdgeBlend(chamfer);check(!v.commandSession_->faceBefore,"empty selection adopted topology");v.cancelCommandSession();
        v.selectParts({0});v.startEdgeBlend(chamfer);check(!v.commandSession_->faceBefore,"Part expanded into edges");v.cancelCommandSession();v.selectParts({});
        TopExp_Explorer face(original,TopAbs_FACE);select(face.Current());v.startEdgeBlend(chamfer);
        check(!v.commandSession_->faceBefore,"Face reinterpreted as Edge");v.cancelCommandSession();v.selectParts({});
        QString message;QObject::connect(&v,&CadViewport::statusMessage,[&](const QString& text){message=text;});
        select(first);select(second);v.startEdgeBlend(chamfer);
        check(!v.commandSession_->faceBefore && message.contains("unsupported"),"multi-edge set silently truncated");
        check(v.adoptEdgeBlend(0,second) && v.commandSession_->changes.size()==1,"cannot correct unsupported selection");
        check(v.adoptEdgeBlend(0,first) && v.commandSession_->changes.size()==1 && v.commandSession_->edge.IsSame(first),"replacement Edge duplicated command changes");
        v.cancelCommandSession();v.selectParts({});
        const auto foreign=BRepPrimAPI_MakeBox(1,2,3).Shape();TopExp_Explorer stale(foreign,TopAbs_EDGE);
        select(stale.Current());v.startEdgeBlend(chamfer);
        check(!v.commandSession_->faceBefore && message.contains("unsupported"),"stale Edge adopted");v.cancelCommandSession();v.selectParts({});
        select(first);select(face.Current());v.startEdgeBlend(chamfer);
        check(!v.commandSession_->faceBefore && message.contains("unsupported"),"mixed selection reinterpreted");v.cancelCommandSession();v.selectParts({});
        select(first);v.startEdgeBlend(chamfer);
        check(v.commandSession_->valid && v.commandSession_->edge.IsSame(first) && !v.moveFaceArrow_.IsNull(),"preselected straight Edge not adopted with preview/arrow");
        checkFilletDirection(v);
        v.dragDimension_->setText("0");check(!v.updateCommandParameter(false),"invalid preselection value accepted");
        v.dragDimension_->setText("1.23456789");check(v.updateCommandParameter(false),"exact preselection input failed");
        QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(v.commandPanel_,&escape);
        check(!v.commandSession_ && v.boxes_[0].shape.IsSame(original) && v.undoStack_.empty(),"preselection Escape changed geometry/history");
        v.selectParts({});select(first);v.startEdgeBlend(chamfer);v.confirmCommandSession();
        check(v.undoStack_.size()==1 && !v.commandSession_,"preselection confirmation not atomic");
        v.undoPull();check(v.boxes_[0].shape.IsSame(original),"preselection Undo failed");v.redoPull();check(v.undoStack_.size()==1,"preselection Redo failed");
    }
}
static void run() {
    CadViewport v;v.resize(1000,800);v.show();QApplication::processEvents();v.addBox();v.addCylinder();
    const auto untouched=v.boxes_[1];
    v.renamePart(0,"Edited Part");v.setPartColor(0,QColor(40,90,130));
    gp_Trsf placement;placement.SetRotation(gp_Ax1(gp_Pnt(),gp_Dir(1,2,3)),0.3);placement.SetTranslationPart(gp_Vec(12,23,34));
    v.applyPartState(0,v.boxes_[0].min,v.boxes_[0].max,placement);
    v.fitAll();QApplication::processEvents();
    const auto original=v.boxes_[0];const double before=volume(original.shape);
    auto arm=[&](bool chamfer) {
        v.startEdgeBlend(chamfer);
        v.dragDimension_->setText("2");
        bool picked=false;
        for(TopExp_Explorer it(v.boxes_[0].shape,TopAbs_EDGE);it.More() && !picked;it.Next()) {
            BRepAdaptor_Curve curve(TopoDS::Edge(it.Current()));
            auto midpoint=curve.Value((curve.FirstParameter()+curve.LastParameter())/2).Transformed(v.boxes_[0].orientation);
            Standard_Integer x,y;v.view_->Convert(midpoint.X(),midpoint.Y(),midpoint.Z(),x,y);
            v.context_->MoveTo(x,y,v.view_,Standard_False);
            if(v.context_->HasDetected() && v.context_->DetectedInteractive()==v.boxes_[0].presentation)
                picked=v.pickEdgeBlend(QPoint(x,y));
        }
        check(picked && v.commandSession_->valid,"typed edge pick/preview failed");
        checkFilletDirection(v);
        check(!v.moveFaceArrow_.IsNull(),"creation did not show orange Pull arrow");
    };
    for(bool chamfer:{false,true}) {
        arm(chamfer);
        check(v.boxes_[0].shape.IsSame(original.shape) && !v.commandSession_->preview.IsNull(),"preview changed canonical geometry");
        const auto& creation=*v.commandSession_;
        const auto arrowMid=creation.pivot.Translated(gp_Vec(creation.direction)*(creation.value+(chamfer?15.5:-15.5)));
        Standard_Integer ax,ay;v.view_->Convert(arrowMid.X(),arrowMid.Y(),arrowMid.Z(),ax,ay);QPoint dragStart(ax,ay);
        check(v.beginMoveFaceDrag(dragStart),"creation orange arrow did not capture");
        QPoint dragEnd=dragStart+QPoint(int(std::round(v.dragScreenX_*3/v.dragMmPerPixel_)),int(std::round(v.dragScreenY_*3/v.dragMmPerPixel_)));
        QMouseEvent dragEvent(QEvent::MouseMove,QPointF(dragEnd),QPointF(dragEnd),Qt::NoButton,Qt::LeftButton,Qt::ShiftModifier);v.mouseMoveEvent(&dragEvent);
        check(v.commandSession_->valid&&v.commandSession_->value>2,"creation drag did not change actual dimension");
        QMouseEvent releaseEvent(QEvent::MouseButtonRelease,QPointF(dragEnd),QPointF(dragEnd),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);v.mouseReleaseEvent(&releaseEvent);
        check(!v.draggingFace_,"creation drag did not release");
        auto preview=v.commandSession_->preview;
        NativeDocumentSnapshot snapshot;check(v.exportDocumentSnapshot(snapshot),"preview export failed");
        check(std::abs(volume(snapshot.parts[0].shape)-before)<1.e-6,"Save included preview");
        QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);
        QApplication::sendEvent(v.commandPanel_,&escape);
        check(!v.commandSession_ && v.undoStack_.empty() && v.boxes_[0].shape.IsSame(original.shape),"Cancel changed document/history");
        check(v.context_->DisplayStatus(preview)==PrsMgr_DisplayStatus_None,"Cancel retained preview");
        arm(chamfer);
        v.dragDimension_->setText(QLocale().toString(1.23456789,'f',8));
        check(v.updateCommandParameter(false) && std::abs(v.commandSession_->value-1.23456789)<1.e-12,"exact fractional input rejected/rounded");
        v.dragDimension_->setText("-");check(!v.updateCommandParameter(false) && v.commandSession_->preview.IsNull(),"incomplete input retained preview");
        v.dragDimension_->setText("0");check(!v.updateCommandParameter(false),"invalid preview accepted");
        check(v.commandSession_->preview.IsNull() && !v.commandButtons_->button(QDialogButtonBox::Ok)->isEnabled(),"invalid preview retained");
        v.dragDimension_->setText("2");
        QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);
        QApplication::sendEvent(v.dragDimension_,&enter);
        check(!v.commandSession_ && v.undoStack_.size()==1,"confirmation not one step");
        const auto edited=v.boxes_[0];const double editedVolume=volume(edited.shape);
        check(editedVolume<before && BRepCheck_Analyzer(edited.shape).IsValid(),"edited material invalid");
        check(v.boxes_.size()==2 && v.boxes_[1].persistentId==untouched.persistentId && v.boxes_[1].shape.IsSame(untouched.shape),"Part slots or unrelated geometry changed");
        check(edited.persistentId==original.persistentId && edited.name==original.name && edited.color==original.color
            && edited.orientation.TranslationPart().IsEqual(original.orientation.TranslationPart(),1.e-12),"Part metadata lost");
        v.undoPull();check(v.undoStack_.empty() && std::abs(volume(v.boxes_[0].shape)-before)<1.e-6,"Undo failed");
        arm(chamfer);v.cancelCommandSession();check(v.redoStack_.size()==1,"Cancel destroyed Redo");
        v.redoPull();check(std::abs(volume(v.boxes_[0].shape)-editedVolume)<1.e-6,"Redo failed");
        check(v.selectedPartIndices_.empty() && v.context_->NbSelected()==0,"history retained selection");
        QTemporaryDir temp;NativeDocumentSnapshot loaded;QString error;
        check(v.exportDocumentSnapshot(snapshot) && NativeDocumentStorage::save(temp.filePath("blend.odc"),snapshot,error)
            && NativeDocumentStorage::load(temp.filePath("blend.odc"),loaded,error),"native roundtrip failed");
        check(loaded.parts[0].id==original.persistentId && std::abs(volume(loaded.parts[0].shape)-editedVolume)<1.e-6
            && BRepCheck_Analyzer(loaded.parts[0].shape).IsValid(),"native geometry/identity lost");
        v.undoPull();
        if(chamfer) {
            check(v.importDocumentSnapshot(loaded,error) && v.undoStack_.empty() && v.partId(0)==original.persistentId
                && std::abs(volume(v.boxes_[0].shape)-editedVolume)<1.e-6,"Open lost edited Part");
        }
    }
    v.startEdgeBlend(false);v.cancelCommandSession();check(!v.commandSession_,"empty picking Cancel failed");
}
static void features() {
    using namespace EdgeBlendFeatures;
    CadViewport v;v.resize(1000,800);v.show();QApplication::processEvents();v.addBox();
    auto sharp=BRepPrimAPI_MakeBox(100,80,60).Shape();
    const auto edgeAt=[](const TopoDS_Shape& s,const gp_Pnt& p) {
        for(TopExp_Explorer it(s,TopAbs_EDGE);it.More();it.Next()) {BRepAdaptor_Curve c(TopoDS::Edge(it.Current()));
            if(c.Value((c.FirstParameter()+c.LastParameter())/2).Distance(p)<1.e-6) return TopoDS::Edge(it.Current());}return TopoDS_Edge{};
    };
    State first,state;TopoDS_Shape fillet,both;QString error;
    check(create(sharp,{},edgeAt(sharp,gp_Pnt(0,0,30)),Kind::Fillet,5,first,fillet,error),"fillet recipe fixture failed");
    check(create(fillet,first,edgeAt(fillet,gp_Pnt(100,80,30)),Kind::Chamfer,3,state,both,error),"mixed recipe fixture failed");
    auto fixture=v.boxes_[0];fixture.shape=both;fixture.blends=state;
    fixture.geometryKind=CadViewport::CadPart::GeometryKind::GeneralBRep;
    fixture.min=fixture.baseMin={0,0,0};fixture.max=fixture.baseMax={100,80,60};
    gp_Trsf placement;placement.SetRotation(gp_Ax1(gp_Pnt(),gp_Dir(1,2,3)),0.4);placement.SetTranslationPart(gp_Vec(13,27,42));fixture.orientation=placement;
    v.applyFaceState(0,fixture);v.fitAll();QApplication::processEvents();
    const auto id=v.partId(0);const auto source=v.boxes_[0].shape;const auto recipes=QJsonDocument(encode(v.boxes_[0].blends)).toJson();
    auto select=[&](const TopoDS_Face& f) {
        v.context_->ClearSelected(false);v.selectedPartIndices_.clear();
        Handle(StdSelect_BRepOwner) owner=new StdSelect_BRepOwner(f,v.boxes_[0].presentation,5,true);
        v.context_->AddOrRemoveSelected(owner,false);
    };
    auto top=[&]() {for(TopExp_Explorer it(v.boxes_[0].shape,TopAbs_FACE);it.More();it.Next()) {
        BRepAdaptor_Surface a(TopoDS::Face(it.Current()));if(a.GetType()==GeomAbs_Plane&&a.Plane().Axis().Direction().IsParallel(gp_Dir(0,0,1),1.e-8)&&a.Plane().Distance(gp_Pnt(0,0,60))<1.e-6)return TopoDS::Face(it.Current());}return TopoDS_Face{};};
    // Older/imported documents have no recipe. Their current B-Rep must be
    // sufficient to restore ordinary editing, without mutating metadata on Cancel.
    auto legacy=fixture;legacy.blends={};v.applyFaceState(0,legacy);
    check(v.startMoveFace(0,top()),"legacy mixed B-Rep ordinary Move unavailable");
    v.dragDimension_->setText("20");check(v.updateCommandParameter(false),"legacy mixed B-Rep ordinary Move failed");
    v.cancelCommandSession();check(v.boxes_[0].blends.features.empty()&&v.boxes_[0].shape.IsSame(source),"recognition mutated legacy document on Cancel");
    v.applyFaceState(0,fixture);
    for(int i=0;i<2;++i) {
        const auto face=v.boxes_[0].blends.features[i].faces.front();select(face);
        std::unique_ptr<QMenu> menu(v.createSelectionMenu(QPoint(-10,-10)));
        check(!v.commandSession_,"opening context menu activated a command");
        auto* edit=menu->findChild<QAction*>(i==0?"context.edit_fillet":"context.edit_chamfer");check(edit,"recognized transition missing Edit action");edit->trigger();
        check(v.commandSession_&&!v.moveFaceArrow_.IsNull()&&!v.commandSession_->preview.IsNull(),"Edit missing orange arrow or blue preview");
        check(std::abs(QLocale().toDouble(v.dragDimension_->text())-(i==0?5.:3.))<1.e-10,"Edit did not show absolute dimension");
        checkFilletDirection(v);
        auto& session=*v.commandSession_;
        const auto origin=session.pivot.Translated(gp_Vec(session.direction)*session.value);
        const auto midpoint=origin.Translated(gp_Vec(session.direction)*(i==0?-15.5:15.5));
        Standard_Integer x,y;v.view_->Convert(midpoint.X(),midpoint.Y(),midpoint.Z(),x,y);
        const QPoint start(x,y);check(v.beginMoveFaceDrag(start),"orange arrow did not capture drag");
        const double initial=session.value;
        const QPoint end=start+QPoint(int(std::round(v.dragScreenX_*3/v.dragMmPerPixel_)),int(std::round(v.dragScreenY_*3/v.dragMmPerPixel_)));
        QMouseEvent drag(QEvent::MouseMove,QPointF(end),QPointF(end),Qt::NoButton,Qt::LeftButton,Qt::ShiftModifier);v.mouseMoveEvent(&drag);
        check(v.commandSession_->valid&&v.commandSession_->value>initial,"orange drag did not change absolute transition dimension");
        auto candidate=v.commandSession_->candidate;
        v.mouseMoveEvent(&drag);check(v.commandSession_->candidate.IsSame(candidate),"redundant pointer motion recomputed geometry");
        QMouseEvent release(QEvent::MouseButtonRelease,QPointF(end),QPointF(end),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);v.mouseReleaseEvent(&release);
        check(!v.draggingFace_,"arrow kept dragging after release");
        v.dragDimension_->setText(QLocale().toString(i==0?6.125:4.375,'f',3));check(v.updateCommandParameter(false),"exact edited dimension failed");
        check(v.boxes_[0].shape.IsSame(source),"Edit preview mutated source");
        v.cancelCommandSession();check(v.boxes_[0].shape.IsSame(source)&&v.undoStack_.empty(),"Edit Cancel changed history");
        check(v.startEditEdgeBlend(0,face),"Edit restart failed");
        v.dragDimension_->setText("7");v.confirmCommandSession();
        check(v.undoStack_.size()==1&&v.boxes_[0].blends.features[i].size==7&&v.partId(0)==id,"Edit did not commit metadata in one step");
        v.undoPull();check(v.boxes_[0].shape.IsSame(source)&&QJsonDocument(encode(v.boxes_[0].blends)).toJson()==recipes,"Edit Undo lost source metadata");
        v.redoPull();check(v.boxes_[0].blends.features[i].size==7,"Edit Redo lost dimension");v.undoPull();
    }
    select(top());check(v.startMoveFace(0,top()),"ordinary Move Face unavailable after both transitions");
    check(v.keepBlends_->isChecked()&&v.keepBlends_->isVisible(),"Move Face Keep did not default ON");
    v.dragDimension_->setText("20");check(v.updateCommandParameter(false),"ordinary Move Face preview failed");
    check(v.commandSession_->changes.front().faceChange->after.blends.features.size()==2,"Keep ON lost transitions");
    v.keepBlends_->setChecked(false);check(v.commandSession_->valid&&v.commandSession_->changes.front().faceChange->after.blends.features.empty(),"Keep OFF failed actual reconstruction");
    v.keepBlends_->setChecked(true);check(v.commandSession_->valid&&v.commandSession_->changes.front().faceChange->after.blends.features.size()==2,"Keep toggle used previous preview cumulatively");
    v.cancelCommandSession();check(v.boxes_[0].shape.IsSame(source),"Keep cancellation changed source");
    TopoDS_Face side;
    for(TopExp_Explorer it(v.boxes_[0].shape,TopAbs_FACE);it.More();it.Next()) {
        BRepAdaptor_Surface a(TopoDS::Face(it.Current()));
        if(a.GetType()==GeomAbs_Plane&&a.Plane().Axis().Direction().IsParallel(gp_Dir(1,0,0),1.e-8)&&a.Plane().Distance(gp_Pnt())<1.e-6) side=TopoDS::Face(it.Current());
    }
    select(side);check(v.startMoveFace(0,side)&&v.keepBlends_->isChecked(),"next Move did not reset Keep ON");
    v.keepBlends_->setChecked(false);v.dragDimension_->setText("-1000");
    check(!v.updateCommandParameter(false)&&v.boxes_[0].shape.IsSame(source)&&v.undoStack_.empty(),"failed Keep OFF partially committed transition removal");
    v.dragDimension_->setText("20");check(v.updateCommandParameter(false),"Keep OFF recovery failed");v.confirmCommandSession();
    check(v.undoStack_.size()==1&&v.boxes_[0].blends.features.size()==1&&v.boxes_[0].blends.features[0].id==state.features[1].id,"Keep OFF commit removed unrelated Chamfer");
    v.undoPull();check(v.boxes_[0].shape.IsSame(source)&&v.boxes_[0].blends.features.size()==2,"Keep OFF Undo lost geometry/recipes");
    v.redoPull();check(v.boxes_[0].blends.features.size()==1,"Keep OFF Redo lost recipes");v.undoPull();
    select(top());v.startPrimitivePull();check(v.commandSession_&&v.keepBlends_->isChecked(),"ordinary planar Pull unavailable after blends");
    v.dragDimension_->setText("10");check(v.updateCommandParameter(false),"ordinary planar Pull preview failed");v.confirmCommandSession();
    check(v.undoStack_.size()==1&&v.partId(0)==id&&v.boxes_[0].blends.features.size()==2,"Pull did not preserve recipe/identity");
    check(std::abs(volume(v.boxes_[0].shape)-volume(source)*70/60)<1.e-5,"Pull material/world-local displacement wrong");
    v.undoPull();v.redoPull();v.undoPull();
    select(top());check(v.startRotateFace(0,top()),"Rotate support unavailable");v.chooseFaceRotationAxis(1);
    check(v.keepBlends_->isChecked(),"Rotate Keep did not default ON");v.dragDimension_->setText("5");check(v.updateCommandParameter(false),"Rotate Keep ON preview failed");
    v.keepBlends_->setChecked(false);check(v.commandSession_->valid&&v.commandSession_->changes.front().faceChange->after.blends.features.empty(),"Rotate Keep OFF failed");v.cancelCommandSession();
    select(v.boxes_[0].blends.features[1].faces.front());
    check(v.startRotateFace(0,v.boxes_[0].blends.features[1].faces.front()),"legacy Chamfer Rotate unavailable");
    check(!v.keepBlends_->isEnabled(),"legacy direct rotation advertised unsupported Keep behavior");v.chooseFaceRotationAxis(3);
    v.dragDimension_->setText("2");check(v.updateCommandParameter(false),"legacy Chamfer Rotate regressed");v.confirmCommandSession();
    check(v.boxes_[0].blends.features.empty()&&BRepCheck_Analyzer(v.boxes_[0].shape).IsValid(),"legacy topology replacement retained an invalid recipe");
    v.undoPull();check(v.boxes_[0].shape.IsSame(source)&&v.boxes_[0].blends.features.size()==2,"legacy Rotate Undo lost certified transitions");
    select(v.boxes_[0].blends.features[0].faces.front());v.startPrimitivePull();check(v.commandSession_&&!v.commandSession_->editBlendId.isEmpty(),"transition Pull did not edit dimension");v.cancelCommandSession();
    select(v.boxes_[0].blends.features[0].faces.front());v.deleteFace();
    check(v.commandSession_&&v.commandSession_->valid,"Delete recognized Fillet failed");
    check(v.boxes_[0].shape.IsSame(source),"Delete preview changed canonical shape");v.cancelCommandSession();
    select(v.boxes_[0].blends.features[1].faces.front());v.deleteFace();v.confirmCommandSession();
    check(v.undoStack_.size()==1&&v.boxes_[0].blends.features.size()==1,"Delete Chamfer lost unrelated Fillet");
    v.undoPull();v.redoPull();v.undoPull();
    check(v.startEditEdgeBlend(0,v.boxes_[0].blends.features[1].faces.front()),"pre-Save dimension edit unavailable");
    v.dragDimension_->setText(QLocale().toString(4.375,'f',3));v.confirmCommandSession();
    const double savedVolume=volume(v.boxes_[0].shape);
    NativeDocumentSnapshot snapshot,loaded;QTemporaryDir temp;
    check(v.exportDocumentSnapshot(snapshot)&&!snapshot.parts[0].edgeBlendRecipe.isEmpty(),"recipe excluded from confirmed Save");
    check(NativeDocumentStorage::save(temp.filePath("features.odc"),snapshot,error)&&NativeDocumentStorage::load(temp.filePath("features.odc"),loaded,error)
        &&v.importDocumentSnapshot(loaded,error),"feature native Save/Open failed");
    check(v.partId(0)==id&&v.boxes_[0].blends.features.size()==2&&v.boxes_[0].blends.features[0].id==state.features[0].id
        &&std::abs(v.boxes_[0].blends.features[1].size-4.375)<1.e-10&&std::abs(volume(v.boxes_[0].shape)-savedVolume)<1.e-5,"Open lost edited solid or certified feature identity");
    check(v.startEditEdgeBlend(0,v.boxes_[0].blends.features[1].faces.front()),"Open could not edit recognized Chamfer");v.cancelCommandSession();
    v.selectParts({0});v.duplicateSelectedParts();
    check(v.boxes_.size()==2&&v.partId(1)!=id&&v.boxes_[1].blends.features.size()==2,"Duplication lost transition recipe");
    check(v.startEditEdgeBlend(1,v.boxes_[1].blends.features[0].faces.front()),"duplicate feature not editable");
    v.dragDimension_->setText("8");v.confirmCommandSession();check(v.boxes_[0].blends.features[0].size==5,"duplicate Edit mutated source metadata");
}

};
void runEdgeBlendSessions() {EdgeBlendSessionTest::preselection();EdgeBlendSessionTest::run();EdgeBlendSessionTest::circular();}

void runEdgeBlendFeatureSessions() {EdgeBlendSessionTest::features();}

// The v0.5.1 recovery fixture (Cylinder 2: R40 x 80 with a certified 20 mm top
// Fillet recipe; Cone 3: R40->R20 x 80, 8 mm top Chamfer, 11 mm bottom Fillet,
// no recipe) is built in code, saved through NativeDocumentStorage and reopened.
// Every recovery assertion then runs on the reopened document.
static void syntheticRecoveryDocument() {
    QTemporaryDir directory;check(directory.isValid(),"temporary directory unavailable");
    const auto generated=EdgeBlendSessionTest::recoveryFixture();QString error;
    const auto path=directory.filePath("synthetic-recovery.odc");
    check(NativeDocumentStorage::save(path,generated,error),qPrintable(error));
    NativeDocumentSnapshot reopened;check(NativeDocumentStorage::load(path,reopened,error),qPrintable(error));
    check(reopened.parts.size()==2&&reopened.parts[0].name=="Cylinder 2"&&reopened.parts[1].name=="Cone 3"
        &&reopened.parts[0].id==generated.parts[0].id&&reopened.parts[1].id==generated.parts[1].id,"synthetic recovery document lost Part identity");
    check(!reopened.parts[0].edgeBlendRecipe.isEmpty()&&reopened.parts[1].edgeBlendRecipe.isEmpty(),"synthetic recovery document changed recipe presence");
    EdgeBlendSessionTest::recovery(path);
}
// Optional, local only: OPENDESIGNER_PRIVATE_FIXTURE names a private .odc.
// The file is copied to a temporary directory and only the copy is opened; the
// original is verified byte-identical afterwards. CI never sets the variable.
static void privateFixtureProbe() {
    const auto source=qEnvironmentVariable("OPENDESIGNER_PRIVATE_FIXTURE");
    if(source.isEmpty()) {std::cout<<"OPENDESIGNER_PRIVATE_FIXTURE not set: private fixture probe skipped.\n";return;}
    if(!QFileInfo(source).isFile()) {std::cout<<"OPENDESIGNER_PRIVATE_FIXTURE is not a file: private fixture probe skipped.\n";return;}
    auto digest=[](const QString& path) {QFile file(path);check(file.open(QIODevice::ReadOnly),"private fixture unreadable");
        QCryptographicHash hash(QCryptographicHash::Sha256);hash.addData(&file);return hash.result();};
    const auto original=digest(source);
    QTemporaryDir directory;check(directory.isValid(),"temporary directory unavailable");
    const auto copy=directory.filePath("private-fixture.odc");check(QFile::copy(source,copy),"private fixture copy failed");
    NativeDocumentSnapshot doc;QString error;check(NativeDocumentStorage::load(copy,doc,error),qPrintable(error));
    const auto generated=EdgeBlendSessionTest::recoveryFixture();bool matches=true;
    for(const auto& expected:generated.parts) {
        auto found=std::find_if(doc.parts.begin(),doc.parts.end(),[&](const auto& p){return p.name==expected.name;});
        if(found==doc.parts.end()) {std::cout<<"private fixture: "<<expected.name.toStdString()<<" missing\n";matches=false;continue;}
        check(BRepCheck_Analyzer(found->shape).IsValid(),"private fixture contains an invalid B-Rep");
        const bool equivalent=BRepReconstruction::equivalent(found->shape,expected.shape);
        std::cout<<"private fixture: "<<expected.name.toStdString()<<(equivalent?" matches":" differs from")<<" the v0.5.1 recovery fixture\n";
        matches=matches&&equivalent;
    }
    if(matches) EdgeBlendSessionTest::recovery(copy);
    else std::cout<<"private fixture diverged from v0.5.1 expectations: recovery assertions skipped for it.\n";
    check(digest(source)==original,"private fixture was modified");
}
void runRegressionDocumentProbe() {syntheticRecoveryDocument();privateFixtureProbe();}
void runDirectModelingRecovery() {EdgeBlendSessionTest::recovery();}
