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
    setWindowTitle("OpenDesigner 0.16 (prototype)");
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
        QMessageBox::about(this, tr("About OpenDesigner"), tr("OpenDesigner 0.15 — Interactive Rotate Part prototype."));
    });
    auto* toolbar = addToolBar(tr("Modeling"));
    toolbar->addAction(boxAction);
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
        std::vector<size_t> indices;
        for (auto* item : structure_->selectedItems()) {
            if (item->parent() != root) continue;
            indices.push_back(static_cast<size_t>(item->data(0, Qt::UserRole).toULongLong()));
        }
        viewport_->selectParts(indices);
    });
    connect(viewport_, &CadViewport::partSelectionChanged, this, [this, root](const std::vector<size_t>& indices) {
        const QSignalBlocker blocker(structure_);
        structure_->clearSelection();
        for (const size_t index : indices) {
            if (index < static_cast<size_t>(root->childCount()))
                root->child(static_cast<int>(index))->setSelected(true);
        }
    });
    connect(structure_, &QTreeWidget::itemDoubleClicked, this,
            [this, root](QTreeWidgetItem* item, int column) {
        if (item->parent() != root || column != 1) return;
        const size_t index = static_cast<size_t>(item->data(0, Qt::UserRole).toULongLong());
        const bool visible = !viewport_->isPartVisible(index);
        viewport_->setPartVisible(index, visible);
        item->setText(1, visible ? QStringLiteral("👁") : QStringLiteral("—"));
        item->setForeground(0, visible ? QBrush() : QBrush(QColor(Qt::gray)));
    });
    connect(structure_, &QWidget::customContextMenuRequested, this,
            [this, root](const QPoint& pos) {
        auto* item = structure_->itemAt(pos);
        if (!item || item->parent() != root) return;
        const size_t index = static_cast<size_t>(item->data(0, Qt::UserRole).toULongLong());
        QMenu menu(structure_);
        auto* select = menu.addAction(tr("Select"));
        auto* rename = menu.addAction(tr("Rename (F2)"));
        const bool visible = viewport_->isPartVisible(index);
        auto* toggle = menu.addAction(visible ? tr("Hide") : tr("Show"));
        auto* fit = menu.addAction(tr("Fit Selection"));
        auto* chosen = menu.exec(structure_->viewport()->mapToGlobal(pos));
        if (chosen == select) {
            structure_->clearSelection();
            item->setSelected(true);
        } else if (chosen == rename) {
            structure_->setCurrentItem(item, 0);
            structure_->editItem(item, 0);
        } else if (chosen == toggle) {
            viewport_->setPartVisible(index, !visible);
            item->setText(1, visible ? QStringLiteral("—") : QStringLiteral("👁"));
            item->setForeground(0, visible ? QBrush(QColor(Qt::gray)) : QBrush());
        } else if (chosen == fit) {
            viewport_->fitSelection({index});
        }
    });
    connect(viewport_, &CadViewport::statusMessage, this, [this](const QString& msg) {
        statusBar()->showMessage(msg, 5000);
    });
    statusBar()->showMessage(tr("Left: select face | Drag orange arrow: Pull Face / Move Part (toggle mode) | Shift during drag: free movement | Middle: orbit | Shift+middle: pan | Wheel: zoom"));
}
