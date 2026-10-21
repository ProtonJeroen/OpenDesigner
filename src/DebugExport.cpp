#include "CadViewport.h"
#include "DebugModel.h"
#include <QJsonArray>
#include <TopExp.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <algorithm>
#include <Standard_Failure.hxx>
#include <exception>
#include <cmath>
namespace {
QJsonArray vector(const gp_XYZ& xyz) {return {xyz.X(),xyz.Y(),xyz.Z()};}
QJsonArray transform(const gp_Trsf& value) {
    QJsonArray rows;for(int i=1;i<=3;++i) {QJsonArray row;for(int j=1;j<=4;++j) row.append(value.Value(i,j));rows.append(row);}return rows;
}
int faceIndex(const TopoDS_Shape& shape,const TopoDS_Face& face) {
    TopTools_IndexedMapOfShape faces;TopExp::MapShapes(shape,TopAbs_FACE,faces);return faces.FindIndex(face);
}
}
QJsonObject CadViewport::debugCommand(const CommandSession& session) const {
    static const char* names[]={"Move Part","Rotate Part","Move Face","Rotate Face","Align Face","Boolean","Primitive Pull","Delete Face","Construction"};
    QJsonArray parts;for(auto index:session.selection) parts.append(QString::number(index));
    QJsonObject operation{{"name",names[static_cast<int>(session.kind)]},{"partIds",parts},
        {"value",session.value},{"valueUnit",session.kind==CommandSession::Kind::Rotate||session.kind==CommandSession::Kind::RotateFace?"degrees":"mm"},
        {"validPreview",session.valid},{"changed",session.changed},
        {"worldPivot",vector(session.pivot.XYZ())},{"worldDirection",vector(session.direction.XYZ())}};
    if(session.construction) {
        operation["constructionMethod"]=Construction::definition(session.construction->request.method).name;
        operation["constructionId"]=session.construction->candidate.id;
        operation["constructionOrigin"]=vector(session.construction->candidate.frame.Location().XYZ());
        operation["constructionNormal"]=vector(session.construction->candidate.frame.Direction().XYZ());
        operation["constructionX"]=vector(session.construction->candidate.frame.XDirection().XYZ());
        operation["inputVector"]=vector(session.construction->request.vector.XYZ());
        operation["inputAngles"]=vector(session.construction->request.angles.XYZ());
        operation["inputValue"]=session.construction->request.value;
        operation["alternativeBisector"]=session.construction->request.alternativeBisector;
    }
    operation["state"]=static_cast<int>(session.state);
    operation["worldDelta3x4"]=transform(session.delta);
    if(session.faceBefore) {
        operation["targetFaceIndexBefore"]=faceIndex(session.faceBefore->shape,session.face);
        operation["beforeObjectRole"]=session.state==CommandSession::State::Confirmed?"last-operation-before":"active-command-before";
        operation["partTransform"]=transform(session.faceBefore->orientation);
        operation["targetFaceOrientation"]=static_cast<int>(session.face.Orientation());
        const auto surface=BRepAdaptor_Surface(session.face);
        if(surface.GetType()==GeomAbs_Plane) {
            operation["localTargetPlaneOrigin"]=vector(surface.Plane().Location().XYZ());
            operation["localTargetPlaneNormal"]=vector(surface.Plane().Axis().Direction().XYZ());
        }
        if(session.kind==CommandSession::Kind::MoveFace)
            operation["localMoveDistance"]=session.value/std::abs(session.faceBefore->orientation.ScaleFactor());
        operation["localAxisOrigin"]=vector(session.faceAxis.Location().XYZ());
        operation["localAxisDirection"]=vector(session.faceAxis.Direction().XYZ());
    }
    if(session.kind==CommandSession::Kind::AlignFace) {
        operation["hasReference"]=session.hasReference;operation["coplanar"]=session.coplanar;
        operation["referencePartId"]=QString::number(session.referencePart);
        if(session.referencePart<boxes_.size()) operation["referenceFaceIndex"]=faceIndex(boxes_[session.referencePart].shape,session.referenceFace);
        operation["localReferencePlaneOrigin"]=vector(session.referencePlane.Location().XYZ());
        operation["localReferencePlaneNormal"]=vector(session.referencePlane.Axis().Direction().XYZ());
        operation["valueUnit"]="command parameter; see reference plane and mode";
    }
    if(session.kind==CommandSession::Kind::MoveFace) operation["changed"]=std::abs(session.value)>1.e-7;
    if(session.kind==CommandSession::Kind::DeleteFace) {
        QJsonArray faces,editedParts;
        for(const auto& change:session.changes) {
            editedParts.append(QString::number(change.partIndex));
            for(const auto& selected:session.deletedFaces) if(selected.partIndex==change.partIndex)
                faces.append(QJsonObject{{"partId",QString::number(change.partIndex)},
                    {"faceIndexBefore",faceIndex(change.faceChange->before.shape,selected.face)}});
        }
        operation["partIds"]=editedParts;operation["deletedFacesBefore"]=faces;
        operation.remove("value");operation.remove("valueUnit");
    }
    return operation;
}
bool CadViewport::exportDebugModel(const QString& path,QString& error,const QJsonArray& browserSelection) const {
    try {
    DebugModel::Snapshot snapshot;
    snapshot.context={{"purpose","Diagnostic snapshot only; not .odc Save/Open"},
        {"partIdConvention","Stable zero-based document slot, exported as a decimal string; inactive slots included"},
        {"undoDepth",static_cast<int>(undoStack_.size())},{"redoDepth",static_cast<int>(redoStack_.size())},
        {"operationScope","Latest currently applied undoable history entry. Not a complete command log. Legacy operations may have no recorded parameters."}};
    snapshot.context["structureBrowserSelectionPartIds"]=browserSelection;
    const auto addPart=[&](const CadPart& part,const QString& id,const QString& role) {
        QJsonObject metadata{{"partId",id},{"role",role},{"name",part.name},{"active",part.active},{"visible",part.visible},
            {"colorRgba",QJsonArray{part.color.redF(),part.color.greenF(),part.color.blueF(),part.color.alphaF()}},
            {"transform3x4",transform(part.orientation)},
            {"geometryKind",part.geometryKind==CadPart::GeometryKind::Box?"Box":"GeneralBRep"}};
        if(!part.presentation.IsNull()) {
            metadata["presentationMatchesCanonical"]=part.presentation->Shape().IsSame(part.shape);
            metadata["presentationDisplayMode"]=part.presentation->DisplayMode();
            metadata["presentationTransparency"]=part.presentation->Transparency();
            metadata["presentationDisplayed"]=!context_.IsNull()&&context_->IsDisplayed(part.presentation);
        }
        snapshot.objects.push_back({metadata,part.shape});
    };
    for(size_t i=0;i<boxes_.size();++i) addPart(boxes_[i],QString::number(i),"document-part");
    const auto selection=selectionContext();QJsonArray selectedParts,selectedFaces;
    for(auto index:selection.parts) selectedParts.append(QString::number(index));
    for(const auto& selected:selection.faces) selectedFaces.append(QJsonObject{{"partId",QString::number(selected.partIndex)},
        {"faceIndex",faceIndex(boxes_[selected.partIndex].shape,selected.face)}});
    snapshot.context["selection"]=QJsonObject{{"mode",selectionMode_==SelectionMode::Face?"Face":"Part"},
        {"partIds",selectedParts},{"faces",selectedFaces}};
    if(!undoStack_.empty()) {
        const auto& last=undoStack_.back();
        snapshot.context["lastAppliedOperation"]=last.debugOperation.isEmpty()
            ?QJsonObject{{"available",false},{"reason","This history entry predates diagnostic parameter recording or uses a legacy command."}}
            :last.debugOperation;
        if(last.groupedChanges.empty()) {
            if(last.faceChange) addPart(last.faceChange->before,QString::number(last.partIndex),"last-operation-before");
        } else for(const auto& change:last.groupedChanges)
            if(change.faceChange) addPart(change.faceChange->before,QString::number(change.partIndex),"last-operation-before");
    } else snapshot.context["lastAppliedOperation"]=QJsonObject{{"available",false},{"reason","No applied undoable operation."}};
    if(!view_.IsNull()) {
        const auto camera=view_->Camera();
        snapshot.context["camera"]=QJsonObject{{"eye",vector(camera->Eye().XYZ())},{"center",vector(camera->Center().XYZ())},
            {"up",vector(camera->Up().XYZ())},{"scale",camera->Scale()},{"projectionType",static_cast<int>(camera->ProjectionType())}};
    }
    if(commandSession_) {
        snapshot.context["activeCommand"]=debugCommand(*commandSession_);
        if(commandSession_->kind==CommandSession::Kind::DeleteFace)
            for(size_t i=0;i<commandSession_->changes.size();++i) {
                const auto& change=commandSession_->changes[i];
                addPart(change.faceChange->before,QString::number(change.partIndex),"active-command-before");
                if(commandSession_->valid) {
                    auto candidate=change.faceChange->after;
                    candidate.presentation=commandSession_->facePreviews.at(i);
                    addPart(candidate,QString::number(change.partIndex),"active-preview");
                }
            }
        if(commandSession_->faceBefore) addPart(*commandSession_->faceBefore,
            commandSession_->selection.empty()?QString():QString::number(commandSession_->selection.front()),"active-command-before");
        if(!commandSession_->candidate.IsNull()&&commandSession_->faceBefore) {
            auto candidate=*commandSession_->faceBefore;candidate.shape=commandSession_->candidate;
            addPart(candidate,commandSession_->selection.empty()?QString():QString::number(commandSession_->selection.front()),"active-preview");
        }
    }
    return DebugModel::write(path,snapshot,error);
    } catch(const Standard_Failure& failure) {error=QString::fromUtf8(failure.GetMessageString());}
      catch(const std::exception& failure) {error=QString::fromUtf8(failure.what());}
    return false;
}
