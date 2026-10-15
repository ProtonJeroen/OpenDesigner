#include "MainWindow.h"
#include "CadViewport.h"
#include <QTreeWidget>
#include <QDockWidget>
#include <QMenuBar>
#include <QToolBar>
#include <QStatusBar>
#include <QAction>
#include <QMessageBox>

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("OpenDesigner 0.2 (prototype)");
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
        QMessageBox::about(this, tr("About OpenDesigner"), tr("OpenDesigner 0.2 — direct modeling prototype."));
    });
    auto* toolbar = addToolBar(tr("Modeling"));
    toolbar->addAction(boxAction);
    toolbar->addAction(pullAction);
    toolbar->addAction(fitAction);
    connect(viewport_, &CadViewport::partCreated, this, [this, root](const QString& name) {
        new QTreeWidgetItem(root, QStringList{name});
        root->setExpanded(true);
        statusBar()->showMessage(name + tr(" created"), 3000);
    });
    connect(viewport_, &CadViewport::statusMessage, this, [this](const QString& msg) {
        statusBar()->showMessage(msg, 5000);
    });
    statusBar()->showMessage(tr("Left: select face | Middle: orbit | Shift+middle: pan | Wheel: zoom"));
}
