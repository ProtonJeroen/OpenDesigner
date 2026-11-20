#include "BRepTiming.h"
#ifdef OPENDESIGNER_ENABLE_BOX_SHADOW
#include "BoxShadowObservation.h"
#endif
#include <BinTools.hxx>
#include <QDataStream>
#include <sstream>
#include "CadViewport.h"
#include "MoveFaceGeometry.h"
#include "BRepProvisionalProviders.h"
#include "CylindricalMoveGeometry.h"
#include <Adaptor3d_Curve.hxx>
#include <QComboBox>
#include <QCheckBox>
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

// Lossless OCCT binary geometry, without display triangulation. Byte equality
// (not a hash, volume, pointer, or rounded dimensions) detects in-place edits.
// Any serialization uncertainty disables reuse; no persistent cache is created.
QByteArray CadViewport::movePartSnapshot(const CadPart& part) const {
    BRepTiming::Scope timingScope("MoveFace.movePartSnapshot");
    try {
        if(part.shape.IsNull()||!part.blends.features.empty()||!part.blends.sharp.IsNull()||!part.blends.certified.IsNull()) return {};
        std::ostringstream binary(std::ios::out|std::ios::binary);
        BinTools::Write(part.shape,binary,false,false,BinTools_FormatVersion_CURRENT);
        if(!binary.good()) return {};
        const auto geometry=binary.str();QByteArray result;QDataStream out(&result,QIODevice::WriteOnly);
        out.setFloatingPointPrecision(QDataStream::DoublePrecision);
        out<<QByteArray(geometry.data(),qsizetype(geometry.size()))<<part.persistentId<<part.name<<part.color
           <<part.visible<<part.active<<qint32(part.geometryKind)<<part.cylinderPrimitive<<part.spherePrimitive<<part.conePrimitive;
        for(const auto& bounds:{part.min,part.max,part.baseMin,part.baseMax}) for(double value:bounds) out<<value;
        for(int row=1;row<=3;++row) for(int col=1;col<=4;++col) out<<part.orientation.Value(row,col);
        out<<qint32(part.orientation.Form())<<part.orientation.ScaleFactor();
        if(part.presentation.IsNull()) return {};
        const auto displayed=part.presentation->LocalTransformation();
        for(int row=1;row<=3;++row) for(int col=1;col<=4;++col) out<<displayed.Value(row,col);
        out<<qint32(displayed.Form())<<displayed.ScaleFactor();
        out<<part.cone.bottomRadius<<part.cone.topRadius<<part.cone.height<<part.cone.baseOffset
           <<part.sphereRadius<<part.cylinderRadius<<part.cylinderHeight<<part.cylinderBaseOffset;
        return out.status()==QDataStream::Ok?result:QByteArray{};
    } catch(...) {return {};}
}
QByteArray CadViewport::moveInputSnapshot(double value) const {
    const auto& s=*commandSession_;
    if(s.kind!=CommandSession::Kind::MoveFace||!s.allowEllipticalCapMove||s.moveFeature!=MoveFaceFeature::Planar||s.blendSupport
       ||s.directionSource!=MoveDirectionSource::FaceNormal||!s.directionReferences.empty()
       ||!s.blendSource.features.empty()||!s.blendSource.sharp.IsNull()||!s.blendSource.certified.IsNull()) return {};
    QByteArray result;QDataStream out(&result,QIODevice::WriteOnly);
    out<<value<<s.directionReverse<<s.allowEllipticalCapMove<<keepBlends_->isChecked()<<s.bossTrim;
    for(const auto& vector:{s.direction,s.faceNormal}) out<<vector.X()<<vector.Y()<<vector.Z();
    out<<s.pivot.X()<<s.pivot.Y()<<s.pivot.Z();
    return out.status()==QDataStream::Ok?result:QByteArray{};
}
bool CadViewport::moveSourceUnchanged() const {
    const auto& s=*commandSession_;
    if(s.moveSourceSnapshot.isEmpty()||s.moveSourceInvalidated||!s.faceBefore||s.changes.size()!=1||!isPartVisible(s.changes.front().partIndex)) return false;
    const auto& current=boxes_[s.changes.front().partIndex];
    if(current.presentation!=s.faceBefore->presentation||!current.shape.IsEqual(s.faceBefore->shape)) return false;
    const auto now=movePartSnapshot(current);
    return !s.moveSourceSnapshot.isEmpty()&&!now.isEmpty()&&now==s.moveSourceSnapshot
        &&movePartSnapshot(*s.faceBefore)==s.moveSourceSnapshot;
}
bool CadViewport::moveProvisionalSelectionUnchanged() const {
    if(!commandSession_||commandSession_->moveSourceInvalidated||commandSession_->moveNumericContextInvalidated)return false;
    const auto& s=*commandSession_;
    if(s.changes.size()!=1||selectedPartIndices_!=s.selection)return false;
    std::vector<Handle(SelectMgr_EntityOwner)> owners;
    for(context_->InitSelected();context_->MoreSelected();context_->NextSelected())owners.push_back(context_->SelectedOwner());
    const bool published=s.provisional.has_value();
    if(owners!=(published?s.provisional->owners:s.owners))return false;
    if(published&&!s.face.IsEqual(s.provisional->face))return false;
    // Hiding the source AIS removes its selected owners. Only a previously
    // validated publication may bind that renderer-generated empty selection.
    if(owners.empty())return published;
    const auto selected=selectionContext();
    return selected.faces.size()==1&&selected.faces.front().partIndex==s.changes.front().partIndex
        &&selected.faces.front().face.IsEqual(s.face);
}
void CadViewport::certifyMovePreview(double value) {
    BRepTiming::Scope timingScope("MoveFace.capturePreviewCertificate");
    auto& s=*commandSession_;s.movePreviewCertificate.reset();
    if(!s.valid||s.state!=CommandSession::State::Preview||std::abs(value)<1.e-7||s.candidate.IsNull()
       ||s.preview.IsNull()||s.changes.size()!=1||!s.changes.front().faceChange||!moveSourceUnchanged()) return;
    const auto inputs=moveInputSnapshot(value);if(inputs.isEmpty()) return;
    const auto change=s.changes.front().faceChange;
    if(!s.candidate.IsEqual(change->after.shape)) return;
    const auto after=movePartSnapshot(change->after);if(after.isEmpty()) return;
    CommandSession::MovePreviewCertificate proof{s.face,s.candidate,change,inputs,after,{},selectedPartIndices_};
    for(context_->InitSelected();context_->MoreSelected();context_->NextSelected()) proof.owners.push_back(context_->SelectedOwner());
    s.movePreviewCertificate=std::move(proof);
}
bool CadViewport::reuseMovePreview(double value) {
    BRepTiming::Scope timingScope("MoveFace.reuseMovePreview");
    auto& s=*commandSession_;
    if(!s.movePreviewCertificate) return false;
    const auto proof=std::move(*s.movePreviewCertificate);s.movePreviewCertificate.reset();
    if(!s.valid||s.value!=value||s.state!=CommandSession::State::Preview||!s.geometryError.isEmpty()
       ||!s.face.IsEqual(proof.face)||!s.candidate.IsEqual(proof.candidate)||!moveSourceUnchanged()
       ||s.changes.front().faceChange!=proof.change||selectedPartIndices_!=proof.selection
       ||!proof.change->before.shape.IsEqual(s.faceBefore->shape)
       ||proof.change->before.presentation!=s.faceBefore->presentation||proof.change->after.presentation!=s.faceBefore->presentation
       ||movePartSnapshot(proof.change->before)!=s.moveSourceSnapshot
       ||moveInputSnapshot(value)!=proof.inputs||movePartSnapshot(proof.change->after)!=proof.after
       ||!s.candidate.IsEqual(proof.change->after.shape)||s.preview.IsNull()||!s.preview->Shape().IsEqual(s.candidate)) return false;
    std::vector<Handle(SelectMgr_EntityOwner)> owners;
    for(context_->InitSelected();context_->MoreSelected();context_->NextSelected()) owners.push_back(context_->SelectedOwner());
    if(owners!=proof.owners) return false;
    // Single GUI-thread transaction: no event-loop yield between these guards
    // and closeCommandSession, which consumes the existing certified FaceChange.
    BRepTiming::Scope reused("MoveFace.certifiedPreviewReused");return true;
}

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
    BRepTiming::Scope timingScope("MoveFace.CadViewport::moveFaceReference");
    feature=MoveFaceFeature::Planar;
    if(face.IsNull()) {error=tr("Select one member face.");return false;}
    if(BRepAdaptor_Surface(face).GetType()==GeomAbs_Cone) {
        error=tr("Move Face cannot translate an ordinary conical wall or an uncertified conical transition. Select a planar cap; cone primitives also support Pull / Resize.");return false;
    }
    if(BRepAdaptor_Surface(face).GetType()==GeomAbs_Torus) {
        error=tr("Move Face: this toroidal transition has no certified Fillet recipe or sharp source. Original geometry was kept.");return false;
    }
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
    BRepTiming::Scope timingScope("MoveFace.CadViewport::moveFace");
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
bool CadViewport::startMoveFace(size_t index, const TopoDS_Face& face,bool allowEllipticalCapMove) {
    BRepTiming::Scope timingScope("MoveFace.CadViewport::startMoveFace");
    if (!isPartVisible(index)) return false;
    const auto& part=boxes_[index];
    auto recipe=part.blends;QString recipeError;
    const bool recognized=EdgeBlendFeatures::resolve(part.shape,recipe,recipeError);
    if(recognized&&EdgeBlendFeatures::selected(recipe,face)>=0) return startEditEdgeBlend(index,face);
    if(recognized&&BRepAdaptor_Surface(face).GetType()!=GeomAbs_Plane
       &&std::any_of(recipe.features.begin(),recipe.features.end(),[](const auto& f){return EdgeBlendFeatures::circular(f);})) {
        emit statusMessage(BRepAdaptor_Surface(face).GetType()==GeomAbs_Cone
            ?tr("Conical support edits with circular Fillets/Chamfers are not yet certified; move a planar cap or remove the transition first.")
            :tr("Cylindrical support edits with circular Fillets are not yet certified; remove the Fillet first."));return false;
    }
    if(!recognized&&!part.blends.features.empty()) {emit statusMessage(recipeError);return false;}
    gp_Pnt center; gp_Dir normal; QString error;MoveFaceFeature feature=MoveFaceFeature::Planar;
    const bool splineMoveReference=allowEllipticalCapMove&&!face.IsNull()
        &&BRepAdaptor_Surface(face).GetType()==GeomAbs_Plane;
    const bool referenceReady=splineMoveReference
        ? MoveFaceGeometry::referenceForMove(part.shape,face,center,normal,error)
        : moveFaceReference(part.shape,face,feature,center,normal,error);
    if (!referenceReady) {
        emit statusMessage(error); resetModelingTools(); return false;
    }
    const gp_Pnt worldCenter=center.Transformed(part.orientation);
    const gp_Dir worldNormal=normal.Transformed(part.orientation);
    hideFaceArrow(); hideMoveAxes(); hideRotationRings();
    beginCommandSession(CommandSession::Kind::MoveFace,{index},worldNormal,worldCenter);
    commandSession_->face=face;
    commandSession_->provisionalPreviewEnabled=qEnvironmentVariableIntValue("OPENDESIGNER_ENABLE_PROVISIONAL_BOX_PREVIEW")==1;
    commandSession_->allowEllipticalCapMove=allowEllipticalCapMove;
    commandSession_->faceBefore=std::make_shared<CadPart>(part);
    commandSession_->moveFeature=feature;
    commandSession_->faceNormal=worldNormal;
    commandSession_->blendSource=recipe;
    commandSession_->blendSupport=recognized&&feature==MoveFaceFeature::Planar;
    if(feature==MoveFaceFeature::Planar&&!commandSession_->blendSupport&&allowEllipticalCapMove)
        commandSession_->moveSourceSnapshot=movePartSnapshot(part);
    moveFacePicking_=false;moveDirectionPicking_=false;updateMoveDirectionControls();
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
namespace {
struct TimedMesh {Handle(Poly_Triangulation) mesh;int nodes=0,triangles=0;};
std::vector<TimedMesh> timedMeshes(const TopoDS_Shape& shape) {
    std::vector<TimedMesh> result;
    if(!BRepTiming::enabled||!BRepTiming::detailed||shape.IsNull())return result;
    for(TopExp_Explorer f(shape,TopAbs_FACE);f.More();f.Next()) {
        TopLoc_Location location;auto mesh=BRep_Tool::Triangulation(TopoDS::Face(f.Current()),location);
        result.push_back({mesh,mesh.IsNull()?0:mesh->NbNodes(),mesh.IsNull()?0:mesh->NbTriangles()});
    }
    return result;
}
void reportMeshChange(const char* label,const TopoDS_Shape& shape,const std::vector<TimedMesh>& before) {
    if(!BRepTiming::enabled||!BRepTiming::detailed)return;
    auto after=timedMeshes(shape);int changed=0;
    for(size_t i=0;i<after.size();++i)
        if(i>=before.size()||after[i].mesh!=before[i].mesh||after[i].nodes!=before[i].nodes||after[i].triangles!=before[i].triangles)++changed;
    BRepTiming::Event event(label,shape);event.finish({},changed,int(after.size()));
}
}
void CadViewport::redrawFacePreview() {
    BRepTiming::Scope timingScope("MoveFace.CadViewport::redrawFacePreview");
    // Replacing a retained scene presentation must invalidate the main render
    // buffer, not just the immediate (manipulator/highlight) layer.
    const auto candidate=commandSession_?commandSession_->candidate:TopoDS_Shape{};
    const auto meshes=timedMeshes(candidate);
    {BRepTiming::Event event("Preview.UpdateCurrentViewer");context_->UpdateCurrentViewer();event.finish();}
    {BRepTiming::Event event("Preview.Invalidate");view_->Invalidate();event.finish();}
    {BRepTiming::Event event("Preview.Redraw");view_->Redraw();event.finish();}
    reportMeshChange("Preview.MeshChangedDuringRedraw",candidate,meshes);
}
Handle(AIS_Shape) CadViewport::createFacePreview(const CadPart& before,const TopoDS_Shape& candidate,QString& error) {
    BRepTiming::Scope timingScope("Handle(AIS_Shape) CadViewport::createFacePreview");
    Handle(AIS_Shape) preview;
    try {
        // Reconstructed B-Reps are copied without display meshes. Complete
        // the shaded representation before erasing the original object.
        if(BRepTiming::enabled&&BRepTiming::detailed) {
            auto before=timedMeshes(candidate);int present=0;
            for(const auto& mesh:before)present+=!mesh.mesh.IsNull();
            BRepTiming::Event inventory("Preview.MeshBefore",candidate);inventory.finish({},present,int(before.size()));
        }
        BRepTiming::Event meshEvent("Preview.Mesh",candidate);
        BRepMesh_IncrementalMesh mesh(candidate,0.1,false,0.35,false);
        bool meshed=mesh.IsDone();meshEvent.finish(candidate,meshed?1:0);
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
        BRepTiming::Event displayEvent("Preview.AIS",candidate);
        const auto meshes=timedMeshes(candidate);
        {BRepTiming::Event event("Preview.AIS.Display",candidate);context_->Display(preview,AIS_Shaded,-1,Standard_False);event.finish();}
        {BRepTiming::Event event("Preview.AIS.Redisplay",candidate);context_->Redisplay(preview,Standard_False);event.finish();}
        displayEvent.finish(candidate);
        reportMeshChange("Preview.MeshChangedDuringAIS",candidate,meshes);
        return preview;
    } catch(const Standard_Failure& failure) {
        error=tr("Cannot display the reconstructed preview: %1").arg(QString::fromUtf8(failure.GetMessageString()));
        if(!preview.IsNull()) context_->Remove(preview,Standard_False);
        return {};
    }
}
bool CadViewport::presentFacePreview(const CadPart& before,bool changed,QString& error) {
    BRepTiming::Scope timingScope("bool CadViewport::presentFacePreview");
    auto& session=*commandSession_;
    if(session.provisional)session.provisional->candidate.Nullify();
    if(session.movePreviewPhase==CommandSession::MovePreviewPhase::Provisional)
        session.movePreviewPhase=CommandSession::MovePreviewPhase::Idle;
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
    if(session.provisionalPreviewEnabled&&session.provisional) {
        session.provisional->owners.clear();
        for(context_->InitSelected();context_->MoreSelected();context_->NextSelected())session.provisional->owners.push_back(context_->SelectedOwner());
    }
    commandButtons_->button(QDialogButtonBox::Ok)->setEnabled(session.valid);
    return session.valid;
}
bool CadViewport::previewMoveFace(double value) {
#ifdef OPENDESIGNER_ENABLE_BOX_SHADOW
    const auto shadowStarted=std::chrono::steady_clock::now();
#endif
    BRepTiming::Scope timingScope("MoveFace.CadViewport::previewMoveFace");
    auto& session=*commandSession_;
    const auto evaluationRevision=moveNumericRevision_;
    const auto evaluationBefore=session.faceBefore;
    const auto evaluationFace=session.face;
    if(session.moveNumericContextInvalidated) {invalidateMoveNumericContext();return false;}
    session.movePreviewCertificate.reset();
    if(!session.moveSourceSnapshot.isEmpty()&&!moveSourceUnchanged()) {
        session.valid=false;session.state=CommandSession::State::Parameters;
        session.candidate.Nullify();session.changes.front().faceChange.reset();
        QString error=tr("The source Part changed during Move Face. Cancel and select the Face again.");
        presentFacePreview(*session.faceBefore,false,error);
        if(session.provisionalPreviewEnabled)session.movePreviewPhase=CommandSession::MovePreviewPhase::Failed;
        reportGeometryFailure(tr("Move Face"),error,false);return false;
    }
#ifdef OPENDESIGNER_ENABLE_BOX_SHADOW
    // Disabled-by-default development observer. No verdict enters session state.
    BoxShadowObservation::Trace shadowTrace(evaluationBefore->shape,evaluationFace,
        bool(evaluationBefore)&&session.moveFeature==MoveFaceFeature::Planar&&!session.blendSupport
            &&session.directionSource==MoveDirectionSource::FaceNormal,shadowStarted);
#endif
    session.value=value;
    session.geometryError.clear();
    const auto& before=*evaluationBefore;
    const auto evaluationBlends=session.blendSource;
    QString error;
    // v0.5.3: a planar Face translated by value*d is the same solid as a normal
    // offset by value*(d.n); the certified planar path is reused unchanged.
    double normalFactor=1;bool directionReady=true;
    if(session.moveFeature==MoveFaceFeature::Planar) {
        gp_Dir direction;double length=0;directionReady=moveFaceDirection(session,direction,normalFactor,error,&length);
        if(directionReady) session.direction=direction;
        // v0.5.4 By Points: the displacement is B - A; its length replaces the
        // typed distance, so the normal offset is exactly (B - A) . n.
        if(session.directionSource==MoveDirectionSource::ByPoints) {value=directionReady?length:0;session.value=value;}
    }
    // The initial spline capability is axial translation, not the generic
    // planar command's normal projection of an oblique reference direction.
    if(directionReady&&session.moveFeature==MoveFaceFeature::Planar&&std::abs(std::abs(normalFactor)-1)>1.e-8) {
        for(TopExp_Explorer it(before.shape,TopAbs_FACE);it.More();it.Next()) {
            const BRepAdaptor_Surface support(TopoDS::Face(it.Current()));
            if(support.GetType()==GeomAbs_BSplineSurface
               ||(support.GetType()==GeomAbs_SurfaceOfExtrusion&&support.BasisCurve()->GetType()==GeomAbs_BSplineCurve)) {
                directionReady=false;error=tr("Spline cap Move requires an axial direction perpendicular to the selected cap.");break;
            }
        }
    }
    const double localDistance=value*normalFactor/std::abs(before.orientation.ScaleFactor());
    // Display-only branch. OK uses forceCertifiedMovePreview_ to bypass it,
    // and provisional geometry is never installed as a FaceChange or receipt.
    // F2.9F.1: the session states only the request (actual B-Rep, Face,
    // placement, operation parameters); provider choice and source admission
    // belong to the shared negotiation layer.
    namespace Provisional=BRepReconstruction::Provisional;
    std::optional<Provisional::PlanarMoveRequest> provisionalRequest;
    if(session.provisionalPreviewEnabled&&!forceCertifiedMovePreview_&&directionReady&&!session.moveSourceSnapshot.isEmpty()) {
        provisionalRequest.emplace();
        auto& request=*provisionalRequest;
        request.source=before.shape;request.face=evaluationFace;
        request.placement=before.orientation;request.displayed=before.presentation->LocalTransformation();
        request.planarFaceMove=session.moveFeature==MoveFaceFeature::Planar;
        request.blendFeatures=session.blendSupport;
        request.alongOutwardNormal=session.directionSource==MoveDirectionSource::FaceNormal&&!session.directionReverse
            &&session.directionReferences.empty();
        request.invariantCapPolicy=session.allowEllipticalCapMove;
        if(Provisional::admitOperation(request)==Provisional::Admission::Unsupported)provisionalRequest.reset();
    }
    if(provisionalRequest) {
        BRepTiming::Scope provisionalTimer("MoveFace.BoxProvisional.Total");
        if(!moveProvisionalSelectionUnchanged()) {invalidateMoveNumericContext();return false;}
        const auto provisional=Provisional::negotiatePlanarMove(*provisionalRequest,localDistance);
        if(provisional.outcome!=Provisional::Outcome::CertifiedOnly) {
            if(provisional.outcome==Provisional::Outcome::Failed)error=provisional.reason;
            session.valid=false;session.candidate.Nullify();session.changes.front().faceChange.reset();
            session.movePreviewCertificate.reset();
            try {
                BRepTiming::Scope publication("MoveFace.BoxProvisional.Publication");
                presentFacePreview(before,false,error);
                if(provisional.outcome==Provisional::Outcome::Failed)throw Standard_Failure(error.toUtf8().constData());
                if(!commandSession_||commandSession_->faceBefore!=evaluationBefore||moveNumericRevision_!=evaluationRevision
                   ||!moveSourceUnchanged()||!moveProvisionalSelectionUnchanged())throw Standard_Failure("Move Face binding changed during provisional construction.");
                session.preview=createFacePreview(before,provisional.shape,error);
                if(session.preview.IsNull())throw Standard_Failure(error.toUtf8().constData());
                context_->Erase(before.presentation,Standard_False);
                session.provisional=CommandSession::ProvisionalBinding{evaluationFace,evaluationRevision,value,{},provisional.shape};
                for(context_->InitSelected();context_->MoreSelected();context_->NextSelected())session.provisional->owners.push_back(context_->SelectedOwner());
                session.movePreviewPhase=CommandSession::MovePreviewPhase::Provisional;
                session.state=CommandSession::State::Parameters;
                displayPullArrow(moveFaceArrow_,session.pivot.Translated(gp_Vec(session.direction)*value),gp_Vec(session.direction));
                commandButtons_->button(QDialogButtonBox::Ok)->setEnabled(true);
                emit statusMessage(tr("Provisional Move Face preview — OK performs full certification."));
                redrawFacePreview();return true;
            } catch(const Standard_Failure& failure) {error=QString::fromUtf8(failure.GetMessageString());}
              catch(const std::exception& failure) {error=QString::fromUtf8(failure.what());}
            presentFacePreview(before,false,error);session.movePreviewPhase=CommandSession::MovePreviewPhase::Failed;
            session.geometryError=error;reportGeometryFailure(tr("Move Face"),error,false);redrawFacePreview();return false;
        }
    }
    TopoDS_Shape candidate;
    EdgeBlendFeatures::State candidateBlends;
    if(!directionReady) {}
    else if(session.blendSupport) {
        if(std::abs(value)<1.e-7) {candidate=before.shape;candidateBlends=before.blends;}
        else EdgeBlendFeatures::move(before.shape,evaluationBlends,evaluationFace,localDistance,
            keepBlends_->isChecked(),candidateBlends,candidate,error);
    }
    else if(session.moveFeature==MoveFaceFeature::Planar) candidate=MoveFaceGeometry::move(before.shape,evaluationFace,localDistance,error,
        session.allowEllipticalCapMove?MoveFaceGeometry::ReconstructionPolicy::AxialInvariantCap:MoveFaceGeometry::ReconstructionPolicy::Existing);
    else if(!session.lateralDirectionReady||!cylinderMoveAngle_->hasAcceptableInput())
        error=tr("Choose a reference with a nonzero lateral projection and a valid direction angle.");
    else if(session.moveFeature==MoveFaceFeature::Boss&&cylinderBossBoundary_->currentIndex()<0)
        error=tr("Choose Trim or Extend explicitly.");
    else {
        auto localDelta=(gp_Vec(session.direction)*value).Transformed(before.orientation.Inverted());
        const auto moved=session.moveFeature==MoveFaceFeature::Boss
            ? CylindricalMoveGeometry::moveBoss(before.shape,evaluationFace,localDelta,session.bossTrim
                ? CylindricalMoveGeometry::BossBoundaryMode::Trim:CylindricalMoveGeometry::BossBoundaryMode::Extend)
            : CylindricalMoveGeometry::moveThroughHole(before.shape,evaluationFace,localDelta);
        candidate=moved.shape;
        if(!moved) error=QString::fromStdString(moved.message);
    }
    // No event-loop pumping or worker runs during reconstruction. Also guard
    // against any reentrant context/input change before publishing the result.
    if(!commandSession_||commandSession_->kind!=CommandSession::Kind::MoveFace
        ||commandSession_->faceBefore!=evaluationBefore||moveNumericRevision_!=evaluationRevision)return false;
#ifdef OPENDESIGNER_ENABLE_BOX_SHADOW
    if(session.moveFeature==MoveFaceFeature::Planar&&!session.blendSupport)
        shadowTrace.observe(before.shape,evaluationFace,localDistance,candidate,!session.moveSourceSnapshot.isEmpty());
#endif
    session.valid=!candidate.IsNull();
    session.candidate=candidate;
    commandButtons_->button(QDialogButtonBox::Ok)->setEnabled(session.valid);
    auto& change=session.changes.front();
    change.faceChange.reset();
    if (session.valid && std::abs(value)>=1.e-7) {
        CadPart after=before;
        after.shape=candidate;
        after.blends=session.blendSupport?candidateBlends:EdgeBlendFeatures::State{};
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
    certifyMovePreview(value);
    if(session.provisionalPreviewEnabled) {
        session.movePreviewPhase=session.valid?CommandSession::MovePreviewPhase::Certified:CommandSession::MovePreviewPhase::Failed;
        // Only OK's full certification retires the binding. A preview-time
        // certified fallback (e.g. a drag back through 0 mm) leaves the source
        // owners removed by the earlier provisional Erase; retiring here would
        // compare that renderer-caused empty selection with the original owners.
        if(session.valid&&forceCertifiedMovePreview_)session.provisional.reset();
    }
    if (!session.valid) reportGeometryFailure(tr("Move Face"),error,false);
    redrawFacePreview();
    return session.valid;
}
bool CadViewport::beginMoveFaceDrag(const QPoint& point) {
    if(!commandSession_||!commandSession_->faceBefore) return false;
    const bool blend=commandSession_->kind==CommandSession::Kind::Fillet||commandSession_->kind==CommandSession::Kind::Chamfer;
    if(commandSession_->kind!=CommandSession::Kind::MoveFace&&!blend) return false;
    const auto& session=*commandSession_;
    if(session.moveFeature!=MoveFaceFeature::Planar&&!session.lateralDirectionReady) return false;
    if(session.kind==CommandSession::Kind::MoveFace&&session.directionSource==MoveDirectionSource::ByPoints) return false; // no distance input
    const bool displayed=session.valid||session.movePreviewPhase==CommandSession::MovePreviewPhase::Provisional;
    const gp_Pnt origin=session.pivot.Translated(gp_Vec(session.direction)*(blend?std::max(0.,session.value):(displayed?session.value:0)));
    const auto base=session.kind==CommandSession::Kind::Fillet
        ?origin.Translated(gp_Vec(session.direction)*(-31)):origin;
    const auto tip=base.Translated(gp_Vec(session.direction)*31);
    Standard_Integer x0,y0,x1,y1;
    view_->Convert(base.X(),base.Y(),base.Z(),x0,y0);
    view_->Convert(tip.X(),tip.Y(),tip.Z(),x1,y1);
    const double dx=x1-x0,dy=y1-y0,len2=dx*dx+dy*dy;
    if (len2<4) return false;
    const double t=std::clamp(((point.x()-x0)*dx+(point.y()-y0)*dy)/len2,0.0,1.0);
    if (std::hypot(point.x()-x0-t*dx,point.y()-y0-t*dy)>12) return false;
    draggingFace_=true; dragStart_=point;
    moveFaceDragStart_=blend?std::max(0.,session.value):(displayed?session.value:0);
    dragScreenX_=dx/std::sqrt(len2); dragScreenY_=dy/std::sqrt(len2);
    dragMmPerPixel_=31/std::sqrt(len2);
    return true;
}
void CadViewport::applyFaceState(size_t index, const CadPart& state) {
    // Record an actual source revision even if a later edit restores identical
    // bytes/handles. A normal commit resets the session before reaching here.
    if(commandSession_&&commandSession_->kind==CommandSession::Kind::MoveFace
       &&commandSession_->changes.size()==1&&commandSession_->changes.front().partIndex==index) {
        stopMoveNumericPreview();
        commandSession_->moveSourceInvalidated=true;
        commandSession_->movePreviewCertificate.reset();
        if(!commandSession_->provisionalCandidate().IsNull()) {
            QString error;presentFacePreview(*commandSession_->faceBefore,false,error);
            commandSession_->movePreviewPhase=CommandSession::MovePreviewPhase::Failed;
        }
    }
    auto& part=boxes_[index];
    context_->ClearSelected(Standard_False); // Owners from replaced topology must not be reused.
    part.shape=state.shape; part.geometryKind=state.geometryKind;
    part.blends=state.blends;
    if(!part.blends.certified.IsSame(part.shape)) part.blends={};
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
