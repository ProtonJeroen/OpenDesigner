#include "CadViewport.h"
#include "MainWindow.h"
#include <QApplication>
#include <QAction>
#include <QComboBox>
#include <QDialog>
#include <QLineEdit>
#include <QMouseEvent>
#include <QTimer>
#include <QTreeWidget>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <GProp_GProps.hxx>
#include <TopExp_Explorer.hxx>
#include <StdSelect_BRepOwner.hxx>
#include <gp_Ax1.hxx>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void near(double a, double b, const char* message) {
    if (std::abs(a-b) > std::max(1.0e-6,std::abs(b)*1.0e-7)) throw std::runtime_error(message);
}
struct Fixture {
    MainWindow window;
    CadViewport* viewport;
    QTreeWidget* tree;
    QString status;
    Fixture() {
        window.show();
        QApplication::processEvents();
        viewport = window.findChild<CadViewport*>();
        tree = window.findChild<QTreeWidget*>();
        QObject::connect(viewport,&CadViewport::statusMessage,&window,[this](const QString& text) { status=text; });
    }
    QTreeWidgetItem* row(int index) { return tree->topLevelItem(0)->child(index); }
    QAction* action(const QString& text) {
        for (auto* action : window.findChildren<QAction*>())
            if (QString(action->text()).remove('&') == text) return action;
        throw std::runtime_error("menu action missing");
    }
};
}

struct BooleanOperationsTest {
    using Op = CadViewport::BooleanOperation;
    static double volume(const CadViewport& v, size_t index) {
        GProp_GProps props;
        BRepGProp::VolumeProperties(v.boxes_[index].shape,props);
        return props.Mass();
    }
    static gp_Pnt center(const CadViewport& v, size_t index) {
        return v.partLocalGizmoCenter(v.boxes_[index]).Transformed(v.boxes_[index].orientation);
    }
    static size_t active(const CadViewport& v) {
        size_t result=0;
        for (const auto& part : v.boxes_) if (part.active) ++result;
        return result;
    }
    static void overlap(Fixture& f) {
        auto& v=*f.viewport;
        v.addBox(); v.selectParts({0}); v.duplicateSelectedParts();
        v.selectParts({0,1});
    }
    static void checkResult(const CadViewport& v, size_t index, double expected) {
        require(v.isPartActive(index),"result inactive");
        const auto& part=v.boxes_[index];
        require(part.geometryKind == CadViewport::CadPart::GeometryKind::GeneralBRep,"result not GeneralBRep");
        require(!part.cylinderPrimitive && !part.spherePrimitive && !part.conePrimitive,"result retains primitive editing flags");
        require(BRepCheck_Analyzer(part.shape).IsValid(),"invalid result");
        near(volume(v,index),expected,"incorrect Boolean volume");
        require(part.presentation->Shape().IsSame(part.shape),"presentation not synchronized");
    }
    static void basicOperations() {
        for (const auto op : {Op::Unite,Op::Subtract,Op::Intersect}) {
            Fixture f; auto& v=*f.viewport; overlap(f);
            v.renamePart(0,QStringLiteral("Target renamed")); v.setPartColor(0,Qt::red);
            const auto source=v.boxes_[0];
            require(v.applyBoolean(op,{0,1},op==Op::Subtract ? std::optional<size_t>(0) : std::nullopt),"basic Boolean failed");
            const double expected=op==Op::Unite ? 600000 : op==Op::Subtract ? 120000 : 360000;
            checkResult(v,2,expected);
            require(active(v)==1 && !v.isPartActive(0) && !v.isPartActive(1),"sources not consumed");
            require(f.row(0)->isHidden() && f.row(1)->isHidden() && !f.row(2)->isHidden(),"browser did not synchronize replacement");
            require(v.selectedPartIndices_==std::vector<size_t>{2},"result not selected");
            const auto shape=v.boxes_[2].shape;
            v.undoPull();
            require(active(v)==2 && !v.isPartActive(2),"Boolean undo did not restore sources");
            require(v.boxes_[0].shape.IsSame(source.shape),"undo changed original geometry");
            require(v.partName(0)==source.name && v.partColor(0)==Qt::red,"undo lost source metadata");
            require(v.selectedPartIndices_==std::vector<size_t>({0,1}),"undo did not restore selection");
            require(!f.row(0)->isHidden() && f.row(2)->isHidden(),"browser undo failed");
            v.redoPull(); checkResult(v,2,expected);
            require(v.boxes_[2].shape.IsSame(shape),"redo recomputed a different B-Rep");
            require(f.row(2)->data(0,Qt::UserRole).toULongLong()==2,"browser slot changed");
        }
    }
    static void selectionAndDialog() {
        Fixture f; auto& v=*f.viewport; overlap(f);
        f.tree->clearSelection(); f.row(0)->setSelected(true); f.row(1)->setSelected(true);
        require(v.selectedBooleanParts()==std::vector<size_t>({0,1}),"browser selection not usable for Boolean");
        f.action(QStringLiteral("Unite"))->trigger(); checkResult(v,2,600000);
        v.undoPull();
        // A face selection is resolved to its owning part, without a part-mode requirement.
        v.selectedPartIndices_.clear(); v.context_->ClearSelected(Standard_False);
        for (size_t i=0;i<2;++i) {
            v.context_->Deactivate(v.boxes_[i].presentation);
            v.context_->Activate(v.boxes_[i].presentation,AIS_Shape::SelectionMode(TopAbs_FACE),Standard_False);
            TopExp_Explorer faces(v.boxes_[i].shape,TopAbs_FACE);
            Handle(StdSelect_BRepOwner) owner=new StdSelect_BRepOwner(faces.Current(),v.boxes_[i].presentation,5,Standard_True);
            v.context_->AddOrRemoveSelected(owner,Standard_False);
        }
        require(v.selectedBooleanParts().size()==2,"viewport face selection not resolved");
        f.action(QStringLiteral("Intersect"))->trigger(); checkResult(v,3,360000);
        v.undoPull();
        bool explicitChoice=false;
        QTimer::singleShot(0,[&]() {
            auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!dialog) return;
            auto* combo=dialog->findChild<QComboBox*>();
            explicitChoice=combo && combo->currentIndex()==-1;
            if (combo) combo->setCurrentIndex(1);
            dialog->accept();
        });
        f.action(QStringLiteral("Subtract..."))->trigger();
        require(explicitChoice,"Subtract did not require an explicit target");
        checkResult(v,4,120000);
        near(center(v,4).X(),112.5,"Subtract used the wrong target");
        v.undoPull();
        const auto slotCount=v.boxes_.size(), history=v.undoStack_.size();
        QTimer::singleShot(0,[]() {
            if (auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget())) dialog->reject();
        });
        v.subtractSelectedParts();
        require(v.boxes_.size()==slotCount && v.undoStack_.size()==history && active(v)==2,"cancel changed the document");
        QMouseEvent background(QEvent::MouseButtonPress,QPointF(-20,-20),QPointF(-20,-20),
                               Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
        v.mousePressEvent(&background);
        require(v.selectedBooleanParts().empty(),"background click left stale Boolean operands");
        v.uniteSelectedParts();
        require(active(v)==2 && v.boxes_.size()==slotCount,"background-click Boolean consumed stale parts");
    }
    static void transformedOperands() {
        for (const auto op : {Op::Unite,Op::Subtract,Op::Intersect}) {
            Fixture f; auto& v=*f.viewport; v.addBox(); v.addBox();
            v.applyBoxBounds(0,{0,0,0},{10,10,10}); v.applyBoxBounds(1,{0,0,0},{10,10,10});
            gp_Trsf rotation, translation, second;
            rotation.SetRotation(gp_Ax1(gp_Pnt(0,0,0),gp_Dir(0,0,1)),std::acos(-1.0)*0.5);
            translation.SetTranslation(gp_Vec(10,0,0)); second.SetTranslation(gp_Vec(5,0,0));
            v.applyPartState(0,v.boxes_[0].min,v.boxes_[0].max,translation*rotation);
            v.applyPartState(1,v.boxes_[1].min,v.boxes_[1].max,second);
            v.selectParts({0,1});
            const gp_Pnt original=center(v,0);
            require(v.applyBoolean(op,{0,1},op==Op::Subtract ? std::optional<size_t>(0) : std::nullopt),"transformed Boolean failed");
            checkResult(v,2,op==Op::Unite ? 1500 : 500);
            near(center(v,2).X(),op==Op::Subtract ? 2.5 : 7.5,"world transform not applied correctly");
            v.undoPull(); near(center(v,0).Distance(original),0,"undo lost original transform");
            v.redoPull(); checkResult(v,2,op==Op::Unite ? 1500 : 500);
            gp_Trsf move; move.SetTranslation(gp_Vec(15,20,30));
            const auto shape=v.boxes_[2].shape; const auto before=center(v,2);
            v.applyPartState(2,v.boxes_[2].min,v.boxes_[2].max,move);
            near(center(v,2).Distance(before.Translated(gp_Vec(15,20,30))),0,"result move failed");
            v.rotationIndex_=2; v.rotationBefore_=v.boxes_[2].orientation; v.rotationAxis_=1;
            v.beginCommandSession(CadViewport::CommandSession::Kind::Rotate,{2},
                gp_Dir(0,1,0),center(v,2));
            v.applyRotationAngle(37);
            v.dragDimension_->setText(QStringLiteral("37"));
            v.confirmCommandSession();
            near(center(v,2).Distance(before.Translated(gp_Vec(15,20,30))),0,"result rotate pivot moved");
            require(v.boxes_[2].shape.IsSame(shape),"result transformed by rebuilding geometry");
            v.setPartColor(2,Qt::blue); v.setPartVisible(2,false);
            require(!v.isPartVisible(2),"result hide failed"); v.setPartVisible(2,true);
            v.selectParts({2}); v.duplicateSelectedParts();
            checkResult(v,3,op==Op::Unite ? 1500 : 500);
            require(v.partColor(3)==Qt::blue,"duplicate lost result color");
            near(center(v,3).Distance(center(v,2).Translated(gp_Vec(25,0,0))),0,"result duplicate transform failed");
        }
    }
    static void allPrimitives() {
        for (int kind=0;kind<4;++kind) {
            for (const auto op : {Op::Unite,Op::Subtract,Op::Intersect}) {
                Fixture f; auto& v=*f.viewport; v.addBox();
                v.applyBoxBounds(0,{-100,-100,-100},{100,100,100});
                if (kind==0) v.addBox(); else if(kind==1) v.addCylinder();
                else if(kind==2) v.addSphere(); else v.addCone();
                gp_Trsf t;
                t.SetTranslation(gp_Vec(-125,0,0));
                v.applyPartState(1,v.boxes_[1].min,v.boxes_[1].max,t);
                const double toolVolume=volume(v,1);
                require(v.applyBoolean(op,{0,1},op==Op::Subtract ? std::optional<size_t>(0) : std::nullopt),"primitive Boolean failed");
                const double expected=op==Op::Unite ? 8000000 : op==Op::Subtract ? 8000000-toolVolume : toolVolume;
                checkResult(v,2,expected);
                v.undoPull(); require(active(v)==2,"primitive Boolean undo failed");
                v.redoPull(); checkResult(v,2,expected);
            }
        }
        Fixture f; auto& v=*f.viewport; overlap(f); v.addBox();
        gp_Trsf t; t.SetTranslation(gp_Vec(-200,0,0));
        v.applyPartState(2,v.boxes_[2].min,v.boxes_[2].max,t);
        require(v.applyBoolean(Op::Intersect,{0,1,2}),"three-part intersection failed");
        checkResult(v,3,240000);
        v.undoPull();
        require(v.applyBoolean(Op::Subtract,{0,1,2},0),"multi-tool cut failed");
        checkResult(v,4,120000);
        v.undoPull();
        require(v.applyBoolean(Op::Unite,{0,1,2}),"three-part fuse failed");
        checkResult(v,5,720000);
    }
    static void failuresAndHistory() {
        Fixture f; auto& v=*f.viewport; v.addBox(); v.addBox(); v.selectParts({0,1});
        const auto unchanged=[&]() {
            require(active(v)==2 && v.boxes_.size()==2 && v.undoStack_.empty(),"failed Boolean changed parts/history");
            require(!f.row(0)->isHidden() && !f.row(1)->isHidden(),"failed Boolean changed browser");
            require(v.selectedPartIndices_==std::vector<size_t>({0,1}),"failed Boolean changed selection");
            require(f.status.contains(QStringLiteral("kept")),"failure message not clear");
        };
        require(!v.applyBoolean(Op::Intersect,{0,1}),"disjoint intersection accepted"); unchanged();
        require(!v.applyBoolean(Op::Subtract,{0,1},0),"nonoverlapping cut accepted"); unchanged();
        require(!v.applyBoolean(Op::Subtract,{0,1}),"missing target accepted"); unchanged();
        require(!v.applyBoolean(Op::Unite,{0}),"one-part Boolean accepted"); unchanged();
        require(!v.applyBoolean(Op::Unite,{0,0}),"repeated operand accepted"); unchanged();
        require(!v.applyBoolean(Op::Unite,{0,99}),"invalid operand accepted"); unchanged();
        gp_Trsf t; t.SetTranslation(gp_Vec(-25,0,0));
        v.applyPartState(1,v.boxes_[1].min,v.boxes_[1].max,t);
        require(!v.applyBoolean(Op::Intersect,{0,1}),"face-only common accepted"); unchanged();
        t.SetTranslation(gp_Vec(-125,0,0));
        v.applyPartState(1,v.boxes_[1].min,v.boxes_[1].max,t);
        require(!v.applyBoolean(Op::Subtract,{0,1},0),"empty cut accepted"); unchanged();
        const auto source=v.boxes_[1].shape;
        v.boxes_[1].shape=BRepBuilderAPI_MakeEdge(gp_Pnt(0,0,0),gp_Pnt(1,0,0)).Shape();
        require(!v.applyBoolean(Op::Unite,{0,1}),"non-solid operand accepted"); unchanged();
        v.boxes_[1].shape=source;
        // Disconnected unions are one part containing multiple solids.
        t.SetTranslation(gp_Vec(0,0,0)); v.applyPartState(1,v.boxes_[1].min,v.boxes_[1].max,t);
        require(v.applyBoolean(Op::Unite,{0,1}),"disconnected union rejected");
        checkResult(v,2,960000);
        v.addBox();
        require(v.applyBoolean(Op::Unite,{2,3}),"GeneralBRep result could not be reused");
        checkResult(v,4,1440000);
        v.undoPull(); require(v.isPartActive(2) && v.isPartActive(3),"chained undo failed");
        v.undoPull(); require(v.isPartActive(0) && v.isPartActive(1) && v.isPartActive(3),"stable slots lost unrelated part");
        v.redoPull(); v.redoPull(); checkResult(v,4,1440000);
        // Consumed indices cannot be revived through visibility or selection APIs.
        v.setPartVisible(0,true); v.selectParts({0,4});
        require(!v.isPartVisible(0) && v.selectedPartIndices_==std::vector<size_t>{4},"consumed part can be selected");
    }
    static void generalBRepOperands() {
        for (const auto op : {Op::Unite,Op::Subtract,Op::Intersect}) {
            Fixture f; auto& v=*f.viewport; overlap(f);
            require(v.applyBoolean(Op::Unite,{0,1}),"GeneralBRep setup failed");
            v.addBox();
            gp_Trsf t; t.SetTranslation(gp_Vec(-325,0,0));
            v.applyPartState(3,v.boxes_[3].min,v.boxes_[3].max,t); // World box x=50..150.
            require(v.applyBoolean(op,{2,3},op==Op::Subtract ? std::optional<size_t>(2) : std::nullopt),
                    "Boolean could not consume an existing GeneralBRep");
            checkResult(v,4,op==Op::Unite ? 720000 : op==Op::Subtract ? 240000 : 360000);
            v.undoPull(); checkResult(v,2,600000);
            require(v.isPartActive(3),"GeneralBRep undo lost tool");
            v.redoPull();
            checkResult(v,4,op==Op::Unite ? 720000 : op==Op::Subtract ? 240000 : 360000);
        }
        Fixture f; auto& v=*f.viewport; overlap(f);
        v.setDirectMoveMode(true); v.selectParts({0,1});
        const auto xAxis=v.moveAxes_[0];
        v.undoPull(); // No commands yet: an empty Undo must not dismiss the gizmo.
        require(v.moveAxes_[0]==xAxis,"empty undo hid the transform gizmo");
    }
    static void hiddenAndMixedUndo() {
        Fixture f; auto& v=*f.viewport; overlap(f);
        v.setPartVisible(1,false);
        require(v.applyBoolean(Op::Unite,{0,1}),"hidden source Boolean failed");
        v.undoPull();
        require(v.isPartVisible(0) && !v.isPartVisible(1),"undo lost original visibility");
        v.redoPull();
        // A transform command follows the Boolean in the same history.
        auto& part=v.boxes_[2];
        CadViewport::PullChange transform{2,part.min,part.max,part.min,part.max,part.orientation,part.orientation};
        gp_Trsf move; move.SetTranslation(gp_Vec(10,20,30));
        transform.afterOrientation=move*part.orientation;
        v.applyPartState(2,part.min,part.max,transform.afterOrientation);
        v.undoStack_.push_back(transform);
        v.undoPull(); require(v.isPartActive(2),"transform undo removed Boolean result");
        v.undoPull(); require(v.isPartActive(0) && !v.isPartActive(2),"mixed Boolean undo failed");
        v.redoPull(); v.redoPull();
        near(center(v,2).X(),72.5,"mixed transform redo failed");
    }
    static void sessionBrowserAndBoolean() {
        Fixture f; auto& v=*f.viewport; overlap(f);
        const auto checkRows = [&](int expectedSlots = 2) {
            require(f.tree->topLevelItem(0)->childCount()==expectedSlots,"preview changed browser rows");
            require(f.row(0)->isSelected() && f.row(1)->isSelected(),"preview changed browser selection");
            require(!f.row(0)->isHidden() && !f.row(1)->isHidden(),"preview hid browser rows");
        };
        v.beginCommandSession(CadViewport::CommandSession::Kind::Move,{0,1},gp_Dir(1,0,0));
        v.previewCommand(40); checkRows();
        v.cancelCommandSession(); checkRows();
        v.beginCommandSession(CadViewport::CommandSession::Kind::Rotate,{0,1},gp_Dir(0,1,0),v.selectionCenter());
        v.previewCommand(23); checkRows();
        // Switching to Boolean discards the unconfirmed rotation before computing operands.
        v.uniteSelectedParts();
        require(!v.commandSession_,"Boolean kept pending transform session");
        require(v.undoStack_.size()==1,"Boolean committed the pending transform");
        checkResult(v,2,600000);
        v.undoPull(); checkRows(3);
        require(f.row(2)->isHidden(),"undone Boolean result row should remain inactive");
        near(center(v,0).Distance(gp_Pnt(50,40,30)),0,"Boolean switch retained preview transform");
        v.redoPull(); checkResult(v,2,600000);
    }
    static void run() {
        basicOperations(); selectionAndDialog(); transformedOperands(); allPrimitives();
        failuresAndHistory(); generalBRepOperands(); hiddenAndMixedUndo(); sessionBrowserAndBoolean();
    }
};

int main(int argc,char** argv) {
#if defined(Q_OS_LINUX)
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM") && !qEnvironmentVariableIsEmpty("DISPLAY"))
        qputenv("QT_QPA_PLATFORM",QByteArray("xcb"));
#endif
    QApplication app(argc,argv);
    try {
        BooleanOperationsTest::run();
        std::cout << "Boolean operations, browser selection, explicit target, failures, transforms and Undo/Redo passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
