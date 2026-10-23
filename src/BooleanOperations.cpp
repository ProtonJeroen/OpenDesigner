#include "CadViewport.h"
#include "BRepReconstruction.h"
#include <QJsonArray>
#include <QVBoxLayout>
#include <QCheckBox>
#include <QListWidget>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QUuid>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepBndLib.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRep_Tool.hxx>
#include <BRepGProp.hxx>
#include <BRep_Builder.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <Standard_Failure.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopoDS_Compound.hxx>
#include <StdSelect_BRepOwner.hxx>
#include <Graphic3d_ZLayerId.hxx>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace {
bool validSolid(const TopoDS_Shape& shape, double& volume) {
    if (shape.IsNull() || !TopExp_Explorer(shape,TopAbs_SOLID).More()
        || !BRepCheck_Analyzer(shape).IsValid()) return false;
    for(TopExp_Explorer shell(shape,TopAbs_SHELL);shell.More();shell.Next())
        if(!BRep_Tool::IsClosed(shell.Current())) return false;
    GProp_GProps properties;
    BRepGProp::VolumeProperties(shape,properties);
    volume = properties.Mass();
    return std::isfinite(volume) && volume > 1.0e-12;
}
template<class Algorithm>
TopoDS_Shape combine(const TopoDS_Shape& a, const TopoDS_Shape& b) {
    Algorithm algorithm;
    TopTools_ListOfShape arguments, tools;
    arguments.Append(a); tools.Append(b);
    algorithm.SetArguments(arguments);
    algorithm.SetTools(tools);
    algorithm.SetNonDestructive(Standard_True);
    algorithm.Build();
    if (!algorithm.IsDone() || algorithm.HasErrors())
        throw std::runtime_error("Open CASCADE could not complete the Boolean operation.");
    return algorithm.Shape();
}
// Boolean common can include lower-dimensional contacts. Keep only solid volume.
TopoDS_Shape solidResult(const TopoDS_Shape& shape, int& count) {
    count = 0;
    if (shape.IsNull()) return {};
    BRep_Builder builder;
    TopoDS_Compound compound;
    builder.MakeCompound(compound);
    TopoDS_Shape first;
    for (TopExp_Explorer solids(shape,TopAbs_SOLID);solids.More();solids.Next()) {
        if (count++ == 0) first = solids.Current();
        builder.Add(compound,solids.Current());
    }
    return count == 1 ? first : TopoDS_Shape(compound);
}
TopoDS_Shape normalizeBooleanResult(const TopoDS_Shape& shape) {
    // Merge redundant face/edge divisions before presentation and history capture.
    // Face reconstruction's stricter single-shell validator cannot be used here:
    // Boolean Parts may contain disconnected solids and solids with cavities.
    BRepBuilderAPI_Copy copy(shape,Standard_True,Standard_False);
    ShapeUpgrade_UnifySameDomain unify(copy.Shape(),Standard_True,Standard_True,Standard_False);
    unify.SetSafeInputMode(Standard_True);
    unify.Build();
    const auto count=[](const TopoDS_Shape& s,TopAbs_ShapeEnum type) {
        int n=0; for(TopExp_Explorer it(s,type);it.More();it.Next()) ++n;
        return n;
    };
    const auto result=unify.Shape();
    double volume=0;
    if (!validSolid(result,volume)
        || count(shape,TopAbs_SOLID)!=count(result,TopAbs_SOLID)
        || count(shape,TopAbs_SHELL)!=count(result,TopAbs_SHELL)
        || !BRepReconstruction::equivalent(shape,result))
        throw std::runtime_error("Final Boolean unification changed material or invalidated the solid geometry.");
    return result;
}
}

void CadViewport::announcePartCreated(const QString& name) {
    boxes_.back().name = name;
    if (boxes_.back().persistentId.isEmpty())
        boxes_.back().persistentId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    emit partCreated(name);
}
bool CadViewport::isPartActive(size_t index) const {
    return index < boxes_.size() && boxes_[index].active;
}
QString CadViewport::partName(size_t index) const {
    return isPartActive(index) ? boxes_[index].name : QString();
}
void CadViewport::renamePart(size_t index, const QString& name) {
    if (!isPartActive(index) || name.trimmed().isEmpty()) return;
    boxes_[index].name = name.trimmed();
    emit partsChanged();
}
QColor CadViewport::partColor(size_t index) const {
    return isPartActive(index) ? boxes_[index].color : QColor();
}
void CadViewport::setPartColor(size_t index, const QColor& color) {
    cancelCommandSession();
    if (!isPartActive(index) || !color.isValid()) return;
    auto& part = boxes_[index];
    part.color = color;
    part.presentation->SetColor(Quantity_Color(color.redF(),color.greenF(),color.blueF(),Quantity_TOC_RGB));
    if (!context_.IsNull()) {
        context_->Redisplay(part.presentation,Standard_False);
        view_->Redraw();
    }
    emit partsChanged();
}
std::vector<size_t> CadViewport::selectedBooleanParts() const {
    std::vector<size_t> result;
    const auto add = [&](size_t index) {
        if (isPartActive(index) && std::find(result.begin(),result.end(),index) == result.end())
            result.push_back(index);
    };
    if (!context_.IsNull()) {
        for (context_->InitSelected();context_->MoreSelected();context_->NextSelected()) {
            const auto object = context_->SelectedInteractive();
            for (size_t i = 0; i < boxes_.size(); ++i)
                if (boxes_[i].presentation == object) add(i);
        }
    }
    if (result.empty())
        for (const auto index : selectedPartIndices_) add(index);
    return result;
}
void CadViewport::uniteSelectedParts() { beginBooleanSession(BooleanOperation::Unite); }
void CadViewport::intersectSelectedParts() { beginBooleanSession(BooleanOperation::Intersect); }
void CadViewport::subtractSelectedParts() { beginBooleanSession(BooleanOperation::Subtract); }

void CadViewport::beginBooleanSession(BooleanOperation operation) {
    endSketchEditing();
    cancelCommandSession();
    const auto selected=selectedBooleanParts();
    beginCommandSession(CommandSession::Kind::Boolean,{},gp_Dir(1,0,0));
    auto& session=*commandSession_;
    session.operation=operation;
    session.initialParts=selected;
    for(const auto index:selected) if(isPartVisible(index)) {
        if(!session.target) session.target=index;
        else session.tools.push_back(index);
    }
    session.chooseTarget=!session.target;
    movePartMode_=directMoveMode_=rotateMode_=false;
    emit transformModesChanged(false,false,false);
    hideFaceArrow(); hideMoveAxes(); hideRotationRings();
    selectionMode_=SelectionMode::Part;
    emit selectionModeChanged(selectionMode_);
    for(const auto& part:boxes_) if(part.active && part.visible) {
        context_->Deactivate(part.presentation); context_->Activate(part.presentation,0,Standard_False);
    }
    dragDimension_->hide();
    booleanControls_=new QWidget(commandPanel_);
    auto* layout=new QVBoxLayout(booleanControls_);
    const auto name=operation==BooleanOperation::Unite ? tr("Unite") : operation==BooleanOperation::Subtract ? tr("Subtract") : tr("Intersect");
    layout->addWidget(new QLabel(tr("Boolean — %1").arg(name),booleanControls_));
    auto* target=new QLabel(booleanControls_); target->setObjectName("booleanTarget"); layout->addWidget(target);
    auto* choose=new QPushButton(tr("Choose Target"),booleanControls_); choose->setObjectName("booleanChooseTarget"); layout->addWidget(choose);
    connect(choose,&QPushButton::clicked,this,[this]() {
        commandSession_->chooseTarget=true;
        emit statusMessage(tr("Boolean: select a visible Target in the viewport or Structure Browser."));
    });
    layout->addWidget(new QLabel(tr("Tools — click Parts to add/remove"),booleanControls_));
    auto* tools=new QListWidget(booleanControls_); tools->setObjectName("booleanTools");
    tools->setSelectionMode(QAbstractItemView::ExtendedSelection); layout->addWidget(tools);
    auto* remove=new QPushButton(tr("Remove selected Tools"),booleanControls_); layout->addWidget(remove);
    connect(remove,&QPushButton::clicked,this,[this,tools]() {
        auto& ids=commandSession_->tools;
        for(auto* item:tools->selectedItems()) {
            const auto id=item->data(Qt::UserRole).toULongLong();
            ids.erase(std::remove(ids.begin(),ids.end(),id),ids.end());
        }
        updateBooleanControls();
    });
    auto* keep=new QCheckBox(tr("Keep Tools"),booleanControls_); keep->setObjectName("booleanKeepTools"); layout->addWidget(keep);
    connect(keep,&QCheckBox::toggled,this,[this](bool checked) { commandSession_->keepTools=checked; updateBooleanControls(); });
    auto* preview=new QPushButton(tr("Preview"),booleanControls_); preview->setObjectName("booleanPreview"); layout->addWidget(preview);
    connect(preview,&QPushButton::clicked,this,[this]() { previewBooleanSession(); });
    static_cast<QVBoxLayout*>(commandPanel_->layout())->insertWidget(0,booleanControls_);
    commandPanel_->setWindowTitle(operation==BooleanOperation::Unite ? tr("Boolean — Unite") : operation==BooleanOperation::Subtract ? tr("Boolean — Subtract") : tr("Boolean — Intersect"));
    updateBooleanControls();
    commandPanel_->adjustSize();
    commandPanel_->move(mapToGlobal(QPoint(16,16)));
    emit statusMessage(tr("Boolean: select Target, then click Tool Parts to add/remove; Preview, OK/Enter or Cancel/Esc."));
}
void CadViewport::selectBooleanParts(const std::vector<size_t>& indices,bool toggle) {
    auto& session=*commandSession_;
    if(session.chooseTarget) {
        for(const auto index:indices) if(isPartVisible(index)) {
            session.target=index; session.chooseTarget=false;
            session.tools.erase(std::remove(session.tools.begin(),session.tools.end(),index),session.tools.end());
            break;
        }
    } else {
        if(!toggle) session.tools.clear(); // Browser multi-selection describes the tool set.
        for(const auto index:indices) if(isPartVisible(index) && session.target!=index) {
            auto found=std::find(session.tools.begin(),session.tools.end(),index);
            if(found==session.tools.end()) session.tools.push_back(index);
            else if(toggle) session.tools.erase(found);
        }
    }
    updateBooleanControls();
}
void CadViewport::updateBooleanControls() {
    auto& session=*commandSession_;
    if(!session.preview.IsNull()) { context_->Remove(session.preview,Standard_False); session.preview.Nullify(); }
    session.booleanChange.reset(); session.valid=false; session.state=CommandSession::State::Selection;
    auto* target=booleanControls_->findChild<QLabel*>("booleanTarget");
    target->setText(session.target ? tr("Target: %1 [part %2]").arg(partName(*session.target)).arg(*session.target+1) : tr("Target: select a visible Part"));
    auto* tools=booleanControls_->findChild<QListWidget*>("booleanTools"); tools->clear();
    for(const auto index:session.tools) {
        auto* item=new QListWidgetItem(tr("Tool: %1 [part %2]").arg(partName(index)).arg(index+1),tools);
        item->setData(Qt::UserRole,QVariant::fromValue<qulonglong>(index));
    }
    commandButtons_->button(QDialogButtonBox::Ok)->setEnabled(session.target && !session.tools.empty());
    view_->Redraw();
}
bool CadViewport::previewBooleanSession() {
    auto& session=*commandSession_;
    if(!session.preview.IsNull()) { context_->Remove(session.preview,Standard_True); session.preview.Nullify(); }
    session.booleanChange.reset(); session.valid=false; session.state=CommandSession::State::Parameters;
    if(!session.target || session.tools.empty()) return false;
    std::vector<size_t> operands{*session.target};
    operands.insert(operands.end(),session.tools.begin(),session.tools.end());
    for(const auto index:operands) if(!isPartVisible(index)) return false;
    if(!calculateBoolean(session.operation,operands,session.target,session.keepTools,true)) return false;
    session.preview=new AIS_Shape(session.booleanChange->resultState.shape);
    session.preview->SetColor(Quantity_NOC_ORANGE);
    session.preview->SetTransparency(0.35);
    context_->Display(session.preview,Standard_False);
    context_->SetDisplayMode(session.preview,AIS_Shaded,Standard_False);
    context_->SetZLayer(session.preview,Graphic3d_ZLayerId_Topmost);
    context_->Deactivate(session.preview);
    session.valid=true; session.state=CommandSession::State::Preview;
    view_->Redraw(); return true;
}
void CadViewport::commitBooleanChange(const std::shared_ptr<BooleanChange>& command) {
    command->resultIndex=boxes_.size();
    boxes_.push_back(command->resultState); ++partCount_;
    announcePartCreated(command->resultState.name);
    restoreBooleanChange(*command,true);
    PullChange entry{}; entry.booleanChange=command;entry.debugOperation=command->debugOperation;
    undoStack_.push_back(entry); redoStack_.clear();
    updateHistoryActions();
    int solids=0;
    for(TopExp_Explorer solid(command->resultState.shape,TopAbs_SOLID);solid.More();solid.Next()) ++solids;
    emit statusMessage(tr("Boolean confirmed: %1 solid(s) in %2.").arg(solids).arg(command->resultState.name));
}
void CadViewport::finishModelingInteraction() {
    cancelCommandSession();
}

bool CadViewport::applyBoolean(BooleanOperation operation, const std::vector<size_t>& parts,
                               std::optional<size_t> target, bool keepTools) {
    return calculateBoolean(operation,parts,target,keepTools,false);
}
bool CadViewport::calculateBoolean(BooleanOperation operation, const std::vector<size_t>& parts,
                               std::optional<size_t> target, bool keepTools, bool previewOnly) {
    if(!previewOnly) cancelCommandSession();
    const QString name = operation == BooleanOperation::Unite ? tr("Unite")
        : operation == BooleanOperation::Subtract ? tr("Subtract") : tr("Intersect");
    bool geometryPhase=false;
    const auto fail = [&](const QString& reason) {
        if(geometryPhase) reportGeometryFailure(name,reason,true);
        else emit statusMessage(tr("%1 failed: %2 Original parts were kept.").arg(name,reason));
        return false;
    };
    if (draggingFace_ || draggingRotation_) return fail(tr("Finish the active drag first."));
    if (parts.size() < 2) return fail(tr("Select at least two solid parts."));
    std::vector<size_t> operands = parts;
    for (size_t i = 0; i < operands.size(); ++i) {
        if (!isPartActive(operands[i])) return fail(tr("A selected part no longer exists."));
        if (std::find(operands.begin(),operands.begin()+i,operands[i]) != operands.begin()+i)
            return fail(tr("The same part cannot be used twice."));
    }
    if (operation == BooleanOperation::Subtract || target) {
        if (!target || std::find(operands.begin(),operands.end(),*target) == operands.end())
            return fail(tr("Choose one of the selected parts as the target."));
        const auto found = std::find(operands.begin(),operands.end(),*target);
        std::rotate(operands.begin(),found,found+1);
    }
    std::shared_ptr<BooleanChange> pending;
    const auto rollback = [&]() {
        if (!pending || pending->resultIndex >= boxes_.size()) return;
        // Recover document state even if a presentation update itself failed.
        boxes_[pending->resultIndex].active = false;
        for (size_t i = 0; i < pending->sources.size(); ++i)
            boxes_[pending->sources[i]] = pending->sourceStates[i];
        restoreBooleanChange(*pending,false);
    };
    try {
        geometryPhase=true;
        std::vector<TopoDS_Shape> world;
        std::vector<double> volumes;
        for (const auto index : operands) {
            const auto& part = boxes_[index];
            double volume = 0.0;
            if (!validSolid(part.shape,volume))
                return fail(tr("%1 is not a valid solid.").arg(part.name));
            // Copy geometry and apply the complete instance transform once.
            const auto shape = BRepBuilderAPI_Transform(part.shape,part.orientation,Standard_True).Shape();
            if (!validSolid(shape,volume)) return fail(tr("A world-space operand is invalid."));
            world.push_back(shape); volumes.push_back(volume);
        }
        TopoDS_Shape shape = world.front();
        for (size_t i = 1; i < world.size(); ++i) {
            if (operation == BooleanOperation::Unite) shape = combine<BRepAlgoAPI_Fuse>(shape,world[i]);
            else if (operation == BooleanOperation::Subtract) shape = combine<BRepAlgoAPI_Cut>(shape,world[i]);
            else shape = combine<BRepAlgoAPI_Common>(shape,world[i]);
            if (shape.IsNull() || !TopExp_Explorer(shape,TopAbs_SOLID).More())
                return fail(tr("The result has no solid volume (empty, disjoint or touching only)."));
        }
        int solids = 0;
        shape = solidResult(shape,solids);
        double volume = 0.0;
        if (solids == 0) return fail(tr("The result has no solid volume (empty, disjoint or touching only)."));
        if (!validSolid(shape,volume)) return fail(tr("Open CASCADE produced invalid solid geometry."));
        if (operation == BooleanOperation::Subtract
            && std::abs(volumes.front()-volume) <= std::max(1.0e-9,volumes.front()*1.0e-12))
            return fail(tr("The tools remove no volume from the target."));
        shape = normalizeBooleanResult(shape);
        Bnd_Box bounds;
        BRepBndLib::Add(shape,bounds);
        if (bounds.IsVoid()) return fail(tr("The result has no bounds."));
        double xmin,ymin,zmin,xmax,ymax,zmax;
        bounds.Get(xmin,ymin,zmin,xmax,ymax,zmax);
        CadPart result;
        result.geometryKind = CadPart::GeometryKind::GeneralBRep;
        result.shape = shape;
        result.min = result.baseMin = {xmin,ymin,zmin};
        result.max = result.baseMax = {xmax,ymax,zmax};
        result.name = tr("%1 %2").arg(name).arg(partCount_+1);
        result.color = boxes_[operands.front()].color;
        result.presentation = new AIS_Shape(shape);
        result.presentation->SetColor(Quantity_Color(result.color.redF(),result.color.greenF(),
            result.color.blueF(),Quantity_TOC_RGB));
        auto command = std::make_shared<BooleanChange>();
        QJsonArray debugOperands;for(auto operand:operands) debugOperands.append(QString::number(operand));
        command->debugOperation=QJsonObject{{"available",true},{"name",name},{"operandPartIds",debugOperands},
            {"keepTools",keepTools},{"targetPartId",target?QJsonValue(QString::number(*target)):QJsonValue(QJsonValue::Null)}};
        command->sources = keepTools ? std::vector<size_t>{operands.front()} : operands;
        for (const auto index : command->sources) command->sourceStates.push_back(boxes_[index]);
        command->beforeSelection = selectedBooleanParts();
        command->resultIndex = boxes_.size();
        command->resultState = result;
        // A validated candidate is shared by interactive preview and direct callers.
        if(previewOnly) {
            command->beforeSelection=commandSession_->initialParts;
            commandSession_->booleanChange=command;
            return true;
        }
        pending = command;
        commitBooleanChange(command);
        return true;
    } catch (const Standard_Failure& error) {
        rollback();
        return fail(tr("Open CASCADE: %1").arg(QString::fromUtf8(error.GetMessageString())));
    } catch (const std::exception& error) {
        rollback();
        return fail(QString::fromUtf8(error.what()));
    }
}
void CadViewport::restoreBooleanChange(const BooleanChange& change, bool after) {
    finishModelingInteraction();
    context_->ClearSelected(Standard_False);
    context_->MoveTo(-1,-1,view_,Standard_False);
    const auto remove = [&](size_t index) {
        context_->Remove(boxes_[index].presentation,Standard_False);
        boxes_[index].active = false;
    };
    const auto restore = [&](size_t index, const CadPart& state) {
        context_->Remove(boxes_[index].presentation,Standard_False);
        boxes_[index] = state;
        auto& part = boxes_[index];
        part.active = true;
        part.presentation->SetShape(part.shape);
        part.presentation->SetLocalTransformation(part.orientation);
        part.presentation->SetColor(Quantity_Color(part.color.redF(),part.color.greenF(),part.color.blueF(),Quantity_TOC_RGB));
        if (part.visible) {
            context_->Display(part.presentation,Standard_False);
            context_->SetDisplayMode(part.presentation,AIS_Shaded,Standard_False);
            context_->Deactivate(part.presentation);
            context_->Activate(part.presentation,(directMoveMode_ || rotateMode_) ? 0
                : AIS_Shape::SelectionMode(TopAbs_FACE),Standard_False);
        }
    };
    if (after) {
        for (const auto index : change.sources) remove(index);
        restore(change.resultIndex,change.resultState);
    } else {
        remove(change.resultIndex);
        for (size_t i = 0; i < change.sources.size(); ++i) restore(change.sources[i],change.sourceStates[i]);
    }
    emit partsChanged();
    selectParts(after ? std::vector<size_t>{change.resultIndex} : change.beforeSelection);
}
