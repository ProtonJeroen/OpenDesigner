#include "MainWindow.h"
#include "CadViewport.h"
#include <QTreeWidget>
#include <QSignalBlocker>
#include <QAbstractItemView>
#include <QMenu>
#include <QHeaderView>
#include <QBrush>
#include <QColor>
#include <QDockWidget>
#include <QMenuBar>
#include <QToolBar>
#include <QStatusBar>
#include <QAction>
#include <QKeySequence>
#include <QMessageBox>
#include <QActionGroup>

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("OpenDesigner 0.19 (prototype)");
    resize(1280, 800);
    viewport_ = new CadViewport(this);
    setCentralWidget(viewport_);
    auto* dock = new QDockWidget(tr("Structure Browser"), this);
    structure_ = new QTreeWidget(dock);
    structure_->setColumnCount(2);
    structure_->setHeaderLabels({tr("Model"), tr("Visible")});
    structure_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    structure_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    structure_->setContextMenuPolicy(Qt::CustomContextMenu);
    structure_->setEditTriggers(QAbstractItemView::EditKeyPressed | QAbstractItemView::DoubleClicked);
    structure_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    auto* root = new QTreeWidgetItem(structure_, QStringList{tr("Assembly 1")});
    root->setExpanded(true);
    dock->setWidget(structure_);
    addDockWidget(Qt::LeftDockWidgetArea, dock);

    auto* file = menuBar()->addMenu(tr("&File"));
    auto* exitAction = file->addAction(tr("E&xit"));
    connect(exitAction, &QAction::triggered, this, &QWidget::close);
    auto* edit = menuBar()->addMenu(tr("&Edit"));
    auto* undoAction = edit->addAction(tr("&Undo Modeling"));
    undoAction->setShortcut(QKeySequence::Undo);
    connect(undoAction, &QAction::triggered, viewport_, &CadViewport::undoPull);
    auto* redoAction = edit->addAction(tr("&Redo Modeling"));
    redoAction->setShortcut(QKeySequence::Redo);
    connect(redoAction, &QAction::triggered, viewport_, &CadViewport::redoPull);
    auto* model = menuBar()->addMenu(tr("&Modeling"));
    auto* boxAction = model->addAction(tr("Create &Box"));
    connect(boxAction, &QAction::triggered, viewport_, &CadViewport::addBox);
    auto* duplicateAction = model->addAction(tr("&Duplicate Selected Parts"));
    duplicateAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_D));
    connect(duplicateAction, &QAction::triggered, viewport_, &CadViewport::duplicateSelectedParts);
    auto* pullAction = model->addAction(tr("&Pull Face..."));
    connect(pullAction, &QAction::triggered, viewport_, &CadViewport::pullSelectedFace);
    auto* movePartAction = model->addAction(tr("Move &Part Along Face..."));
    connect(movePartAction, &QAction::triggered, viewport_, &CadViewport::movePartAlongSelectedFace);
    auto* rotatePartAction = model->addAction(tr("&Rotate Part..."));
    connect(rotatePartAction, &QAction::triggered, viewport_, &CadViewport::rotateSelectedPart);
    auto* interactiveMove = model->addAction(tr("Interactive &Move Part Mode"));
    interactiveMove->setCheckable(true);
    connect(interactiveMove, &QAction::toggled, viewport_, &CadViewport::setMovePartMode);
    auto* interactiveRotate = model->addAction(tr("Interactive &Rotate Part Mode"));
    interactiveRotate->setCheckable(true);
    connect(interactiveRotate, &QAction::toggled, viewport_, &CadViewport::setRotateMode);
    auto* directMove = model->addAction(tr("Direct Move Part Mode"));
    directMove->setCheckable(true);
    connect(directMove, &QAction::toggled, viewport_, &CadViewport::setDirectMoveMode);
    auto* transform = model->addAction(tr("Combined Transform Gizmo"));
    transform->setCheckable(true);
    connect(transform, &QAction::toggled, this, [this, directMove, interactiveRotate](bool enabled) {
        if (enabled) {
            directMove->setChecked(true);
            interactiveRotate->setChecked(true);
        } else {
            directMove->setChecked(false);
            interactiveRotate->setChecked(false);
        }
    });
    auto* angleMenu = model->addMenu(tr("Angle Snapping"));
    auto* angleGroup = new QActionGroup(this);
    angleGroup->setExclusive(true);
    for (const int step : {0, 5, 15, 45}) {
        auto* action = angleMenu->addAction(step == 0 ? tr("Off") : tr("%1 degrees").arg(step));
        action->setCheckable(true);
        action->setChecked(step == 15);
        angleGroup->addAction(action);
        connect(action, &QAction::triggered, viewport_, [this,step]() {
            viewport_->setAngleSnap(static_cast<double>(step));
        });
    }
    auto* snapMenu = model->addMenu(tr("Face &Snapping"));
    auto* snapGroup = new QActionGroup(this);
    snapGroup->setExclusive(true);
    for (const int step : {0, 1, 5, 10}) {
        auto* action = snapMenu->addAction(step == 0
            ? tr("Off") : tr("%1 mm").arg(step));
        action->setCheckable(true);
        action->setChecked(step == 1);
        snapGroup->addAction(action);
        connect(action, &QAction::triggered, viewport_, [this, step]() {
            viewport_->setSnapStep(static_cast<double>(step));
        });
    }
    auto* viewMenu = menuBar()->addMenu(tr("&View"));
    auto* fitAction = viewMenu->addAction(tr("&Fit All"));
    connect(fitAction, &QAction::triggered, viewport_, &CadViewport::fitAll);
    auto* help = menuBar()->addMenu(tr("&Help"));
    connect(help->addAction(tr("&About")), &QAction::triggered, this, [this] {
        QMessageBox::about(this, tr("About OpenDesigner"), tr("OpenDesigner 0.19 — Duplicate Parts prototype."));
    });
    auto* toolbar = addToolBar(tr("Modeling"));
    toolbar->addAction(boxAction);
    toolbar->addAction(duplicateAction);
    toolbar->addAction(pullAction);
    toolbar->addAction(movePartAction);
    toolbar->addAction(rotatePartAction);
    toolbar->addAction(interactiveMove);
    toolbar->addAction(interactiveRotate);
    toolbar->addAction(directMove);
    toolbar->addAction(transform);
    toolbar->addAction(fitAction);
    toolbar->addSeparator();
    toolbar->addAction(undoAction);
    toolbar->addAction(redoAction);
    connect(viewport_, &CadViewport::partCreated, this, [this, root](const QString& name) {
        auto* item = new QTreeWidgetItem(root, QStringList{name});
        item->setData(0, Qt::UserRole, root->childCount() - 1);
        item->setFlags(item->flags() | Qt::ItemIsEditable);
        item->setText(1, QStringLiteral("👁"));
        root->setExpanded(true);
        statusBar()->showMessage(name + tr(" created"), 3000);
    });
    connect(structure_, &QTreeWidget::itemSelectionChanged, this, [this, root]() {
        std::vector<size_t> visibleIndices;
        for (auto* item : structure_->selectedItems()) {
            if (item->parent() != root) continue;
            const size_t index = static_cast<size_t>(item->data(0, Qt::UserRole).toULongLong());
            // Hidden items stay selected in the browser for batch Show/Hide,
            // but are excluded from the 3D manipulator.
            if (viewport_->isPartVisible(index)) visibleIndices.push_back(index);
        }
        // The viewport emits partSelectionChanged when its selection changes.
        // Do not let that signal clear the hidden browser-only selection.
        const QSignalBlocker blocker(viewport_);
        viewport_->selectParts(visibleIndices);
    });
    connect(viewport_, &CadViewport::partSelectionChanged, this, [this, root](const std::vector<size_t>& indices) {
        const QSignalBlocker blocker(structure_);
        structure_->clearSelection();
        for (const size_t index : indices) {
            if (index < static_cast<size_t>(root->childCount()))
                root->child(static_cast<int>(index))->setSelected(true);
        }
    });
    // Visibility is a one-click control. Apply it to all selected parts when
    // clicking the eye of an already selected part, otherwise to that part only.
    auto setVisibility = [this, root](const QList<QTreeWidgetItem*>& items, bool visible) {
        const QSignalBlocker blocker(structure_);
        for (auto* item : items) {
            if (!item || item->parent() != root) continue;
            const size_t index = static_cast<size_t>(item->data(0, Qt::UserRole).toULongLong());
            viewport_->setPartVisible(index, visible);
            item->setText(1, visible ? QStringLiteral("👁") : QStringLiteral("—"));
            item->setForeground(0, visible ? QBrush() : QBrush(QColor(Qt::gray)));
        }
    };
    connect(structure_, &QTreeWidget::itemClicked, this,
            [this, root, setVisibility](QTreeWidgetItem* item, int column) {
        if (item->parent() != root || column != 1) return;
        const size_t index = static_cast<size_t>(item->data(0, Qt::UserRole).toULongLong());
        const bool visible = !viewport_->isPartVisible(index);
        setVisibility({item}, visible);
    });
    connect(structure_, &QWidget::customContextMenuRequested, this,
            [this, root, setVisibility](const QPoint& pos) {
        auto* item = structure_->itemAt(pos);
        if (!item || item->parent() != root) return;
        QList<QTreeWidgetItem*> targets;
        if (item->isSelected()) {
            for (auto* selected : structure_->selectedItems())
                if (selected->parent() == root) targets.append(selected);
        }
        if (targets.isEmpty()) targets.append(item);
        const size_t index = static_cast<size_t>(item->data(0, Qt::UserRole).toULongLong());
        QMenu menu(structure_);
        auto* select = menu.addAction(tr("Select"));
        auto* rename = menu.addAction(tr("Rename (F2)"));
        auto* duplicate = menu.addAction(tr("Duplicate Selected Parts"));
        menu.addSeparator();
        auto* hide = menu.addAction(targets.size() > 1 ? tr("Hide Selected Parts") : tr("Hide"));
        auto* show = menu.addAction(targets.size() > 1 ? tr("Show Selected Parts") : tr("Show"));
        menu.addSeparator();
        auto* fit = menu.addAction(tr("Fit Selection"));
        auto* chosen = menu.exec(structure_->viewport()->mapToGlobal(pos));
        if (chosen == select) {
            structure_->clearSelection();
            item->setSelected(true);
        } else if (chosen == duplicate) {
            std::vector<size_t> indices;
            for (auto* target : targets) {
                const size_t index = static_cast<size_t>(target->data(0, Qt::UserRole).toULongLong());
                if (viewport_->isPartVisible(index)) indices.push_back(index);
            }
            viewport_->selectParts(indices);
            viewport_->duplicateSelectedParts();
        } else if (chosen == rename) {
            structure_->setCurrentItem(item, 0);
            structure_->editItem(item, 0);
        } else if (chosen == hide) {
            setVisibility(targets, false);
        } else if (chosen == show) {
            setVisibility(targets, true);
        } else if (chosen == fit) {
            std::vector<size_t> indices;
            for (auto* target : targets)
                indices.push_back(static_cast<size_t>(target->data(0, Qt::UserRole).toULongLong()));
            viewport_->fitSelection(indices);
        }
    });
    connect(viewport_, &CadViewport::statusMessage, this, [this](const QString& msg) {
        statusBar()->showMessage(msg, 5000);
    });
    statusBar()->showMessage(tr("Left: select face | Drag orange arrow: Pull Face / Move Part (toggle mode) | Shift during drag: free movement | Middle: orbit | Shift+middle: pan | Wheel: zoom"));
}
