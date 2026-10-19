#include "CadViewport.h"
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepBndLib.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRep_Builder.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <Standard_Failure.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopoDS_Compound.hxx>
#include <StdSelect_BRepOwner.hxx>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace {
bool validSolid(const TopoDS_Shape& shape, double& volume) {
    if (shape.IsNull() || !TopExp_Explorer(shape,TopAbs_SOLID).More()
        || !BRepCheck_Analyzer(shape).IsValid()) return false;
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
}

void CadViewport::announcePartCreated(const QString& name) {
    boxes_.back().name = name;
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
    if (!isPartActive(index) || !color.isValid()) return;
    auto& part = boxes_[index];
    part.color = color;
    part.presentation->SetColor(Quantity_Color(color.redF(),color.greenF(),color.blueF(),Quantity_TOC_RGB));
    if (!context_.IsNull()) {
        context_->Redisplay(part.presentation,Standard_False);
        view_->Redraw();
    }
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
void CadViewport::uniteSelectedParts() {
    applyBoolean(BooleanOperation::Unite,selectedBooleanParts());
}
void CadViewport::intersectSelectedParts() {
    applyBoolean(BooleanOperation::Intersect,selectedBooleanParts());
}
void CadViewport::subtractSelectedParts() {
    const auto selected = selectedBooleanParts();
    if (selected.size() < 2) {
        emit statusMessage(tr("Subtract: select a target and at least one tool first."));
        return;
    }
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Subtract — choose target and tools"));
    QFormLayout layout(&dialog);
    QLabel explanation(tr("Choose the part to keep. All other selected parts are cutting tools."),&dialog);
    explanation.setWordWrap(true);
    layout.addRow(&explanation);
    QComboBox target(&dialog);
    for (const auto index : selected)
        target.addItem(tr("%1 [part %2]").arg(partName(index)).arg(index+1));
    target.setCurrentIndex(-1);
    layout.addRow(tr("Target"),&target);
    QLabel tools(&dialog);
    tools.setWordWrap(true);
    layout.addRow(tr("Tools (consumed on success)"),&tools);
    QDialogButtonBox buttons(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);
    buttons.button(QDialogButtonBox::Ok)->setEnabled(false);
    layout.addRow(&buttons);
    connect(&target,&QComboBox::currentIndexChanged,&dialog,[&](int current) {
        QStringList names;
        for (size_t i = 0; i < selected.size(); ++i)
            if (static_cast<int>(i) != current) names << partName(selected[i]);
        tools.setText(names.join(QStringLiteral(", ")));
        buttons.button(QDialogButtonBox::Ok)->setEnabled(current >= 0);
    });
    connect(&buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);
    connect(&buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    if (dialog.exec() == QDialog::Accepted && target.currentIndex() >= 0)
        applyBoolean(BooleanOperation::Subtract,selected,selected[static_cast<size_t>(target.currentIndex())]);
}
void CadViewport::finishModelingInteraction() {
    dragDimension_->hide();
    dimensionEditing_ = rotationEditing_ = primitivePullEditing_ = false;
    multiFaceEditing_ = groupMoveEditing_ = groupRotationEditing_ = false;
    draggingFace_ = draggingRotation_ = primitivePullDragging_ = false;
    multiFaceDragging_ = groupMoveDragging_ = groupRotationDragging_ = false;
    multiFaceParts_.clear();
    clearMultiFaceHighlight();
    hideFaceArrow(); hideMoveAxes(); hideRotationRings();
}
bool CadViewport::applyBoolean(BooleanOperation operation, const std::vector<size_t>& parts,
                               std::optional<size_t> target) {
    const QString name = operation == BooleanOperation::Unite ? tr("Unite")
        : operation == BooleanOperation::Subtract ? tr("Subtract") : tr("Intersect");
    const auto fail = [&](const QString& reason) {
        emit statusMessage(tr("%1 failed: %2 Original parts were kept.").arg(name,reason));
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
    if (operation == BooleanOperation::Subtract) {
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
        command->sources = operands;
        for (const auto index : operands) command->sourceStates.push_back(boxes_[index]);
        command->beforeSelection = selectedBooleanParts();
        command->resultIndex = boxes_.size();
        command->resultState = result;
        // Geometry and command are complete before changing the document or scene.
        pending = command;
        boxes_.push_back(result);
        ++partCount_;
        announcePartCreated(result.name);
        PullChange entry{};
        entry.booleanChange = command;
        restoreBooleanChange(*command,true);
        undoStack_.push_back(entry);
        redoStack_.clear();
        emit statusMessage(tr("%1 succeeded: %2 solid(s) in one new part.").arg(name).arg(solids));
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
