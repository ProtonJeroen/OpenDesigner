#include "CommandPanelPositioner.h"
#include <QApplication>
#include <QDebug>
#include <QDialog>
#include <QDockWidget>
#include <QMoveEvent>
#include <QResizeEvent>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMouseEvent>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>
#include <QWindow>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool ok,const char* message) {if(!ok) throw std::runtime_error(message);}
void events(int ms=40) {QEventLoop loop;QTimer::singleShot(ms,&loop,&QEventLoop::quit);loop.exec();}
// Independent oracle: Qt's native viewport mapping, or the top-level client
// origin plus layout offset for an alien widget. Screen origins are nonzero.
QRect bounds(QWidget& viewport) {
    if(auto* native=viewport.windowHandle()) return {native->mapToGlobal(QPoint()),native->size()};
    auto* top=viewport.window();
    return {top->windowHandle()->geometry().topLeft()+viewport.mapTo(top,QPoint()),viewport.size()};
}
QRect frame(QWidget& panel) {return panel.windowHandle()->frameGeometry();}
void placed(QWidget& viewport,QWidget& panel,bool anchor,QPoint local={}) {
    QElapsedTimer timer;timer.start();
    for(;;) {
        const auto b=bounds(viewport),f=frame(panel);
        const auto expected=anchor?QPoint(b.x()+b.width()-f.width()-12,b.y()+12):b.topLeft()+local;
        if(b.contains(f) && std::abs(f.x()-expected.x())<=1 && std::abs(f.y()-expected.y())<=1) return;
        if(timer.elapsed()>1500) {
            QString diagnostic;QDebug(&diagnostic)<<"bounds"<<b<<"native frame"<<f<<"widget frame"<<panel.frameGeometry()<<"expected"<<expected<<"panel screen"<<panel.windowHandle()->screen()->name();
            std::cerr<<diagnostic.toStdString()<<'\n';throw std::runtime_error("cross-screen placement did not converge");
        }
        events(10);
    }
}
// Hold QWidgetWindow's configure delivery while the platform geometry changes.
// This models the native/QWidget lag without replacing any positioning code.
class DelayedConfigure final : public QObject {
public:
    explicit DelayedConfigure(QWindow* window):window_(window) {window_->installEventFilter(this);}
    ~DelayedConfigure() override {release();}
    void release() {
        if(!window_) return;
        window_->removeEventFilter(this);
        QMoveEvent move(window_->position(),window_->position());
        QResizeEvent resize(window_->size(),window_->size());
        QApplication::sendEvent(window_,&move);QApplication::sendEvent(window_,&resize);
        window_=nullptr;
    }
protected:
    bool eventFilter(QObject*,QEvent* event) override {return event->type()==QEvent::Move || event->type()==QEvent::Resize;}
private:
    QWindow* window_;
};
void drag(QWidget& panel,QPoint target) {
    auto* handle=panel.findChild<QLabel*>("commandPanelDragHandle");check(handle,"missing synthetic drag handle");
    const auto start=frame(panel).topLeft()+QPoint(10,10),end=target+QPoint(10,10);
    const auto send=[&](QEvent::Type type,QPoint at,Qt::MouseButton button,Qt::MouseButtons buttons) {
        QMouseEvent event(type,QPointF(10,10),QPointF(at),button,buttons,Qt::NoModifier);
        QApplication::sendEvent(handle,&event);
    };
    send(QEvent::MouseButtonPress,start,Qt::LeftButton,Qt::LeftButton);
    send(QEvent::MouseMove,end,Qt::NoButton,Qt::LeftButton);
    send(QEvent::MouseButtonRelease,end,Qt::LeftButton,Qt::NoButton);
}
void run(bool nativeViewport) {
    QMainWindow main;auto* viewport=new QWidget;viewport->setAttribute(Qt::WA_NativeWindow,nativeViewport);
    main.setCentralWidget(viewport);
    auto* dock=new QDockWidget("Structure",&main);dock->setWidget(new QLabel("Browser"));main.addDockWidget(Qt::LeftDockWidgetArea,dock);dock->setMinimumWidth(100);
    main.resize(900,650);
    CommandPanelPositioner positioner(viewport);
    main.show();events();
    QDialog panel(viewport,Qt::Tool);panel.setWindowTitle("Cross-screen command");
    auto* layout=new QVBoxLayout(&panel);layout->addWidget(new QLineEdit("12.5"));
    auto* tall=new QLabel("Parameters");tall->setMinimumSize(320,250);layout->addWidget(tall);
    positioner.show(&panel);placed(*viewport,panel,true);
    const QPoint explicitLocal(30,40);
    panel.windowHandle()->setFramePosition(bounds(*viewport).topLeft()+explicitLocal);
    events();placed(*viewport,panel,false,explicitLocal);
    panel.hide();positioner.show(&panel);placed(*viewport,panel,true);
    const auto screens=QGuiApplication::screens();
    // All 12 directed transitions, with explicit screen-first and automatic
    // geometry-first delivery (no physical display or KWin simulation claim).
    for(int order=0;order<2;++order) for(int from=0;from<4;++from) for(int to=0;to<4;++to) if(from!=to) {
        for(auto index:{from,to}) {
            auto* screen=screens[index];
            auto* window=main.windowHandle();
            const auto origin=screen->geometry().topLeft()+QPoint(60,70);
            if(order==0) {window->setScreen(screen);window->setFramePosition(origin);}
            else window->setGeometry(QRect(origin,QSize(900,650)));
            main.resize(900,650);events();
            placed(*viewport,panel,true);
            check(std::abs(panel.windowHandle()->devicePixelRatio()-screen->devicePixelRatio())<0.01,"panel retained the previous screen's DPR");
            // A later native size/configure must be fitted using current bounds.
            QTimer::singleShot(80,&main,[&main]{main.resize(450,240);});
            events(150);placed(*viewport,panel,true);
            check(panel.findChild<QScrollArea*>()->verticalScrollBar()->maximum()>0,"constrained cross-screen form cannot scroll");
            main.resize(900,650);events();placed(*viewport,panel,true);
            auto* scroll=panel.findChild<QScrollArea*>();
            check(!scroll->horizontalScrollBar()->maximum() && !scroll->verticalScrollBar()->maximum(),"restored cross-screen form retained scrollbars");
        }
    }
    // Screen notification before later platform geometry and QWidget configure.
    // The delayed platform correction must not be recorded as a user's drag.
    auto* native=panel.windowHandle();
    const auto oldScreen=main.windowHandle()->screen();
    auto* destination=screens[(screens.indexOf(oldScreen)+1)%4];
    main.windowHandle()->setScreen(destination);
    QTimer::singleShot(70,&main,[&] {
        main.windowHandle()->setFramePosition(destination->geometry().topLeft()+QPoint(90,100));
        main.resize(800,600);
    });
    events(110);placed(*viewport,panel,true);
    {
        DelayedConfigure delayed(native);
        const auto requestedSize=panel.size();
        native->setFramePosition(frame(panel).topLeft()+QPoint(-75,55));
        native->resize(QSize(120,80));
        check(panel.size()!=native->size() && panel.frameGeometry().topLeft()!=frame(panel).topLeft(),"test did not create native/QWidget geometry lag");
        // Let the positioner's screen-transition retries observe the native
        // change while QWidget's geometry cache still reports the old frame.
        events(120);placed(*viewport,panel,true);
        check(native->size()==requestedSize,"native resize was not repaired while QWidget size was stale");
        delayed.release();
    }
    events(450);placed(*viewport,panel,true);
    if(nativeViewport) {
        main.move(main.pos()+QPoint(10,15));events();
        auto* child=viewport->windowHandle();
        DelayedConfigure delayed(child);
        child->setGeometry(QRect(child->position()+QPoint(17,23),QSize(370,220)));
        events(120);placed(*viewport,panel,true);
        delayed.release();
    }
    // Geometry events after the bounded retry window still trigger fresh fits.
    main.resize(430,260);events();placed(*viewport,panel,true);
    main.resize(800,600);events();placed(*viewport,panel,true);
    // A real title-bar drag must take over even during configure settling.
    const QPoint decoratedLocal(20,30);
    const auto point=frame(panel).topLeft();
    QMouseEvent press(QEvent::NonClientAreaMouseButtonPress,QPointF(),QPointF(point),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(native,&press);
    native->setFramePosition(bounds(*viewport).topLeft()+decoratedLocal);events();
    QMouseEvent release(QEvent::NonClientAreaMouseButtonRelease,QPointF(),QPointF(point),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
    QApplication::sendEvent(native,&release);
    placed(*viewport,panel,false,decoratedLocal);
    QWidget manual(viewport,Qt::Tool|Qt::FramelessWindowHint);
    auto* manualLayout=new QVBoxLayout(&manual);manualLayout->addWidget(new QLineEdit("Manual command"));
    positioner.manage(&manual,true);positioner.show(&manual);placed(*viewport,manual,true);
    const QPoint local(25,35);drag(manual,bounds(*viewport).topLeft()+local);events();placed(*viewport,manual,false,local);
    for(auto* screen:screens) {
        main.windowHandle()->setScreen(screen);
        main.windowHandle()->setFramePosition(screen->geometry().topLeft()+QPoint(80,90));
        main.resize(800,600);events();placed(*viewport,manual,false,local);
        main.move(main.pos()+QPoint(30,20));events();placed(*viewport,manual,false,local);
        dock->hide();events();placed(*viewport,manual,false,local);dock->show();events();placed(*viewport,manual,false,local);
        main.resize(280,190);events();
        check(bounds(*viewport).contains(frame(manual)),"manual panel escaped resized viewport");
        main.resize(800,600);events();
        check(bounds(*viewport).contains(frame(manual)),"manual panel escaped restored viewport");
        // Restore an explicit offset after clamping, as in a fresh user drag.
        drag(manual,bounds(*viewport).topLeft()+local);events();placed(*viewport,manual,false,local);
    }
    manual.hide();positioner.show(&manual);placed(*viewport,manual,true);
    // Queued callbacks must also tolerate closing/destroying a command/window.
    auto* transient=new QDialog(viewport,Qt::Tool);transient->setLayout(new QVBoxLayout);
    positioner.show(transient);delete transient;events(450);
    manual.hide();panel.hide();main.hide();
}
}
int main(int argc,char** argv) {
    QApplication app(argc,argv);
    try {
        check(app.platformName()=="offscreen" && app.screens().size()==4,"synthetic screen configuration was not loaded");
        const double dprs[]={1,1.25,1.5,2};
        for(int i=0;i<4;++i) check(std::abs(app.screens()[i]->devicePixelRatio()-dprs[i])<0.01,"synthetic DPR mismatch");
        run(false);run(true);
        std::cout<<"Synthetic Qt screens at 100/125/150/200 percent: production panel transitions and delayed resizing passed.\n";
        return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
