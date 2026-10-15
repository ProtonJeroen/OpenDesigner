#include "MainWindow.h"
#include "CadViewport.h"
#include <QTreeWidget>
#include <QDockWidget>
#include <QMenuBar>
#include <QToolBar>
#include <QStatusBar>
#include <QAction>
#include <QKeySequence>
#include <QMessageBox>

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("OpenDesigner 0.4 (prototype)");
    resize(1280, 800);
    viewport_ = new CadViewport(this);
    setCentralWidget(viewport_);
    auto* dock = new QDockWidget(tr("Structure Browser"), this);
    structure_ = new QTreeWidget(dock);
    structure_->setHeaderLabel(tr("Model"));
    auto* root = new QTreeWidgetItem(structure_, QStringList{tr("Assembly 1")});
    root->setExpanded(true);
    dock->setWidget(structure_);
    addDockWidget(Qt::LeftDockWidgetArea, dock);

    auto* file = menuBar()->addMenu(tr("&File"));
    auto* exitAction = file->addAction(tr("E&xit"));
    connect(exitAction, &QAction::triggered, this, &QWidget::close);
    auto* edit = menuBar()->addMenu(tr("&Edit"));
    auto* undoAction = edit->addAction(tr("&Undo Pull Face"));
    undoAction->setShortcut(QKeySequence::Undo);
    connect(undoAction, &QAction::triggered, viewport_, &CadViewport::undoPull);
    auto* redoAction = edit->addAction(tr("&Redo Pull Face"));
    redoAction->setShortcut(QKeySequence::Redo);
    connect(redoAction, &QAction::triggered, viewport_, &CadViewport::redoPull);
    auto* model = menuBar()->addMenu(tr("&Modeling"));
    auto* boxAction = model->addAction(tr("Create &Box"));
    connect(boxAction, &QAction::triggered, viewport_, &CadViewport::addBox);
    auto* pullAction = model->addAction(tr("&Pull Face..."));
    connect(pullAction, &QAction::triggered, viewport_, &CadViewport::pullSelectedFace);
    auto* viewMenu = menuBar()->addMenu(tr("&View"));
    auto* fitAction = viewMenu->addAction(tr("&Fit All"));
    connect(fitAction, &QAction::triggered, viewport_, &CadViewport::fitAll);
    auto* help = menuBar()->addMenu(tr("&Help"));
    connect(help->addAction(tr("&About")), &QAction::triggered, this, [this] {
        QMessageBox::about(this, tr("About OpenDesigner"), tr("OpenDesigner 0.4 — interactive face manipulator prototype."));
    });
    auto* toolbar = addToolBar(tr("Modeling"));
    toolbar->addAction(boxAction);
    toolbar->addAction(pullAction);
    toolbar->addAction(fitAction);
    toolbar->addSeparator();
    toolbar->addAction(undoAction);
    toolbar->addAction(redoAction);
    connect(viewport_, &CadViewport::partCreated, this, [this, root](const QString& name) {
        new QTreeWidgetItem(root, QStringList{name});
        root->setExpanded(true);
        statusBar()->showMessage(name + tr(" created"), 3000);
    });
    connect(viewport_, &CadViewport::statusMessage, this, [this](const QString& msg) {
        statusBar()->showMessage(msg, 5000);
    });
    statusBar()->showMessage(tr("Left: select face | Drag orange arrow or Ctrl+left drag: pull face | Middle: orbit | Shift+middle: pan | Wheel: zoom"));
}
