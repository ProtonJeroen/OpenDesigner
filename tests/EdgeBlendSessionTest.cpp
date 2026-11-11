#include "CadViewport.h"
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
#include <BRepAdaptor_Curve.hxx>
#include <stdexcept>
#include <cmath>
namespace {
void check(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
double volume(const TopoDS_Shape& shape) {GProp_GProps p;BRepGProp::VolumeProperties(shape,p);return p.Mass();}
}
struct EdgeBlendSessionTest {
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
        check(!v.moveFaceArrow_.IsNull(),"creation did not show orange Pull arrow");
    };
    for(bool chamfer:{false,true}) {
        arm(chamfer);
        check(v.boxes_[0].shape.IsSame(original.shape) && !v.commandSession_->preview.IsNull(),"preview changed canonical geometry");
        const auto& creation=*v.commandSession_;
        const auto arrowMid=creation.pivot.Translated(gp_Vec(creation.direction)*(creation.value+15.5));
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
        auto& session=*v.commandSession_;
        const auto origin=session.pivot.Translated(gp_Vec(session.direction)*session.value);
        const auto midpoint=origin.Translated(gp_Vec(session.direction)*15.5);
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
void runEdgeBlendSessions() {EdgeBlendSessionTest::run();}

void runEdgeBlendFeatureSessions() {EdgeBlendSessionTest::features();}
