#include "CommandPanelPositioner.h"
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QDockWidget>
#include <QMenu>
#include <QMouseEvent>
#include <QPointer>
#include <QEventLoop>
#include <QTimer>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QVBoxLayout>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool ok,const char* message) {if(!ok) throw std::runtime_error(message);}
void settle() {for(int i=0;i<3;++i) {QApplication::processEvents();QGuiApplication::sync();QEventLoop loop;QTimer::singleShot(10,&loop,&QEventLoop::quit);loop.exec();}}
QRect bounds(QWidget& viewport) {return {viewport.mapToGlobal(QPoint()),viewport.size()};}
void inside(QWidget& viewport,QWidget& panel) {check(bounds(viewport).contains(panel.frameGeometry()),"panel frame escaped viewport");}
void anchored(QWidget& viewport,QWidget& panel) {
    inside(viewport,panel);const auto b=bounds(viewport),p=panel.frameGeometry();
    check(std::abs((b.right()-p.right())-12)<=1 && std::abs((p.top()-b.top())-12)<=1,"initial position is not viewport upper-right +12 logical pixels");
}
void moveFrame(QWidget& panel,QPoint p) {panel.move(panel.pos()+p-panel.frameGeometry().topLeft());settle();}
void noScroll(QWidget& panel) {
    auto* scroll=panel.findChild<QScrollArea*>();check(scroll,"missing scroll wrapper");
    check(!scroll->horizontalScrollBar()->maximum() && !scroll->verticalScrollBar()->maximum(),"natural content fits but command form scrolls");
    for(auto* control:scroll->widget()->findChildren<QWidget*>()) if(control->isVisibleTo(&panel))
        check(scroll->viewport()->rect().contains(QRect(control->mapTo(scroll->viewport(),QPoint()),control->size())),"command content clipped despite available viewport space");
}
void mouse(QWidget& widget,QEvent::Type type,QPoint global,Qt::MouseButton button,Qt::MouseButtons buttons) {
    QMouseEvent event(type,QPointF(widget.mapFromGlobal(global)),QPointF(global),button,buttons,Qt::NoModifier);QApplication::sendEvent(&widget,&event);
}
void run() {
    QMainWindow main;auto* viewport=new QWidget;main.setCentralWidget(viewport);main.resize(1100,800);main.move(60,50);
    auto* dock=new QDockWidget("Structure",&main);dock->setWidget(new QLabel("Browser"));main.addDockWidget(Qt::LeftDockWidgetArea,dock);dock->setMinimumWidth(180);
    main.show();settle();CommandPanelPositioner helper(viewport);
    if(qEnvironmentVariableIntValue("QT_SCALE_FACTOR")==2) check(viewport->devicePixelRatioF()>=1.9,"HiDPI test did not activate device scaling");
    QWidget panel(viewport,Qt::Tool|Qt::FramelessWindowHint);panel.setWindowTitle("Modeling Preview");
    auto* layout=new QVBoxLayout(&panel);auto* edit=new QLineEdit;edit->setText("12.5");layout->addWidget(edit);
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);layout->addWidget(buttons);
    helper.manage(&panel,true);helper.show(&panel);settle();anchored(*viewport,panel);noScroll(panel);
    check(panel.findChild<QScrollArea*>() && panel.isAncestorOf(edit),"wrapper lost original controls");
    panel.activateWindow();edit->setFocus();
    for(int i=0;i<50 && QApplication::focusWidget()!=edit;++i) {QEventLoop loop;QTimer::singleShot(10,&loop,&QEventLoop::quit);loop.exec();}
    auto* focus=QApplication::focusWidget();check(focus==edit,"numeric input did not acquire focus");
    auto* handle=panel.findChild<QLabel*>("commandPanelDragHandle");check(handle,"frameless panel has no drag handle");
    const auto start=handle->mapToGlobal(handle->rect().center());const auto expected=panel.frameGeometry().topLeft()+QPoint(-200,80);
    mouse(*handle,QEvent::MouseButtonPress,start,Qt::LeftButton,Qt::LeftButton);
    mouse(*handle,QEvent::MouseMove,start+QPoint(-200,80),Qt::NoButton,Qt::LeftButton);
    mouse(*handle,QEvent::MouseButtonRelease,start+QPoint(-200,80),Qt::LeftButton,Qt::NoButton);settle();
    check(panel.frameGeometry().topLeft()==expected,"drag handle did not move frame by pointer delta");
    for(int i=0;i<5;++i) {edit->setText(QString::number(100+i));helper.show(&panel);settle();}
    check(panel.frameGeometry().topLeft()==expected && QApplication::focusWidget()==focus,"preview/show reset dragged panel or stole focus");
    // A user position near the border is valid; the default margin must not
    // pull an already visible, manually placed panel away from the border.
    const auto border=bounds(*viewport).topLeft()+QPoint(2,3);moveFrame(panel,border);helper.update();settle();
    check(panel.frameGeometry().topLeft()==border,"visible manual position was unnecessarily moved");
    auto* dynamic=new QLabel("Expanded parameters");dynamic->setMinimumHeight(160);layout->addWidget(dynamic);settle();inside(*viewport,panel);noScroll(panel);
    check(panel.frameGeometry().topLeft()==border,"parameter growth reset manual position");
    const auto grown=panel.size();dynamic->hide();settle();noScroll(panel);
    check(panel.height()<grown.height(),"hidden parameters retained an obsolete natural height");
    dynamic->show();settle();noScroll(panel);check(panel.size()==grown,"repeated content changes altered natural size");
    const auto originalFont=panel.font();auto largeFont=originalFont;largeFont.setPointSizeF(18);panel.setFont(largeFont);settle();noScroll(panel);
    check(panel.width()>grown.width(),"font change did not recompute natural input width");
    panel.setFont(originalFont);settle();noScroll(panel);check(panel.size()==grown,"font restoration retained stale form dimensions");
    const auto relative=panel.frameGeometry().topLeft()-bounds(*viewport).topLeft();main.move(main.pos()+QPoint(30,20));settle();
    check(panel.frameGeometry().topLeft()-bounds(*viewport).topLeft()==relative,"parent move changed viewport-relative user position");
    dock->hide();settle();inside(*viewport,panel);dock->show();settle();inside(*viewport,panel);
    moveFrame(panel,bounds(*viewport).bottomRight()-QPoint(100,100));inside(*viewport,panel);
    main.resize(720,500);settle();inside(*viewport,panel);
    panel.hide();helper.show(&panel);settle();anchored(*viewport,panel);
    // Overflow both axes at 210x150, but fit a 1920x1080 screen at DPR 2
    // when restored. A window manager may cap a top-level 1000x1000 request.
    QWidget small;small.resize(210,150);small.move(20,20);small.show();settle();CommandPanelPositioner tiny(&small);
    QDialog large(&small,Qt::Tool);auto* largeLayout=new QVBoxLayout(&large);
    auto* wide=new QLineEdit;wide->setMinimumWidth(350);largeLayout->addWidget(wide);
    auto* tall=new QLabel("Long form");tall->setMinimumHeight(350);largeLayout->addWidget(tall);
    auto* confirm=new QPushButton("OK");largeLayout->addWidget(confirm);tiny.show(&large);settle();anchored(small,large);
    // Native decorations and the WM's initial configure arrive asynchronously.
    // Further event processing must preserve the frame's exact default anchor.
    for(int i=0;i<3;++i) {settle();anchored(small,large);}
    auto* scroll=large.findChild<QScrollArea*>();check(scroll && scroll->horizontalScrollBar()->maximum()>0 && scroll->verticalScrollBar()->maximum()>0,"oversized content has no reachable scroll ranges");
    scroll->ensureWidgetVisible(confirm);settle();check(scroll->verticalScrollBar()->value()>0,"bottom controls cannot be reached");
    // Constrained sizes must never become the next natural-size measurement.
    small.resize(1000,1000);settle();noScroll(large);const auto natural=large.size();
    for(int i=0;i<3;++i) {small.resize(210,150);settle();inside(small,large);small.resize(1000,1000);settle();noScroll(large);check(large.size()==natural,"viewport shrink/restore ratcheted natural size down");}
    // One-dimensional overflow must not introduce an unnecessary second bar.
    const auto decorations=large.frameGeometry().size()-large.size();
    small.resize(natural.width()+decorations.width()+24+40,200);settle();inside(small,large);
    check(scroll->verticalScrollBar()->maximum()>0 && !scroll->horizontalScrollBar()->maximum(),"vertical overflow unnecessarily introduced horizontal scrolling");
    const auto manual=bounds(small).topLeft()+QPoint(3,4);moveFrame(large,manual);
    for(int i=0;i<3;++i) {tiny.show(&large);settle();check(large.frameGeometry().topLeft()==manual,"native decorated panel reset manual position");}
    large.hide();tiny.show(&large);settle();anchored(small,large);
    // Only explicitly registered nonmodal Tool windows are affected.
    QDialog modal(viewport);modal.setModal(true);modal.resize(150,100);modal.move(100,100);const auto modalPos=modal.pos();
    helper.manage(&modal);helper.update();check(!modal.findChild<QScrollArea*>() && modal.pos()==modalPos,"modal dialog was wrapped/repositioned");
    QMenu menu;menu.addAction("Fit All");helper.manage(&menu);check(!menu.findChild<QScrollArea*>(),"context menu was registered");
    auto* transient=new QDialog(viewport,Qt::Tool);transient->setLayout(new QVBoxLayout);helper.show(transient);settle();delete transient;helper.update();settle();
    panel.hide();large.hide();main.hide();small.hide();
}
}
int main(int argc,char** argv) {
    QApplication app(argc,argv);
    try {run();std::cout<<"Viewport-relative placement, drag, resize, scrolling, exclusions and HiDPI passed.\n";return 0;}
    catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
