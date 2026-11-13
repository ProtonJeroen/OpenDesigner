#include "MainWindow.h"
#include <QKeyEvent>
#include <QApplication>
#include "CadViewport.h"
#include "NativeDocument.h"
#include <QTreeWidget>
#include <QSignalBlocker>
#include <QAbstractItemView>
#include <QMenu>
#include <QHeaderView>
#include <QBrush>
#include <QColor>
#include <QColorDialog>
#include <QDockWidget>
#include <QMenuBar>
#include <QToolBar>
#include <QStatusBar>
#include <QAction>
#include <QKeySequence>
#include <QMessageBox>
#include <QActionGroup>
#include <QFileDialog>
#include <QCloseEvent>
#include <QFileInfo>
#include <QComboBox>
#include <QSpinBox>
#include <QLabel>
#include <QIcon>
#include <algorithm>

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("OpenDesigner 0.5.3");
    resize(1280, 800);
    viewport_ = new CadViewport(this);
    connect(viewport_,&CadViewport::geometryOperationFailed,this,[this](const QString& operation,const QString& reason) {
        auto* dialog=findChild<QMessageBox*>(QStringLiteral("geometryErrorDialog"));
        if(!dialog) {
            dialog=new QMessageBox(QMessageBox::Warning,tr("Geometry operation failed"),QString(),QMessageBox::Ok,this);
            dialog->setObjectName(QStringLiteral("geometryErrorDialog"));
            dialog->setAttribute(Qt::WA_DeleteOnClose);
        }
        dialog->setText(tr("%1 could not produce valid closed solid geometry. Original geometry was kept.").arg(operation));
        dialog->setInformativeText(reason);
        dialog->open();
    });
    setCentralWidget(viewport_);
    auto* dock = new QDockWidget(tr("Structure Browser"), this);
    structure_ = new QTreeWidget(dock);
    structure_->setColumnCount(2);
    structure_->setHeaderLabels({tr("Model"), tr("Visible")});
    structure_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    structure_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    structure_->setContextMenuPolicy(Qt::CustomContextMenu);
    structure_->installEventFilter(this);
    structure_->viewport()->installEventFilter(this);
    viewport_->installEventFilter(this);
    structure_->setEditTriggers(QAbstractItemView::EditKeyPressed | QAbstractItemView::DoubleClicked);
    structure_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    root_ = new QTreeWidgetItem(structure_, QStringList{tr("Assembly 1")});
    constructionRoot_=new QTreeWidgetItem(structure_,QStringList{tr("Construction Geometry")});
    constructionRoot_->setExpanded(true);
    auto* root = root_;
    root->setExpanded(true);
    dock->setWidget(structure_);
    addDockWidget(Qt::LeftDockWidgetArea, dock);

    auto* file = menuBar()->addMenu(tr("&File"));
    auto* newAction = file->addAction(tr("&New"));
    newAction->setShortcut(QKeySequence::New);
    connect(newAction, &QAction::triggered, this, &MainWindow::newDocument);
    auto* openAction = file->addAction(tr("&Open..."));
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, &MainWindow::openDocument);
    saveAction_ = file->addAction(tr("&Save"));
    saveAction_->setShortcut(QKeySequence::Save);
    connect(saveAction_, &QAction::triggered, this, &MainWindow::saveDocument);
    saveAsAction_ = file->addAction(tr("Save &As..."));
    saveAsAction_->setShortcut(QKeySequence::SaveAs);
    connect(saveAsAction_, &QAction::triggered, this, &MainWindow::saveDocumentAs);
    file->addSeparator();
    auto* debugExport=file->addAction(tr("Export Debug Model..."));
    debugExport->setObjectName("exportDebugModelAction");
    connect(debugExport,&QAction::triggered,this,[this] {
        const auto path=QFileDialog::getSaveFileName(this,tr("Export Debug Model"),
            QStringLiteral("opendesigner-debug.zip"),tr("Debug model ZIP (*.zip)"));
        if(path.isEmpty()) return;
        QString error;
        QJsonArray browserSelection;
        for(auto* item:structure_->selectedItems())
            if(item->data(0,Qt::UserRole).isValid()) browserSelection.append(QString::number(item->data(0,Qt::UserRole).toULongLong()));
        if(!viewport_->exportDebugModel(path,error,browserSelection))
            QMessageBox::warning(this,tr("Debug export failed"),error);
        else statusBar()->showMessage(tr("Debug model exported to %1").arg(path),10000);
    });
    file->addSeparator();
    auto* exitAction = file->addAction(tr("E&xit"));
    connect(exitAction, &QAction::triggered, this, &QWidget::close);
    auto* edit = menuBar()->addMenu(tr("&Edit"));
    auto* undoAction=viewport_->undoAction();
    auto* redoAction=viewport_->redoAction();
    edit->addAction(undoAction);
    edit->addAction(redoAction);
    auto* model = menuBar()->addMenu(tr("&Modeling"));
    auto* sketchMenu=model->addMenu(tr("2D Modeling"));sketchMenu->setObjectName("modeling2DMenu");
    auto* editSketch=sketchMenu->addAction(tr("Edit Sketch"));editSketch->setObjectName("sketch.edit");
    connect(editSketch,&QAction::triggered,viewport_,[this]{viewport_->activateSketchWorkplane();});
    auto* finishSketch=sketchMenu->addAction(tr("Finish 2D"));finishSketch->setObjectName("sketch.finish");finishSketch->setEnabled(false);
    connect(finishSketch,&QAction::triggered,viewport_,&CadViewport::endSketchEditing);
    auto* normalSketch=sketchMenu->addAction(tr("Look at Workplane"));normalSketch->setObjectName("sketch.normal");normalSketch->setEnabled(false);
    connect(normalSketch,&QAction::triggered,viewport_,&CadViewport::lookNormalToSketch);
    sketchMenu->addSeparator();
    auto* construction2D=sketchMenu->addAction(tr("Construction geometry"));construction2D->setObjectName("sketch.construction");construction2D->setCheckable(true);
    const auto drawAction=[this,sketchMenu,construction2D](const QString& text,const char* name,Sketch::DrawTool tool) {
        auto* action=sketchMenu->addAction(text);action->setObjectName(name);
        connect(action,&QAction::triggered,viewport_,[this,construction2D,tool] {
            viewport_->startSketchDrawing(tool,construction2D->isChecked()?Sketch::Role::Construction:Sketch::Role::Profile);
        });return action;
    };
    auto* line2D=drawAction(tr("Line"),"sketch.line",Sketch::DrawTool::Segment);
    auto* infinite2D=drawAction(tr("Infinite Construction Line"),"sketch.infinite",Sketch::DrawTool::InfiniteLine);
    auto* horizontal2D=drawAction(tr("Horizontal Infinite Construction Line"),"sketch.infinite.horizontal",Sketch::DrawTool::HorizontalInfiniteLine);
    auto* vertical2D=drawAction(tr("Vertical Infinite Construction Line"),"sketch.infinite.vertical",Sketch::DrawTool::VerticalInfiniteLine);
    horizontal2D->setIcon(Sketch::orthogonalLineIcon(Sketch::DrawTool::HorizontalInfiniteLine));
    vertical2D->setIcon(Sketch::orthogonalLineIcon(Sketch::DrawTool::VerticalInfiniteLine));
    auto* circle2D=drawAction(tr("Circle"),"sketch.circle",Sketch::DrawTool::Circle);
    auto* arc2D=drawAction(tr("Arc (center/start/end)"),"sketch.arc",Sketch::DrawTool::Arc);
    auto* ellipse2D=drawAction(tr("Ellipse"),"sketch.ellipse",Sketch::DrawTool::Ellipse);
    auto* ellipticalArc2D=drawAction(tr("Elliptical Arc"),"sketch.elliptical_arc",Sketch::DrawTool::EllipticalArc);
    auto* axes2D=sketchMenu->addMenu(tr("Construction Axes"));axes2D->setObjectName("sketch.axes");axes2D->setEnabled(false);
    viewport_->populateSketchAxesMenu(*axes2D);
    auto* overdraw=sketchMenu->addAction(tr("Overdraw"));overdraw->setObjectName("sketch.overdraw");overdraw->setEnabled(false);
    connect(overdraw,&QAction::triggered,viewport_,&CadViewport::startSketchOverdraw);
    auto* offset2D=sketchMenu->addAction(tr("Offset Construction Line"));offset2D->setObjectName("sketch.offset_line");offset2D->setEnabled(false);
    connect(offset2D,&QAction::triggered,viewport_,[this]{viewport_->startSketchConstruction(Sketch::ConstructionTool::OffsetLine);});
    auto* tangent2D=sketchMenu->addAction(tr("Common Tangent Construction Line"));tangent2D->setObjectName("sketch.common_tangent");tangent2D->setEnabled(false);
    connect(tangent2D,&QAction::triggered,viewport_,[this]{viewport_->startSketchConstruction(Sketch::ConstructionTool::CommonTangent);});
    auto* projection2D=sketchMenu->addAction(tr("Project 3D Geometry"));projection2D->setObjectName("sketch.project_3d");projection2D->setEnabled(false);
    connect(projection2D,&QAction::triggered,viewport_,&CadViewport::startSketchProjection);
    auto* snap2D=sketchMenu->addAction(tr("Snapping"));snap2D->setObjectName("sketch.snapping");snap2D->setCheckable(true);snap2D->setChecked(true);
    connect(snap2D,&QAction::toggled,viewport_,&CadViewport::setSketchSnapping);
    connect(viewport_,&CadViewport::activeSketchWorkplaneChanged,this,[this,finishSketch,normalSketch,overdraw,offset2D,tangent2D,axes2D,projection2D](const QString& id) {
        projection2D->setEnabled(!id.isEmpty());
        axes2D->setEnabled(!id.isEmpty());
        finishSketch->setEnabled(!id.isEmpty());normalSketch->setEnabled(!id.isEmpty());overdraw->setEnabled(!id.isEmpty());
        offset2D->setEnabled(!id.isEmpty());tangent2D->setEnabled(!id.isEmpty());rebuildConstructionBrowser();rebuildSketchControls();
    });
    auto* constructionMenu=model->addMenu(tr("Construction Geometry"));
    for(const auto kind:{Construction::Kind::Workplane,Construction::Kind::Axis,Construction::Kind::Point}) {
        auto* group=constructionMenu->addMenu(Construction::kindName(kind));
        for(const auto& definition:Construction::definitions()) if(definition.kind==kind) {
            auto* action=group->addAction(definition.name);
            action->setObjectName(QString("construction.method.%1").arg(int(definition.method)));
            connect(action,&QAction::triggered,viewport_,[this,method=definition.method]{viewport_->startConstruction(method);});
        }
    }
    auto* model3D=model->addMenu(tr("3D Modeling"));model3D->setObjectName("modeling3DMenu");
    auto* extrude=model3D->addAction(tr("Extrude"));extrude->setObjectName("sketch.extrude");extrude->setEnabled(false);
    connect(extrude,&QAction::triggered,viewport_,&CadViewport::startProfileExtrusion);
    connect(viewport_,&CadViewport::activeSketchWorkplaneChanged,extrude,[extrude](const QString& id){extrude->setEnabled(!id.isEmpty());});
    auto* revolve=model3D->addAction(tr("Revolve"));revolve->setObjectName("sketch.revolve");revolve->setEnabled(false);
    connect(revolve,&QAction::triggered,viewport_,&CadViewport::startProfileRevolution);
    connect(viewport_,&CadViewport::activeSketchWorkplaneChanged,revolve,[revolve](const QString& id){revolve->setEnabled(!id.isEmpty());});
    model3D->addSeparator();
    auto* boxAction = model3D->addAction(tr("Create &Box"));
    connect(boxAction, &QAction::triggered, viewport_, &CadViewport::addBox);
    auto* cylinderAction = model3D->addAction(tr("Create &Cylinder"));
    connect(cylinderAction, &QAction::triggered, viewport_, &CadViewport::addCylinder);
    auto* sphereAction = model3D->addAction(tr("Create &Sphere"));
    connect(sphereAction, &QAction::triggered, viewport_, &CadViewport::addSphere);
    auto* coneAction = model3D->addAction(tr("Create C&one"));
    connect(coneAction, &QAction::triggered, viewport_, &CadViewport::addCone);
    auto* booleanMenu = model3D->addMenu(tr("&Boolean"));
    connect(booleanMenu->addAction(tr("&Unite")),&QAction::triggered,viewport_,&CadViewport::uniteSelectedParts);
    connect(booleanMenu->addAction(tr("&Subtract...")),&QAction::triggered,viewport_,&CadViewport::subtractSelectedParts);
    connect(booleanMenu->addAction(tr("&Intersect")),&QAction::triggered,viewport_,&CadViewport::intersectSelectedParts);
    auto* duplicateAction = model3D->addAction(tr("&Duplicate Selected Parts"));
    duplicateAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_D));
    connect(duplicateAction, &QAction::triggered, viewport_, &CadViewport::duplicateSelectedParts);
    auto* moveFaceAction = model3D->addAction(tr("Move Face"));
    connect(moveFaceAction,&QAction::triggered,viewport_,&CadViewport::moveFace);
    auto* filletAction=model3D->addAction(tr("Fillet"));
    connect(filletAction,&QAction::triggered,viewport_,&CadViewport::fillet);
    auto* chamferAction=model3D->addAction(tr("Chamfer"));
    connect(chamferAction,&QAction::triggered,viewport_,&CadViewport::chamfer);
    auto* rotateFaceAction = model3D->addAction(tr("Rotate Face"));
    connect(rotateFaceAction,&QAction::triggered,viewport_,&CadViewport::rotateFace);
    auto* alignFaceAction = model3D->addAction(tr("Align Face"));
    connect(alignFaceAction,&QAction::triggered,viewport_,&CadViewport::alignFace);
    auto* deleteFaceAction=model3D->addAction(tr("Delete Face"));
    deleteFaceAction->setObjectName(QStringLiteral("deleteFaceAction"));
    connect(deleteFaceAction,&QAction::triggered,viewport_,&CadViewport::deleteFace);
    auto* pullAction = model3D->addAction(tr("Primitive Pull / Resize"));
    connect(pullAction, &QAction::triggered, viewport_, &CadViewport::startPrimitivePull);
    auto* movePartAction = model3D->addAction(tr("Move &Part Along Face..."));
    connect(movePartAction, &QAction::triggered, viewport_, &CadViewport::movePartAlongSelectedFace);
    auto* rotatePartAction = model3D->addAction(tr("&Rotate Part..."));
    connect(rotatePartAction, &QAction::triggered, viewport_, &CadViewport::rotateSelectedPart);
    auto* interactiveMove = model3D->addAction(tr("Interactive &Move Part Along Face"));
    interactiveMove->setCheckable(true);
    connect(interactiveMove, &QAction::toggled, viewport_, &CadViewport::setMovePartMode);
    auto* interactiveRotate = model3D->addAction(tr("Interactive &Rotate Part"));
    interactiveRotate->setCheckable(true);
    connect(interactiveRotate, &QAction::toggled, viewport_, &CadViewport::setRotateMode);
    auto* directMove = model3D->addAction(tr("Interactive Move Part"));
    directMove->setCheckable(true);
    connect(directMove, &QAction::toggled, viewport_, &CadViewport::setDirectMoveMode);
    auto* angleMenu = model3D->addMenu(tr("Angle Snapping"));
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
    auto* snapMenu = model3D->addMenu(tr("Face &Snapping"));
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
    // v0.5.3 view-level display modes (not saved, not undoable).
    viewMenu->addSeparator();
    auto* displayModes=new QActionGroup(this);
    auto* shaded=viewMenu->addAction(tr("&Shaded"));shaded->setObjectName("view.shaded");shaded->setCheckable(true);shaded->setChecked(true);
    auto* wireframe=viewMenu->addAction(tr("&Wireframe"));wireframe->setObjectName("view.wireframe");wireframe->setCheckable(true);
    displayModes->addAction(shaded);displayModes->addAction(wireframe);
    connect(shaded,&QAction::triggered,viewport_,[this]{viewport_->setWireframe(false);});
    connect(wireframe,&QAction::triggered,viewport_,[this]{viewport_->setWireframe(true);});
    auto* show3D=viewMenu->addAction(tr("Show &3D Geometry"));show3D->setObjectName("view.show_3d");show3D->setCheckable(true);show3D->setChecked(true);
    show3D->setToolTip(tr("Temporarily show or hide all solids without changing Part visibility"));
    connect(show3D,&QAction::toggled,viewport_,[this](bool on){viewport_->setShow3DGeometry(on);});
    // v0.5.4: same pattern for Workplane frames and their Sketches.
    auto* showWorkplanes=viewMenu->addAction(tr("Show W&orkplanes"));showWorkplanes->setObjectName("view.show_workplanes");
    showWorkplanes->setCheckable(true);showWorkplanes->setChecked(true);
    showWorkplanes->setToolTip(tr("Temporarily show or hide all Workplanes and their Sketches without changing their visibility; Axes and Points stay visible"));
    connect(showWorkplanes,&QAction::toggled,viewport_,[this](bool on){viewport_->setShowWorkplanes(on);});
    connect(viewport_,&CadViewport::viewModesChanged,this,[this,shaded,wireframe,show3D,showWorkplanes] {
        const QSignalBlocker a(shaded),b(wireframe),c(show3D),d(showWorkplanes);
        wireframe->setChecked(viewport_->wireframe());shaded->setChecked(!viewport_->wireframe());show3D->setChecked(viewport_->show3DGeometry());
        showWorkplanes->setChecked(viewport_->showWorkplanes());
    });
    viewMenu->addSeparator();
    // Explicit camera alignment; activating a Workplane never moves the camera.
    auto* lookAtWorkplane=viewMenu->addAction(tr("&Look at Workplane"));lookAtWorkplane->setObjectName("view.look_at_workplane");
    lookAtWorkplane->setToolTip(tr("Look normal to the active or selected Workplane (+Y up)"));lookAtWorkplane->setEnabled(false);
    connect(lookAtWorkplane,&QAction::triggered,viewport_,[this]{viewport_->lookAtWorkplane();});
    const auto updateLookAt=[this,lookAtWorkplane]{lookAtWorkplane->setEnabled(!viewport_->lookAtWorkplaneCandidate().isEmpty());};
    connect(viewport_,&CadViewport::activeSketchWorkplaneChanged,lookAtWorkplane,updateLookAt);
    connect(viewport_,&CadViewport::constructionSelectionChanged,lookAtWorkplane,updateLookAt);
    connect(viewport_,&CadViewport::constructionsChanged,lookAtWorkplane,updateLookAt);
    auto* help = menuBar()->addMenu(tr("&Help"));
    connect(help->addAction(tr("&About")), &QAction::triggered, this, [this] {
        QMessageBox::about(this, tr("About OpenDesigner"), tr("OpenDesigner 0.5.3 — Workplanes and geometric references."));
    });
    auto* toolbar = addToolBar(tr("3D Modeling"));toolbar->setObjectName("modeling3DToolbar");
    connect(viewport_,&CadViewport::transformModesChanged,this,[interactiveMove,directMove,interactiveRotate](bool faceNormal,bool move,bool rotate) {
        const QSignalBlocker a(interactiveMove),b(directMove),c(interactiveRotate);
        interactiveMove->setChecked(faceNormal);directMove->setChecked(move);interactiveRotate->setChecked(rotate);
    });
    toolbar->addAction(extrude);
    toolbar->addAction(revolve);
    toolbar->addAction(boxAction);
    toolbar->addAction(duplicateAction);
    toolbar->addAction(pullAction);
    toolbar->addAction(moveFaceAction);
    toolbar->addAction(rotateFaceAction);
    toolbar->addAction(alignFaceAction);
    toolbar->addAction(movePartAction);
    toolbar->addAction(rotatePartAction);
    toolbar->addAction(interactiveMove);
    toolbar->addAction(interactiveRotate);
    toolbar->addAction(directMove);
    toolbar->addAction(fitAction);
    toolbar->addAction(shaded);toolbar->addAction(wireframe);toolbar->addAction(show3D);toolbar->addAction(showWorkplanes);
    toolbar->addSeparator();
    toolbar->addAction(undoAction);
    toolbar->addAction(redoAction);
    addToolBarBreak();auto* sketchToolbar=addToolBar(tr("2D Modeling"));sketchToolbar->setObjectName("sketchToolbar");
    sketchToolbar->addAction(editSketch);
    sketchWorkplane_=new QComboBox(sketchToolbar);sketchWorkplane_->setObjectName("sketchWorkplane");sketchWorkplane_->setMinimumContentsLength(15);
    sketchWorkplane_->setToolTip(tr("Active Workplane for 2D editing"));sketchToolbar->addWidget(sketchWorkplane_);
    connect(sketchWorkplane_,qOverload<int>(&QComboBox::currentIndexChanged),this,[this] {
        const auto id=sketchWorkplane_->currentData().toString();
        if(id.isEmpty()) viewport_->endSketchEditing();else viewport_->activateSketchWorkplane(id);
    });
    sketchToolbar->addAction(normalSketch);sketchToolbar->addAction(finishSketch);sketchToolbar->addSeparator();
    sketchToolbar->addAction(construction2D);sketchToolbar->addAction(line2D);sketchToolbar->addAction(infinite2D);
    sketchToolbar->addAction(horizontal2D);sketchToolbar->addAction(vertical2D);
    sketchToolbar->addAction(circle2D);sketchToolbar->addAction(arc2D);sketchToolbar->addAction(overdraw);
    sketchToolbar->insertAction(overdraw,ellipse2D);sketchToolbar->insertAction(overdraw,ellipticalArc2D);
    sketchToolbar->addAction(axes2D->menuAction());
    sketchToolbar->addAction(offset2D);sketchToolbar->addAction(tangent2D);sketchToolbar->addAction(projection2D);sketchToolbar->addSeparator();sketchToolbar->addAction(snap2D);
    auto* pixels=new QSpinBox(sketchToolbar);pixels->setObjectName("sketchSnapPixels");pixels->setRange(1,100);pixels->setValue(viewport_->sketchSnapPixels());pixels->setSuffix(tr(" px"));
    pixels->setToolTip(tr("2D snap radius in screen pixels; Shift temporarily bypasses snapping"));sketchToolbar->addWidget(pixels);
    connect(pixels,qOverload<int>(&QSpinBox::valueChanged),viewport_,&CadViewport::setSketchSnapPixels);rebuildSketchControls();

    connect(viewport_, &CadViewport::partCreated, this, [this, root](const QString& name) {
        const QSignalBlocker blocker(structure_);
        auto* item = new QTreeWidgetItem(root, QStringList{name});
        item->setData(0, Qt::UserRole, root->childCount() - 1);
        item->setFlags(item->flags() | Qt::ItemIsEditable);
        item->setText(1, QStringLiteral("👁"));
        root->setExpanded(true);
        statusBar()->showMessage(name + tr(" created"), 3000);
        if (!documentTransition_) markDocumentDirty();
    });
    connect(viewport_, &CadViewport::activePartChanged, this, [this] {
        const QSignalBlocker blocker(structure_);
        QPixmap green(14,14);green.fill(QColor("#90D89A"));
        for(int i=0;i<root_->childCount();++i) {
            auto* item=root_->child(i);
            const bool active=viewport_->partId(item->data(0,Qt::UserRole).toULongLong())==viewport_->activePartId() && !viewport_->activePartId().isEmpty();
            item->setIcon(0,active?QIcon(green):QIcon());
            item->setToolTip(0,active?tr("Active Part — current modeling target"):QString());
        }
        if(!documentTransition_)markDocumentDirty();
    });
    connect(viewport_, &CadViewport::partsChanged, this, [this] {
        if (!documentTransition_) markDocumentDirty();
    });
    connect(viewport_, &CadViewport::sketchEntitiesChanged,this,[this] {
        rebuildConstructionBrowser();if(!documentTransition_) markDocumentDirty();
    });
    connect(viewport_, &CadViewport::sketchSelectionChanged,this,[this](const QStringList& ids) {
        const QSignalBlocker blocker(structure_);
        for(int i=0;i<constructionRoot_->childCount();++i) {
            auto* plane=constructionRoot_->child(i);
            for(int j=0;j<plane->childCount();++j) {
                auto* item=plane->child(j);item->setSelected(ids.contains(item->data(0,Qt::UserRole).toString()));
            }
        }
        if(ids.empty() && structure_->currentItem() && structure_->currentItem()->data(0,Qt::UserRole+1).toString()=="sketch") structure_->setCurrentItem(nullptr);
    });
    connect(viewport_, &CadViewport::constructionsChanged,this,[this] {
        rebuildConstructionBrowser();if(!documentTransition_) markDocumentDirty();
    });
    connect(viewport_, &CadViewport::constructionSelectionChanged,this,[this](const QStringList& ids) {
        const QSignalBlocker blocker(structure_);
        for(int i=0;i<constructionRoot_->childCount();++i) {
            auto* item=constructionRoot_->child(i);item->setSelected(ids.contains(item->data(0,Qt::UserRole).toString()));
        }
        if(ids.empty() && structure_->currentItem() && structure_->currentItem()->parent()==constructionRoot_) structure_->setCurrentItem(nullptr);
    });
    connect(viewport_, &CadViewport::documentReplaced, this, [this] {
        rebuildStructureBrowser();rebuildConstructionBrowser();
    });
    // Rows retain their stable slot IDs. Boolean-consumed parts and undone
    // results are hidden rather than renumbering other browser references.
    connect(viewport_, &CadViewport::partsChanged, this, [this,root]() {
        const QSignalBlocker blocker(structure_);
        for (int i = 0; i < root->childCount(); ++i) {
            auto* item = root->child(i);
            const size_t index = item->data(0,Qt::UserRole).toULongLong();
            const bool active = viewport_->isPartActive(index);
            item->setHidden(!active);
            if (!active) { item->setSelected(false); continue; }
            item->setText(0,viewport_->partName(index));
            const bool visible = viewport_->isPartVisible(index);
            item->setText(1,visible ? QStringLiteral("👁") : QStringLiteral("—"));
            item->setForeground(0,visible ? QBrush() : QBrush(QColor(Qt::gray)));
        }
    });
    connect(structure_, &QTreeWidget::itemChanged, this, [this,root](QTreeWidgetItem* item,int column) {
        if(column==0 && item->data(0,Qt::UserRole+1).toString()=="sketch") {
            const auto id=item->data(0,Qt::UserRole).toString();
            for(const auto& entity:viewport_->sketchEntities()) if(entity.id==id) {
                auto updated=entity;updated.name=item->text(0);QString error;
                if(!viewport_->updateSketchEntity(updated,error)) {rebuildConstructionBrowser();statusBar()->showMessage(error);}
                break;
            }
            return;
        }
        if(column==0 && item->parent()==constructionRoot_) {
            const auto id=item->data(0,Qt::UserRole).toString();
            for(const auto& object:viewport_->constructionObjects()) if(object.id==id) {
                auto updated=object;updated.name=item->text(0);if(!viewport_->updateConstruction(updated)) rebuildConstructionBrowser();break;
            }
            return;
        }
        if (column == 0 && item->parent() == root)
            viewport_->renamePart(item->data(0,Qt::UserRole).toULongLong(),item->text(0));
    });
    connect(structure_, &QTreeWidget::itemSelectionChanged, this, [this, root]() {
        QStringList sketchIds;
        for(auto* item:structure_->selectedItems()) if(item->data(0,Qt::UserRole+1).toString()=="sketch") sketchIds.push_back(item->data(0,Qt::UserRole).toString());
        if(!sketchIds.empty()) {viewport_->selectSketchEntities(sketchIds);return;}
        QStringList constructionIds;
        for(auto* item:structure_->selectedItems()) if(item->parent()==constructionRoot_) constructionIds.push_back(item->data(0,Qt::UserRole).toString());
        if(!constructionIds.empty()) {
            viewport_->selectConstructions(constructionIds);return;
        }
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
        if(indices.empty()) structure_->setCurrentItem(nullptr);
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
        if(item->data(0,Qt::UserRole+1).toString()=="sketch" && column==1) {
            const auto id=item->data(0,Qt::UserRole).toString();
            for(const auto& entity:viewport_->sketchEntities()) if(entity.id==id) {
                auto updated=entity;updated.visible=!updated.visible;QString error;
                if(!viewport_->updateSketchEntity(updated,error)) statusBar()->showMessage(error);
                break;
            }
            return;
        }
        if(item->parent()==constructionRoot_ && column==1) {
            const auto id=item->data(0,Qt::UserRole).toString();
            for(const auto& object:viewport_->constructionObjects()) if(object.id==id) {auto updated=object;updated.visible=!updated.visible;viewport_->updateConstruction(updated);break;}
            return;
        }
        if (item->parent() != root || column != 1) return;
        const size_t index = static_cast<size_t>(item->data(0, Qt::UserRole).toULongLong());
        const bool visible = !viewport_->isPartVisible(index);
        setVisibility({item}, visible);
    });
    connect(structure_, &QWidget::customContextMenuRequested, this,
            [this, root, setVisibility](const QPoint& pos) {
        auto* item = structure_->itemAt(pos);
        if(!item || item==root) {
            QMenu menu(structure_);auto* clear=menu.addAction(tr("Clear Active Part"));clear->setEnabled(!viewport_->activePartId().isEmpty());
            if(menu.exec(structure_->viewport()->mapToGlobal(pos))==clear)viewport_->setActivePart({});return;
        }
        if(item->data(0,Qt::UserRole+1).toString()=="sketch") {
            QStringList ids;
            if(item->isSelected()) for(auto* selected:structure_->selectedItems())
                if(selected->data(0,Qt::UserRole+1).toString()=="sketch") ids.push_back(selected->data(0,Qt::UserRole).toString());
            if(ids.empty()) ids.push_back(item->data(0,Qt::UserRole).toString());
            viewport_->selectSketchEntities(ids);QMenu menu(structure_);viewport_->populateSketchMenu(menu);
            menu.exec(structure_->viewport()->mapToGlobal(pos));return;
        }
        if(item->parent()==constructionRoot_) {
            QStringList ids;
            if(item->isSelected()) for(auto* selected:structure_->selectedItems()) if(selected->parent()==constructionRoot_) ids.push_back(selected->data(0,Qt::UserRole).toString());
            if(ids.empty()) ids.push_back(item->data(0,Qt::UserRole).toString());
            viewport_->selectConstructions(ids);QMenu menu(structure_);
            auto* rename=menu.addAction(tr("Rename (F2)"));
            connect(rename,&QAction::triggered,this,[this,id=ids.front()] {
                for(int i=0;i<constructionRoot_->childCount();++i) if(constructionRoot_->child(i)->data(0,Qt::UserRole).toString()==id) {
                    structure_->setCurrentItem(constructionRoot_->child(i),0);structure_->editItem(constructionRoot_->child(i),0);break;
                }
            });
            viewport_->populateConstructionMenu(menu);menu.exec(structure_->viewport()->mapToGlobal(pos));return;
        }
        if (item->parent() != root) return;
        QList<QTreeWidgetItem*> targets;
        if (item->isSelected()) {
            for (auto* selected : structure_->selectedItems())
                if (selected->parent() == root) targets.append(selected);
        }
        if (targets.isEmpty()) targets.append(item);
        const size_t index = static_cast<size_t>(item->data(0, Qt::UserRole).toULongLong());
        QMenu menu(structure_);
        const auto id=viewport_->partId(index);
        auto* activate=menu.addAction(id==viewport_->activePartId()?tr("Deactivate Part"):tr("Set Active Part"));
        connect(activate,&QAction::triggered,viewport_,[this,id]{viewport_->setActivePart(id==viewport_->activePartId()?QString():id);});
        auto* clear=menu.addAction(tr("Clear Active Part"));clear->setEnabled(!viewport_->activePartId().isEmpty());
        connect(clear,&QAction::triggered,viewport_,[this]{viewport_->setActivePart({});});
        auto* select = menu.addAction(tr("Select"));
        auto* rename = menu.addAction(tr("Rename (F2)"));
        auto* duplicate = menu.addAction(tr("Duplicate Selected Parts"));
        auto* remove=menu.addAction(targets.size()>1?tr("Delete Selected Parts"):tr("Delete Part"));
        remove->setObjectName("structure.delete_part");remove->setEnabled(viewport_->canDeleteParts());
        auto* color = menu.addAction(tr("Color..."));
        menu.addSeparator();
        auto* hide = menu.addAction(targets.size() > 1 ? tr("Hide Selected Parts") : tr("Hide"));
        auto* show = menu.addAction(targets.size() > 1 ? tr("Show Selected Parts") : tr("Show"));
        menu.addSeparator();
        auto* fit = menu.addAction(tr("Fit Selected"));
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
        } else if (chosen == remove) {
            std::vector<size_t> indices;
            for(auto* target:targets)indices.push_back(target->data(0,Qt::UserRole).toULongLong());
            viewport_->deleteParts(indices);
        } else if (chosen == color) {
            const QColor selected = QColorDialog::getColor(viewport_->partColor(index),this,tr("Part Color"));
            if (selected.isValid())
                for (auto* target : targets)
                    viewport_->setPartColor(target->data(0,Qt::UserRole).toULongLong(),selected);
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
    statusBar()->showMessage(tr("Selection: Faces / Parts | Right-click selected geometry: commands | Drag orange arrow: Pull Face / Move Part (toggle mode) | Shift during drag: free movement | Middle: orbit | Shift+middle: pan | Wheel: zoom"));
    updateWindowTitle();
}

void MainWindow::updateWindowTitle() {
    const QString name = documentPath_.isEmpty()
        ? tr("Untitled") : QFileInfo(documentPath_).fileName();
    setWindowTitle(tr("OpenDesigner 0.5.3 — %1%2").arg(name, documentDirty_ ? QStringLiteral(" *") : QString()));
}

void MainWindow::markDocumentDirty() {
    if (documentTransition_ || documentDirty_) return;
    documentDirty_ = true;
    updateWindowTitle();
}

bool MainWindow::maybeSave() {
    if (!documentDirty_) return true;
    const auto answer = QMessageBox::warning(this, tr("Unsaved changes"),
        tr("The document has unsaved changes. Save them before continuing?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Cancel) return false;
    if (answer == QMessageBox::Save) return saveDocument();
    return true;
}

bool MainWindow::saveDocument() {
    if (documentPath_.isEmpty()) return saveDocumentAs();
    NativeDocumentSnapshot snapshot;
    QString error;
    if (!viewport_->exportDocumentSnapshot(snapshot) ||
        !NativeDocumentStorage::save(documentPath_, snapshot, error)) {
        if (error.isEmpty()) error=tr("Could not capture the current document for saving.");
        QMessageBox::warning(this, tr("Save failed"), error);
        return false;
    }
    documentDirty_ = false;
    updateWindowTitle();
    statusBar()->showMessage(tr("Document saved to %1").arg(documentPath_), 5000);
    return true;
}

bool MainWindow::saveDocumentAs() {
    QString suggested = documentPath_.isEmpty() ? QStringLiteral("Untitled.odc") : documentPath_;
    QFileDialog dialog(this,tr("Save OpenDesigner document"),suggested,tr("OpenDesigner document (*.odc)"));
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setDefaultSuffix(QStringLiteral("odc"));
    if (dialog.exec()!=QDialog::Accepted || dialog.selectedFiles().isEmpty()) return false;
    QString path=dialog.selectedFiles().front();
    if (path.isEmpty()) return false;
    if (!path.endsWith(QStringLiteral(".odc"), Qt::CaseInsensitive)) path += QStringLiteral(".odc");
    const QString oldPath = documentPath_;
    documentPath_ = path;
    if (!saveDocument()) { documentPath_ = oldPath; updateWindowTitle(); return false; }
    return true;
}

void MainWindow::newDocument() {
    if (!maybeSave()) return;
    documentTransition_ = true;
    viewport_->resetDocument();
    documentPath_.clear();
    documentDirty_ = false;
    documentTransition_ = false;
    updateWindowTitle();
    statusBar()->showMessage(tr("New document"), 3000);
}

void MainWindow::openDocument() {
    if (!maybeSave()) return;
    const QString path = QFileDialog::getOpenFileName(this, tr("Open OpenDesigner document"),
                                                      QString(), tr("OpenDesigner document (*.odc)"));
    if (path.isEmpty()) return;
    NativeDocumentSnapshot snapshot;
    QString error;
    if (!NativeDocumentStorage::load(path, snapshot, error)) {
        QMessageBox::warning(this, tr("Open failed"), error);
        return;
    }
    documentTransition_ = true;
    if (!viewport_->importDocumentSnapshot(snapshot, error)) {
        documentTransition_ = false;
        QMessageBox::warning(this, tr("Open failed"), error);
        return;
    }
    documentTransition_ = false;
    documentPath_ = path;
    documentDirty_ = false;
    updateWindowTitle();
    statusBar()->showMessage(tr("Document opened from %1").arg(path), 5000);
}

void MainWindow::rebuildStructureBrowser() {
    if (!root_) return;
    const QSignalBlocker blocker(structure_);
    const auto oldChildren = root_->takeChildren();
    qDeleteAll(oldChildren);
    for (size_t index = 0; index < viewport_->partSlotCount(); ++index) {
        auto* item = new QTreeWidgetItem(root_, QStringList{viewport_->partName(index)});
        item->setData(0, Qt::UserRole, static_cast<qulonglong>(index));
        item->setFlags(item->flags() | Qt::ItemIsEditable);
        if(viewport_->partId(index)==viewport_->activePartId() && !viewport_->activePartId().isEmpty()) {
            QPixmap green(14,14);green.fill(QColor("#90D89A"));item->setIcon(0,QIcon(green));item->setToolTip(0,tr("Active Part — current modeling target"));
        }
        const bool active = viewport_->isPartActive(index);
        item->setHidden(!active);
        const bool visible = viewport_->isPartVisible(index);
        item->setText(1, visible ? QStringLiteral("👁") : QStringLiteral("—"));
        item->setForeground(0, visible ? QBrush() : QBrush(QColor(Qt::gray)));
    }
    root_->setExpanded(true);
}

void MainWindow::rebuildConstructionBrowser() {
    if(!constructionRoot_) return;
    const QSignalBlocker blocker(structure_);
    const auto children=constructionRoot_->takeChildren();qDeleteAll(children);
    for(const auto& object:viewport_->constructionObjects()) {
        auto* item=new QTreeWidgetItem(constructionRoot_,QStringList{object.name});
        item->setData(0,Qt::UserRole,object.id);item->setFlags(item->flags()|Qt::ItemIsEditable);
        item->setToolTip(0,Construction::kindName(object.kind)+(object.selectable?QString():tr(" — selection locked")));
        item->setText(1,object.visible?QStringLiteral("👁"):QStringLiteral("—"));
        item->setForeground(0,object.visible?QBrush():QBrush(QColor(Qt::gray)));
        item->setSelected(viewport_->constructionSelection().contains(object.id));
        if(viewport_->activeSketchWorkplane()==object.id) {
            auto font=item->font(0);font.setBold(true);item->setFont(0,font);item->setToolTip(0,tr("Active 2D Workplane"));
        }
        for(const auto& entity:viewport_->sketchEntities()) if(entity.workplaneId==object.id) {
            const auto role=entity.role==Sketch::Role::Construction?tr("Construction"):tr("Profile");
            auto* child=new QTreeWidgetItem(item,QStringList{entity.name.isEmpty()?Sketch::typeName(entity.geometry):entity.name});
            child->setData(0,Qt::UserRole,entity.id);child->setData(0,Qt::UserRole+1,QStringLiteral("sketch"));
            child->setFlags(child->flags()|Qt::ItemIsEditable);
            child->setToolTip(0,role+(entity.selectable?QString():tr(" — selection locked")));
            child->setText(1,entity.visible?QStringLiteral("👁"):QStringLiteral("—"));
            child->setForeground(0,entity.visible && object.visible?QBrush():QBrush(QColor(Qt::gray)));
            child->setSelected(viewport_->sketchSelection().contains(entity.id));
        }
        item->setExpanded(true);
    }
    constructionRoot_->setExpanded(true);rebuildSketchControls();
}
void MainWindow::rebuildSketchControls() {
    if(!sketchWorkplane_) return;
    const QSignalBlocker blocker(sketchWorkplane_);sketchWorkplane_->clear();sketchWorkplane_->addItem(tr("Choose Workplane…"),QString());
    for(const auto& object:viewport_->constructionObjects()) if(object.kind==Construction::Kind::Workplane && object.visible && object.selectable)
        sketchWorkplane_->addItem(object.name,object.id);
    const int index=sketchWorkplane_->findData(viewport_->activeSketchWorkplane());sketchWorkplane_->setCurrentIndex(std::max(0,index));
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (maybeSave()) {
        documentTransition_=true;
        viewport_->endSketchEditing();
        viewport_->setActivePart({});
        documentTransition_=false;
        event->accept();
    } else event->ignore();
}

bool MainWindow::eventFilter(QObject* watched,QEvent* event) {
    if(event->type()==QEvent::KeyPress) {
        const auto* key=static_cast<QKeyEvent*>(event);
        if(key->key()==Qt::Key_Delete && key->modifiers()==Qt::NoModifier
            && QApplication::focusWidget()==watched && viewport_->canDeleteParts()) {
            if(watched==viewport_) {
                if(viewport_->deleteSelectedParts()){event->accept();return true;}
            } else if(watched==structure_ || watched==structure_->viewport()) {
                std::vector<size_t> parts;
                for(auto* item:structure_->selectedItems()) {
                    if(item->parent()!=root_)return QMainWindow::eventFilter(watched,event);
                    parts.push_back(item->data(0,Qt::UserRole).toULongLong());
                }
                if(viewport_->deleteParts(parts)){event->accept();return true;}
            }
        }
    }
    return QMainWindow::eventFilter(watched,event);
}
