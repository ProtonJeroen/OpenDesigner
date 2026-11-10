#include "CadViewport.h"
#include <QApplication>
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
    };
    for(bool chamfer:{false,true}) {
        arm(chamfer);
        check(v.boxes_[0].shape.IsSame(original.shape) && !v.commandSession_->preview.IsNull(),"preview changed canonical geometry");
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
};
void runEdgeBlendSessions() {EdgeBlendSessionTest::run();}
