#include "CadViewport.h"
#include "FilletGeometry.h"
#include "ChamferGeometry.h"
#include "EdgeBlendValidation.h"
#include <QDialogButtonBox>
#include <QPushButton>
#include <QLineEdit>
#include <QLocale>
#include <StdSelect_BRepOwner.hxx>
#include <TopoDS.hxx>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <cmath>
#include <ShapeAnalysis_Surface.hxx>
#include <BRepTools.hxx>
#include <BRep_Tool.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <Graphic3d_ZLayerId.hxx>

namespace {
// Evaluate the oriented surface normal at the actual edge/transition point,
// including curved supports and reversed (inner/concave) faces.
gp_Vec outwardNormal(const TopoDS_Face& face,const gp_Pnt& point) {
    ShapeAnalysis_Surface surface(BRep_Tool::Surface(face));
    const auto uv=surface.ValueOfUV(point,1.e-7);
    gp_Pnt p;gp_Vec du,dv;
    BRepAdaptor_Surface(face).D1(uv.X(),uv.Y(),p,du,dv);
    gp_Vec normal=du.Crossed(dv);
    if(normal.Magnitude()>1.e-10)normal.Normalize();
    if(face.Orientation()==TopAbs_REVERSED)normal.Reverse();
    return normal;
}
}

void CadViewport::fillet() {startEdgeBlend(false);}
void CadViewport::chamfer() {startEdgeBlend(true);}
void CadViewport::startEdgeBlend(bool chamfer) {
    cancelCommandSession();
    const auto selection=selectionContext();
    const bool unsupportedSelection=(selection.kind==SelectionContext::Kind::Edges && selection.edges.size()!=1)
        || (selection.kind==SelectionContext::Kind::None && context_->NbSelected()>0);
    endSketchEditing();resetModelingTools();
    beginCommandSession(chamfer?CommandSession::Kind::Chamfer:CommandSession::Kind::Fillet,{},gp_Dir(1,0,0));
    commandSession_->valid=false;
    commandPanel_->setWindowTitle(chamfer?tr("Chamfer Preview"):tr("Fillet Preview"));
    dragDimension_->setToolTip(chamfer?tr("Equal distance in mm"):tr("Radius in mm"));
    dragDimension_->setText(QLocale().toString(1.0));
    commandButtons_->button(QDialogButtonBox::Ok)->setEnabled(false);
    for(auto& part:boxes_) if(part.active && part.visible) {
        context_->Deactivate(part.presentation);
        context_->Activate(part.presentation,AIS_Shape::SelectionMode(TopAbs_EDGE),Standard_False);
    }
    if(unsupportedSelection) {
        emit statusMessage(tr("The selected set is unsupported. Select one Edge for Fillet or Chamfer."));
        return;
    }
    if(selection.kind==SelectionContext::Kind::Edges && selection.edges.size()==1) {
        adoptEdgeBlend(selection.edges.front().partIndex,selection.edges.front().edge);
        return;
    }
    emit statusMessage(tr("Select one solid edge, then enter a positive value in mm."));
}
bool CadViewport::pickEdgeBlend(const QPoint& point) {
    context_->MoveTo(point.x(),point.y(),view_,Standard_True);
    if(!context_->HasDetected()) return false;
    auto owner=Handle(StdSelect_BRepOwner)::DownCast(context_->DetectedOwner());
    if(owner.IsNull() || owner->Shape().IsNull() || owner->Shape().ShapeType()!=TopAbs_EDGE) return false;
    for(size_t i=0;i<boxes_.size();++i) if(isPartVisible(i) && boxes_[i].presentation==context_->DetectedInteractive()) {
        return adoptEdgeBlend(i,TopoDS::Edge(owner->Shape()));
    }
    return false;
}
bool CadViewport::adoptEdgeBlend(size_t i,const TopoDS_Edge& edge) {
    if(!commandSession_ || !isPartVisible(i))return false;
    TopTools_IndexedDataMapOfShapeListOfShape adjacent;QString error;
    if(!EdgeBlendValidation::input(boxes_[i].shape,edge,1.,adjacent,error)) {
        emit statusMessage(error);return false;
    }
    auto& session=*commandSession_;const auto& part=boxes_[i];
    BRepAdaptor_Curve curve(edge);
    auto center=curve.Value((curve.FirstParameter()+curve.LastParameter())/2);
    gp_Vec normal(1,1,1);gp_Vec sum;
    for(const auto& f:adjacent.FindFromKey(edge)) {
        BRepAdaptor_Surface surface(TopoDS::Face(f));
        if(session.kind==CommandSession::Kind::Fillet) {
            sum+=outwardNormal(TopoDS::Face(f),center);continue;
        }
        // Conical supports of a circular Chamfer use their actual outward normal.
        if(surface.GetType()==GeomAbs_Cone) {sum+=outwardNormal(TopoDS::Face(f),center);continue;}
        if(surface.GetType()!=GeomAbs_Plane) continue;
        gp_Vec n(surface.Plane().Axis().Direction());if(f.Orientation()==TopAbs_REVERSED) n.Reverse();sum+=n;
    }
    if(sum.Magnitude()>1.e-8) normal=sum;
    if(session.kind==CommandSession::Kind::Fillet) {
        if(sum.Magnitude()<=1.e-8) {
            emit statusMessage(tr("Cannot determine the Fillet direction. Select another Edge."));return false;
        }
        normal.Reverse();
    }
    session.edge=edge;
    session.faceBefore=std::make_shared<CadPart>(part);
    session.blendSource=part.blends;
    QString recipeError;EdgeBlendFeatures::resolve(part.shape,session.blendSource,recipeError);
    session.direction=gp_Dir(normal.Transformed(part.orientation));
    session.pivot=center.Transformed(part.orientation);
    session.changes.clear();
    session.changes.push_back({i,part.min,part.max,part.min,part.max,part.orientation,part.orientation});
    return updateCommandParameter(false);
}
bool CadViewport::previewEdgeBlend(double value) {
    auto& session=*commandSession_;
    if(!session.faceBefore) return false;
    const auto& before=*session.faceBefore;
    QString error;
    const double localValue=value/std::abs(before.orientation.ScaleFactor());
    TopoDS_Shape candidate;
    EdgeBlendFeatures::State candidateState;
    const auto& recipe=session.blendSource;
    bool built=false;
    if(!session.editBlendId.isEmpty())
        built=EdgeBlendFeatures::edit(before.shape,recipe,session.editBlendId,localValue,candidateState,candidate,error);
    else {
        built=EdgeBlendFeatures::create(before.shape,recipe,session.edge,
            session.kind==CommandSession::Kind::Fillet?EdgeBlendFeatures::Kind::Fillet:EdgeBlendFeatures::Kind::Chamfer,
            localValue,candidateState,candidate,error);
        if(!built&&recipe.features.empty()) {
            // Creation on more general topology retains the existing validated
            // kernel path, but never attaches an uncertified editing recipe.
            candidate=session.kind==CommandSession::Kind::Fillet
                ?FilletGeometry::apply(before.shape,session.edge,localValue,error)
                :ChamferGeometry::apply(before.shape,session.edge,localValue,error);
        }
    }
    session.value=value;session.candidate=candidate;
    session.valid=!candidate.IsNull();session.changed=session.valid;
    if(!session.editBlendId.isEmpty()) for(const auto& f:recipe.features) if(f.id==session.editBlendId)
        session.changed=session.valid&&std::abs(localValue-f.size)>1.e-12*std::max(1.,f.size);
    auto& change=session.changes.front();change.faceChange.reset();
    if(session.valid) {
        CadPart after=before;after.shape=candidate;after.blends=built?candidateState:EdgeBlendFeatures::State{};
        after.geometryKind=CadPart::GeometryKind::GeneralBRep;
        after.cylinderPrimitive=after.spherePrimitive=after.conePrimitive=false;
        Bnd_Box bounds;BRepBndLib::AddOptimal(candidate,bounds,false,false);
        bounds.Get(after.min[0],after.min[1],after.min[2],after.max[0],after.max[1],after.max[2]);
        after.baseMin=after.min;after.baseMax=after.max;
        change.afterMin=after.min;change.afterMax=after.max;
        change.faceChange=std::make_shared<FaceChange>(FaceChange{before,after});
    }
    const bool valid=presentFacePreview(before,true,error);
    session.geometryError=error;
    session.state=valid?CommandSession::State::Preview:CommandSession::State::Parameters;
    if(!valid) emit statusMessage(error);
    updateEdgeBlendArrow();
    redrawFacePreview();return valid;
}

bool CadViewport::startEditEdgeBlend(size_t index,const TopoDS_Face& face) {
    if(!isPartVisible(index)) return false;
    auto recipe=boxes_[index].blends;QString error;
    if(!EdgeBlendFeatures::resolve(boxes_[index].shape,recipe,error)) {emit statusMessage(error);return false;}
    const int selected=EdgeBlendFeatures::selected(recipe,face);
    if(selected<0) {emit statusMessage(tr("Choose a recognized Fillet or equal-distance Chamfer face."));return false;}
    const auto feature=recipe.features[selected];
    cancelCommandSession();resetModelingTools();
    const auto& part=boxes_[index];
    beginCommandSession(feature.kind==EdgeBlendFeatures::Kind::Fillet?CommandSession::Kind::Fillet:CommandSession::Kind::Chamfer,{index},gp_Dir(1,0,0));
    auto& session=*commandSession_;session.faceBefore=std::make_shared<CadPart>(part);
    session.blendSource=recipe;session.editBlendId=feature.id;session.face=face;
    GProp_GProps props;BRepGProp::SurfaceProperties(face,props);auto center=props.CentreOfMass();
    BRepAdaptor_Surface surface(face);gp_Vec normal;
    if(feature.kind==EdgeBlendFeatures::Kind::Fillet) {
        double u0,u1,v0,v1;BRepTools::UVBounds(face,u0,u1,v0,v1);
        center=surface.Value((u0+u1)/2,(v0+v1)/2);
        normal=-outwardNormal(face,center);
    } else if(surface.GetType()==GeomAbs_Plane) normal=gp_Vec(surface.Plane().Axis().Direction());
    else if(surface.GetType()==GeomAbs_Cone) {
        // Circular Chamfer band: its centre of mass lies on the axis, so anchor
        // the manipulator on the band itself. The unoriented carrier normal is
        // oriented once below, like a planar Chamfer.
        double u0,u1,v0,v1;BRepTools::UVBounds(face,u0,u1,v0,v1);
        center=surface.Value((u0+u1)/2,(v0+v1)/2);
        normal=outwardNormal(face,center);
        if(face.Orientation()==TopAbs_REVERSED) normal.Reverse();
    } else if(surface.GetType()==GeomAbs_Torus) {
        const auto circle=surface.Torus();
        center=circle.Location().Translated(gp_Vec(circle.XAxis().Direction())*circle.MajorRadius());
        normal=gp_Vec(circle.Axis().Direction());
    } else if(surface.GetType()==GeomAbs_Cylinder) {
        const auto axis=surface.Cylinder().Axis();normal=gp_Vec(axis.Location(),center);
        normal-=gp_Vec(axis.Direction())*normal.Dot(gp_Vec(axis.Direction()));
    }
    if(normal.Magnitude()<1.e-8) {cancelCommandSession();return false;}
    if(feature.kind!=EdgeBlendFeatures::Kind::Fillet && face.Orientation()==TopAbs_REVERSED) normal.Reverse();
    session.direction=gp_Dir(normal.Transformed(part.orientation));
    session.value=feature.size*std::abs(part.orientation.ScaleFactor());
    session.pivot=center.Transformed(part.orientation).Translated(gp_Vec(session.direction)*(-session.value));
    commandPanel_->setWindowTitle(feature.kind==EdgeBlendFeatures::Kind::Fillet?tr("Edit Fillet Preview"):tr("Edit Chamfer Preview"));
    dragDimension_->setToolTip(feature.kind==EdgeBlendFeatures::Kind::Fillet?tr("Absolute radius in mm"):tr("Absolute equal distance in mm"));
    dragDimension_->setText(QLocale().toString(session.value,'g',17));
    updateCommandParameter(false);showCommandControls(previous_);
    commandPanel_->activateWindow();dragDimension_->setFocus();dragDimension_->selectAll();
    return true;
}
void CadViewport::updateEdgeBlendArrow() {
    if(!commandSession_||!commandSession_->faceBefore) return;
    const auto& s=*commandSession_;
    const double value=std::isfinite(s.value)?std::max(0.,s.value):0.;
    // displayPullArrow builds 31 mm from base to tip. Keep the shaft outside
    // the material, with its tip at the moving radius anchor.
    const double offset=s.kind==CommandSession::Kind::Fillet?value-31.:value;
    displayPullArrow(moveFaceArrow_,s.pivot.Translated(gp_Vec(s.direction)*offset),gp_Vec(s.direction));
    if(!moveFaceArrow_.IsNull()) context_->SetZLayer(moveFaceArrow_,Graphic3d_ZLayerId_Topmost);
}
