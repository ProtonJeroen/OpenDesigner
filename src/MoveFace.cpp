#include "CadViewport.h"
#include "MoveFaceGeometry.h"
#include "CylindricalMoveGeometry.h"
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QSignalBlocker>
#include <QStandardItemModel>
#include <QVBoxLayout>
#include <QLineEdit>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QLocale>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRep_Tool.hxx>
#include <Poly_Triangulation.hxx>
#include <TopExp_Explorer.hxx>
#include <Prs3d_Drawer.hxx>
#include <Standard_Failure.hxx>
#include <StdSelect_BRepOwner.hxx>
#include <TopoDS.hxx>
#include <Quantity_Color.hxx>
#include <Graphic3d_ZLayerId.hxx>
#include <cmath>
#include <algorithm>

void CadViewport::initializeCylindricalMoveControls() {
    cylinderMoveLabel_=new QLabel(commandPanel_);
    cylinderMoveLabel_->setObjectName("cylinderMoveAxis");
    cylinderMoveDirection_=new QComboBox(commandPanel_);
    cylinderMoveDirection_->setObjectName("cylinderMoveDirection");
    cylinderMoveDirection_->addItems({tr("Choose lateral reference…"),tr("Part X projected into lateral plane"),
        tr("Part Y projected into lateral plane"),tr("Part Z projected into lateral plane")});
    cylinderMoveDirection_->setToolTip(tr("Choose a Part coordinate axis. Its perpendicular projection defines angle zero; the orange arrow shows the positive movement direction."));
    cylinderMoveAngle_=new QDoubleSpinBox(commandPanel_);
    cylinderMoveAngle_->setObjectName("cylinderMoveAngle");
    cylinderMoveAngle_->setRange(-360,360);cylinderMoveAngle_->setDecimals(6);
    cylinderMoveAngle_->setPrefix(tr("Direction angle: "));cylinderMoveAngle_->setSuffix(tr("°"));
    cylinderMoveAngle_->setToolTip(tr("Rotate the chosen reference in the lateral plane. Positive angles follow the right-hand rule about the displayed cylinder axis (Part coordinates)."));
    cylinderBossBoundary_=new QComboBox(commandPanel_);
    cylinderBossBoundary_->setObjectName("cylinderBossBoundary");
    cylinderBossBoundary_->addItems({tr("Extend — full round boss"),tr("Trim — original stock boundaries")});
    cylinderBossBoundary_->setToolTip(tr("Extend preserves the full boss and valid overhangs. Trim clips it against the original stock side surfaces. Invalid or disconnected results are refused."));
    for(QWidget* widget:std::array<QWidget*,4>{cylinderMoveLabel_,cylinderMoveDirection_,cylinderMoveAngle_,cylinderBossBoundary_}) {
        commandLayout_->addWidget(widget);widget->hide();
    }
    connect(cylinderMoveDirection_,qOverload<int>(&QComboBox::currentIndexChanged),this,[this]{updateCylindricalMoveParameters();});
    connect(cylinderMoveAngle_,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[this]{updateCylindricalMoveParameters();});
    connect(cylinderMoveAngle_->findChild<QLineEdit*>(),&QLineEdit::textEdited,this,[this]{
        if(!cylinderMoveAngle_->hasAcceptableInput() || (commandSession_&&!commandSession_->lateralDirectionReady))
            updateCylindricalMoveParameters();
    });
    connect(cylinderBossBoundary_,qOverload<int>(&QComboBox::currentIndexChanged),this,[this]{updateCylindricalMoveParameters();});
}

bool CadViewport::moveFaceReference(const TopoDS_Shape& shape,const TopoDS_Face& face,
        MoveFaceFeature& feature,gp_Pnt& center,gp_Dir& axis,QString& error) const {
    feature=MoveFaceFeature::Planar;
    if(face.IsNull()) {error=tr("Select one member face.");return false;}
    if(BRepAdaptor_Surface(face).GetType()!=GeomAbs_Cylinder)
        return MoveFaceGeometry::reference(shape,face,center,axis,error);
    // The unchanged geometry APIs certify recognition at zero displacement.
    auto reference=CylindricalMoveGeometry::moveThroughHole(shape,face,gp_Vec(0,0,0));
    if(reference) feature=MoveFaceFeature::ThroughHole;
    else {
        const QString holeError=QString::fromStdString(reference.message);
        reference=CylindricalMoveGeometry::moveBoss(shape,face,gp_Vec(0,0,0),CylindricalMoveGeometry::BossBoundaryMode::Extend);
        if(!reference) {
            const auto bossError=QString::fromStdString(reference.message);
            error=holeError==bossError ? tr("Move Face unavailable: %1").arg(holeError)
                : tr("Cylindrical surface recognized; Move Face reconstruction unavailable. Hole: %1 Boss: %2").arg(holeError,bossError);
            return false;
        }
        feature=MoveFaceFeature::Boss;
    }
    axis=reference.axis.Direction();
    center=reference.axis.Location().Translated(gp_Vec(axis)*(reference.height/2));
    error.clear();return true;
}

void CadViewport::updateCylindricalMoveParameters() {
    if(!commandSession_||commandSession_->kind!=CommandSession::Kind::MoveFace||
       commandSession_->moveFeature==MoveFaceFeature::Planar) return;
    auto& session=*commandSession_;
    draggingFace_=false;
    const int choice=cylinderMoveDirection_->currentIndex();
    session.lateralDirectionReady=false;
    cylinderMoveAngle_->setEnabled(choice>0);
    if(choice>=1&&choice<=3) {
        const gp_Vec reference(choice==1?1:0,choice==2?1:0,choice==3?1:0);
        const gp_Vec axis(session.cylinderAxisLocal);
        auto direction=reference-axis*reference.Dot(axis);
        if(direction.Magnitude()>1.e-8 && cylinderMoveAngle_->hasAcceptableInput()) {
            direction.Rotate(gp_Ax1(gp_Pnt(0,0,0),session.cylinderAxisLocal),cylinderMoveAngle_->value()*std::acos(-1.)/180.);
            session.direction=gp_Dir(direction.Transformed(session.faceBefore->orientation));
            session.lateralDirectionReady=true;
        }
    }
    if(session.moveFeature==MoveFaceFeature::Boss&&cylinderBossBoundary_->currentIndex()>=0)
        session.bossTrim=cylinderBossBoundary_->currentIndex()==1;
    if(!session.lateralDirectionReady&&!moveFaceArrow_.IsNull()) {
        context_->Remove(moveFaceArrow_,Standard_False);moveFaceArrow_.Nullify();
    }
    updateCommandParameter(false);
}

void CadViewport::moveFace() {
    endSketchEditing();
    cancelCommandSession();
    setSelectionMode(SelectionMode::Face);
    if (context_.IsNull()) return;
    movePartMode_=directMoveMode_=rotateMode_=false;
    emit transformModesChanged(false,false,false);
    hideFaceArrow(); hideMoveAxes(); hideRotationRings();
    moveFacePicking_=true;
    const auto selection=selectionContext();
    if(selection.kind==SelectionContext::Kind::Faces&&selection.faces.size()==1) {
        startMoveFace(selection.faces.front().partIndex,selection.faces.front().face);return;
    }
    emit statusMessage(tr("Move Face: select one planar face or a supported cylindrical hole, notch or boss wall."));
}
bool CadViewport::startMoveFace(size_t index, const TopoDS_Face& face) {
    if (!isPartVisible(index)) return false;
    const auto& part=boxes_[index];
    gp_Pnt center; gp_Dir normal; QString error;MoveFaceFeature feature;
    if (!moveFaceReference(part.shape,face,feature,center,normal,error)) {
        emit statusMessage(error); resetModelingTools(); return false;
    }
    const gp_Pnt worldCenter=center.Transformed(part.orientation);
    const gp_Dir worldNormal=normal.Transformed(part.orientation);
    hideFaceArrow(); hideMoveAxes(); hideRotationRings();
    beginCommandSession(CommandSession::Kind::MoveFace,{index},worldNormal,worldCenter);
    commandSession_->face=face;
    commandSession_->faceBefore=std::make_shared<CadPart>(part);
    commandSession_->moveFeature=feature;
    moveFacePicking_=false;
    commandPanel_->setWindowTitle(tr("Move Face Preview"));
    dragDimension_->setToolTip(tr("Signed face offset in mm; positive is outward"));
    if(feature==MoveFaceFeature::Planar) {
        commandButtons_->button(QDialogButtonBox::Ok)->setEnabled(true);
        displayPullArrow(moveFaceArrow_,worldCenter,gp_Vec(worldNormal));
        context_->SetZLayer(moveFaceArrow_,Graphic3d_ZLayerId_Topmost);
    } else {
        commandSession_->cylinderAxisLocal=normal;
        const QSignalBlocker directionBlock(cylinderMoveDirection_),angleBlock(cylinderMoveAngle_),policyBlock(cylinderBossBoundary_);
        cylinderMoveDirection_->setCurrentIndex(0);cylinderMoveAngle_->setValue(0);cylinderMoveAngle_->setEnabled(false);
        cylinderBossBoundary_->setCurrentIndex(0);
        cylinderMoveLabel_->setText(tr("%1 — axis in Part coordinates\n(%2, %3, %4)")
            .arg(feature==MoveFaceFeature::Boss?tr("Boss"):tr("Through-hole / open notch"))
            .arg(normal.X(),0,'g',9).arg(normal.Y(),0,'g',9).arg(normal.Z(),0,'g',9));
        auto* model=qobject_cast<QStandardItemModel*>(cylinderMoveDirection_->model());
        for(int i=1;i<=3;++i) {
            const gp_Vec ref(i==1?1:0,i==2?1:0,i==3?1:0);
            model->item(i)->setEnabled((ref-gp_Vec(normal)*ref.Dot(gp_Vec(normal))).Magnitude()>1.e-8);
        }
        cylinderMoveLabel_->show();cylinderMoveDirection_->show();cylinderMoveAngle_->show();
        cylinderBossBoundary_->setVisible(feature==MoveFaceFeature::Boss);
        dragDimension_->setToolTip(tr("Signed displacement in mm along the orange lateral arrow. Choose a lateral reference and direction angle first."));
        commandSession_->valid=false;commandButtons_->button(QDialogButtonBox::Ok)->setEnabled(false);
        showCommandPanel(commandPanel_);
    }
    context_->MoveTo(-1,-1,view_,Standard_False);
    view_->Redraw();
    commandPanel_->activateWindow();
    if(feature==MoveFaceFeature::Planar) {dragDimension_->setFocus();dragDimension_->selectAll();}
    else cylinderMoveDirection_->setFocus();
    emit statusMessage(feature==MoveFaceFeature::Planar
        ? tr("Move Face: drag the normal arrow or type signed mm. OK/Enter applies; Cancel/Esc discards.")
        : tr("Move Face: choose a lateral reference and angle, then drag the orange arrow or type signed mm. Cancel/Esc discards."));
    return true;
}
void CadViewport::redrawFacePreview() {
    // Replacing a retained scene presentation must invalidate the main render
    // buffer, not just the immediate (manipulator/highlight) layer.
    context_->UpdateCurrentViewer();
    view_->Invalidate();
    view_->Redraw();
}
Handle(AIS_Shape) CadViewport::createFacePreview(const CadPart& before,const TopoDS_Shape& candidate,QString& error) {
    Handle(AIS_Shape) preview;
    try {
        // Reconstructed B-Reps are copied without display meshes. Complete
        // the shaded representation before erasing the original object.
        BRepMesh_IncrementalMesh mesh(candidate,0.1,false,0.35,false);
        bool meshed=mesh.IsDone();
        for(TopExp_Explorer f(candidate,TopAbs_FACE);f.More()&&meshed;f.Next()) {
            TopLoc_Location location;
            const auto triangulation=BRep_Tool::Triangulation(TopoDS::Face(f.Current()),location);
            meshed=!triangulation.IsNull() && triangulation->NbTriangles()>0;
        }
        if(!meshed) throw Standard_Failure("The reconstructed solid could not be tessellated for preview.");
        preview=new AIS_Shape(candidate);
        preview->SetLocalTransformation(before.orientation);
        preview->Attributes()->SetAutoTriangulation(true);
        preview->SetColor(Quantity_Color(0.15,0.65,1.0,Quantity_TOC_RGB));
        preview->SetTransparency(0.25);
        preview->SetDisplayMode(AIS_Shaded);
        // Select mode -1: previews never participate in operand picking.
        context_->Display(preview,AIS_Shaded,-1,Standard_False);
        context_->Redisplay(preview,Standard_False);
        return preview;
    } catch(const Standard_Failure& failure) {
        error=tr("Cannot display the reconstructed preview: %1").arg(QString::fromUtf8(failure.GetMessageString()));
        if(!preview.IsNull()) context_->Remove(preview,Standard_False);
        return {};
    }
}
bool CadViewport::presentFacePreview(const CadPart& before,bool changed,QString& error) {
    auto& session=*commandSession_;
    if(!session.preview.IsNull()) {
        context_->Remove(session.preview,Standard_False);
        session.preview.Nullify();
    }
    if(session.valid && changed) {
        session.preview=createFacePreview(before,session.candidate,error);
        session.valid=!session.preview.IsNull();
        if(session.valid) context_->Erase(before.presentation,Standard_False);
    }
    if(!session.valid || !changed) {
        context_->Display(before.presentation,Standard_False);
        context_->Redisplay(before.presentation,Standard_False);
    }
    if(!session.valid) {
        session.candidate.Nullify();
        session.changes.front().faceChange.reset();
    }
    commandButtons_->button(QDialogButtonBox::Ok)->setEnabled(session.valid);
    return session.valid;
}
bool CadViewport::previewMoveFace(double value) {
    auto& session=*commandSession_;
    session.value=value;
    session.geometryError.clear();
    const auto& before=*session.faceBefore;
    QString error;
    const double localDistance=value/std::abs(before.orientation.ScaleFactor());
    TopoDS_Shape candidate;
    if(session.moveFeature==MoveFaceFeature::Planar) candidate=MoveFaceGeometry::move(before.shape,session.face,localDistance,error);
    else if(!session.lateralDirectionReady||!cylinderMoveAngle_->hasAcceptableInput())
        error=tr("Choose a reference with a nonzero lateral projection and a valid direction angle.");
    else if(session.moveFeature==MoveFaceFeature::Boss&&cylinderBossBoundary_->currentIndex()<0)
        error=tr("Choose Trim or Extend explicitly.");
    else {
        auto localDelta=(gp_Vec(session.direction)*value).Transformed(before.orientation.Inverted());
        const auto moved=session.moveFeature==MoveFaceFeature::Boss
            ? CylindricalMoveGeometry::moveBoss(before.shape,session.face,localDelta,session.bossTrim
                ? CylindricalMoveGeometry::BossBoundaryMode::Trim:CylindricalMoveGeometry::BossBoundaryMode::Extend)
            : CylindricalMoveGeometry::moveThroughHole(before.shape,session.face,localDelta);
        candidate=moved.shape;
        if(!moved) error=QString::fromStdString(moved.message);
    }
    session.valid=!candidate.IsNull();
    session.candidate=candidate;
    commandButtons_->button(QDialogButtonBox::Ok)->setEnabled(session.valid);
    auto& change=session.changes.front();
    change.faceChange.reset();
    if (session.valid && std::abs(value)>=1.e-7) {
        CadPart after=before;
        after.shape=candidate;
        // Directly edited topology must not subsequently be rebuilt by Primitive Pull.
        after.geometryKind=CadPart::GeometryKind::GeneralBRep;
        after.cylinderPrimitive=after.spherePrimitive=after.conePrimitive=false;
        Bnd_Box bounds;
        BRepBndLib::AddOptimal(candidate,bounds,false,false);
        bounds.Get(after.min[0],after.min[1],after.min[2],after.max[0],after.max[1],after.max[2]);
        after.baseMin=after.min; after.baseMax=after.max;
        change.afterMin=after.min; change.afterMax=after.max;
        change.faceChange=std::make_shared<FaceChange>(FaceChange{before,after});
        displayPullArrow(moveFaceArrow_,session.pivot.Translated(gp_Vec(session.direction)*value),gp_Vec(session.direction));
    } else if(session.moveFeature==MoveFaceFeature::Planar||session.lateralDirectionReady) {
        displayPullArrow(moveFaceArrow_,session.pivot,gp_Vec(session.direction));
    }
    if(!moveFaceArrow_.IsNull()) context_->SetZLayer(moveFaceArrow_,Graphic3d_ZLayerId_Topmost);
    presentFacePreview(before,std::abs(value)>=1.e-7,error);
    session.state=session.valid ? CommandSession::State::Preview : CommandSession::State::Parameters;
    if (!session.valid) reportGeometryFailure(tr("Move Face"),error,false);
    redrawFacePreview();
    return session.valid;
}
bool CadViewport::beginMoveFaceDrag(const QPoint& point) {
    if (!commandSession_ || commandSession_->kind!=CommandSession::Kind::MoveFace) return false;
    const auto& session=*commandSession_;
    if(session.moveFeature!=MoveFaceFeature::Planar&&!session.lateralDirectionReady) return false;
    const gp_Pnt origin=session.pivot.Translated(gp_Vec(session.direction)*(session.valid?session.value:0));
    const auto tip=origin.Translated(gp_Vec(session.direction)*31);
    Standard_Integer x0,y0,x1,y1;
    view_->Convert(origin.X(),origin.Y(),origin.Z(),x0,y0);
    view_->Convert(tip.X(),tip.Y(),tip.Z(),x1,y1);
    const double dx=x1-x0,dy=y1-y0,len2=dx*dx+dy*dy;
    if (len2<4) return false;
    const double t=std::clamp(((point.x()-x0)*dx+(point.y()-y0)*dy)/len2,0.0,1.0);
    if (std::hypot(point.x()-x0-t*dx,point.y()-y0-t*dy)>12) return false;
    draggingFace_=true; dragStart_=point;
    moveFaceDragStart_=session.valid?session.value:0;
    dragScreenX_=dx/std::sqrt(len2); dragScreenY_=dy/std::sqrt(len2);
    dragMmPerPixel_=31/std::sqrt(len2);
    return true;
}
void CadViewport::applyFaceState(size_t index, const CadPart& state) {
    auto& part=boxes_[index];
    context_->ClearSelected(Standard_False); // Owners from replaced topology must not be reused.
    part.shape=state.shape; part.geometryKind=state.geometryKind;
    part.persistentId=state.persistentId; part.name=state.name; part.color=state.color;
    part.visible=state.visible;
    part.min=state.min; part.max=state.max; part.baseMin=state.baseMin; part.baseMax=state.baseMax;
    part.orientation=state.orientation;
    part.cylinderPrimitive=state.cylinderPrimitive; part.spherePrimitive=state.spherePrimitive;
    part.conePrimitive=state.conePrimitive; part.cone=state.cone;
    part.sphereRadius=state.sphereRadius; part.cylinderRadius=state.cylinderRadius;
    part.cylinderHeight=state.cylinderHeight; part.cylinderBaseOffset=state.cylinderBaseOffset;
    part.presentation->SetShape(part.shape);
    part.presentation->SetLocalTransformation(part.orientation);
    context_->Redisplay(part.presentation,Standard_False);
    context_->Deactivate(part.presentation);
    if (part.visible) context_->Activate(part.presentation,AIS_Shape::SelectionMode(TopAbs_FACE),Standard_False);
}
