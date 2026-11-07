#include "CommandPanelPositioner.h"
#include <QDialog>
#include <QDebug>
#include <cstdio>
#include <QEvent>
#include <QLabel>
#include <QLayout>
#include <QMouseEvent>
#include <QMoveEvent>
#include <QScreen>
#include <QPointer>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <QWindow>
#include <QGuiApplication>
#include <algorithm>

namespace {
QRect panelFrame(QWidget* widget) {
    return widget->windowHandle()?widget->windowHandle()->frameGeometry():widget->frameGeometry();
}
QPoint clientOrigin(QWidget* widget) {
    return widget->windowHandle()?widget->windowHandle()->geometry().topLeft():widget->geometry().topLeft();
}
QSize naturalSize(QWidget* content) {
    content->ensurePolished();
    if(content->layout()) content->layout()->activate();
    auto size=content->sizeHint().expandedTo(content->minimumSizeHint()).expandedTo(content->minimumSize());
    if(content->layout() && content->layout()->hasHeightForWidth())
        size.setHeight(std::max(size.height(),content->layout()->totalHeightForWidth(size.width())));
    return size.expandedTo(QSize(1,1));
}
class PanelScrollArea final : public QScrollArea {
public:
    using QScrollArea::QScrollArea;
    QSize sizeHint() const override {return widget()?widget()->sizeHint():QSize(0,0);}
    QSize minimumSizeHint() const override {return QSize(0,0);}
};
}
struct CommandPanelPositioner::Panel {
    QPointer<QWidget> widget,content;
    QPointer<QLabel> handle;
    QPointer<QScrollArea> scroll;
    QSize natural;
    QMargins frameMargins;
    QPoint localPosition,lastFramePosition,lastViewportOrigin,dragOffset;
    std::vector<QPoint> nativePositions;
    bool anchor=true,placing=false,dragging=false,systemDragging=false,opening=true,settling=false;
    void expectNativePosition(const QPoint& position) {
        // Native configure notifications can arrive after a later resize/move
        // has already updated QWidget's geometry. Keep their client origins,
        // independent of asynchronously changing frame margins.
        if(nativePositions.size()==64) nativePositions.erase(nativePositions.begin());
        nativePositions.push_back(position);
    }
};
CommandPanelPositioner::CommandPanelPositioner(QWidget* viewport):QObject(viewport),viewport_(viewport) {
    for(auto* ancestor=viewport_;ancestor;ancestor=ancestor->parentWidget()) ancestor->installEventFilter(this);
    reconcileTimer_.setInterval(50);
    connect(&reconcileTimer_,&QTimer::timeout,this,[this] {
        update();
        if(--reconcilePasses_<=0) {reconcileTimer_.stop();screenTransition_=false;}
    });
    watchWindows();
}
CommandPanelPositioner::~CommandPanelPositioner() = default;
QRect CommandPanelPositioner::viewportBounds() const {
    // Keep the complete rectangle in the owning window's logical coordinate
    // system. mapToGlobal() can select a different screen for an individual
    // point in a mixed-DPI desktop gap, while size() remains window-local.
    auto* ancestor=viewport_;
    while(!ancestor->windowHandle() && ancestor->parentWidget()) ancestor=ancestor->parentWidget();
    if(auto* native=ancestor->windowHandle()) {
        auto origin=viewport_->mapTo(ancestor,QPoint());
        for(auto* window=native;window;window=window->parent()) origin+=window->position();
        // CadViewport is a native child. Its platform size/offset can precede
        // the corresponding QWidget configure just like a Tool window's can.
        return {origin,ancestor==viewport_?native->size():viewport_->size()};
    }
    return {ancestor->geometry().topLeft()+viewport_->mapTo(ancestor,QPoint()),viewport_->size()};
}
void CommandPanelPositioner::reconcile(bool screenTransition) {
    // Screen/DPR, frame margins and KWin configure events need not arrive in
    // one event-loop turn. Re-read geometry on every pass; never replay an old
    // target. Late Move/Resize events also schedule a fresh fit after this
    // bounded retry period, so there is no permanent polling timer.
    reconcilePasses_=8;
    screenTransition_=screenTransition_ || screenTransition;
    reconcileTimer_.start();
    schedule();
}
void CommandPanelPositioner::watchWindows() {
    std::erase_if(windows_,[](const auto& item){return !item;});
    std::erase_if(screens_,[](const auto& item){return !item;});
    const auto watch=[this](QWindow* window) {
        if(!window || std::find(windows_.begin(),windows_.end(),window)!=windows_.end()) return;
        windows_.push_back(window);window->installEventFilter(this);
        connect(window,&QWindow::screenChanged,this,[this] {watchWindows();reconcile(true);});
    };
    for(auto* ancestor=viewport_;ancestor;ancestor=ancestor->parentWidget()) watch(ancestor->windowHandle());
    for(const auto& panel:panels_) if(panel->widget) watch(panel->widget->windowHandle());
    for(auto* screen:QGuiApplication::screens()) {
        if(std::find(screens_.begin(),screens_.end(),screen)!=screens_.end()) continue;
        screens_.push_back(screen);
        connect(screen,&QScreen::geometryChanged,this,[this] {reconcile(true);});
        connect(screen,&QScreen::logicalDotsPerInchChanged,this,[this] {reconcile(true);});
    }
}
QRect CommandPanelPositioner::boundedFrame(const QRect& viewport,const QSize& size,const QPoint& requested,bool anchor) {
    const int mx=anchor?std::min(Margin,std::max(0,(viewport.width()-size.width())/2)):0;
    const int my=anchor?std::min(Margin,std::max(0,(viewport.height()-size.height())/2)):0;
    const int left=viewport.left()+mx,top=viewport.top()+my;
    const int right=std::max(left,viewport.x()+viewport.width()-mx-size.width());
    const int bottom=std::max(top,viewport.y()+viewport.height()-my-size.height());
    return QRect(QPoint(anchor?right:std::clamp(requested.x(),left,right),
                        anchor?top:std::clamp(requested.y(),top,bottom)),size);
}
void CommandPanelPositioner::manage(QWidget* widget,bool addDragHandle) {
    if(!widget || widget->windowType()!=Qt::Tool || widget->isModal()) return;
    for(const auto& p:panels_) if(p->widget==widget) return;
    auto p=std::make_unique<Panel>();p->widget=widget;
    auto* content=new QWidget(widget);content->setObjectName("commandPanelContent");
    // QWidget::setLayout transfers the existing layout and reparents its items.
    // All field pointers, callbacks and the original QDialog remain intact.
    if(widget->layout()) content->setLayout(widget->layout());
    p->content=content;
    auto* outer=new QVBoxLayout(widget);outer->setContentsMargins(0,0,0,0);outer->setSpacing(0);
    outer->setSizeConstraint(QLayout::SetNoConstraint);
    if(addDragHandle) {
        p->handle=new QLabel(widget->windowTitle(),widget);p->handle->setObjectName("commandPanelDragHandle");
        p->handle->setMargin(4);p->handle->setCursor(Qt::SizeAllCursor);
        p->handle->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed);
        p->handle->setToolTip(tr("Drag to move this command panel"));outer->addWidget(p->handle);
    }
    auto* scroll=new PanelScrollArea(widget);scroll->setObjectName("commandPanelScrollArea");
    // The helper sizes the content from its natural layout, independently of
    // the constrained window. widgetResizable would feed the scroll viewport's
    // size back into the content's word wrapping and minimum-size calculation.
    scroll->setFrameShape(QFrame::NoFrame);scroll->setWidgetResizable(false);scroll->setMinimumSize(0,0);
    scroll->setWidget(content);outer->addWidget(scroll);
    p->scroll=scroll;
    widget->setMinimumSize(0,0);
    widget->installEventFilter(this);content->installEventFilter(this);
    if(p->handle) p->handle->installEventFilter(this);
    panels_.push_back(std::move(p));
    connect(widget,&QObject::destroyed,this,[this] {std::erase_if(panels_,[](const auto& item){return !item->widget;});});
}
void CommandPanelPositioner::show(QWidget* widget) {
    manage(widget);
    if(!widget) return;
    if(!widget->isVisible()) for(const auto& p:panels_) if(p->widget==widget) {
        p->anchor=true;p->opening=true;
    }
    // Mapping a native tool window is asynchronous. Wait for its first expose
    // before placing it, so the WM's initial placement cannot overwrite ours.
    widget->show();
    watchWindows();
    update();schedule();
}
void CommandPanelPositioner::schedule() {
    if(pending_) return;
    pending_=true;QTimer::singleShot(0,this,[this]{pending_=false;update();});
}
void CommandPanelPositioner::moveFrame(Panel& p,const QPoint& position) {
    p.lastFramePosition=position;p.lastViewportOrigin=viewportBounds().topLeft();
    p.localPosition=position-p.lastViewportOrigin;
    auto* w=p.widget.data();const auto delta=position-panelFrame(w).topLeft();
    if(!delta.isNull()) {
        p.expectNativePosition(clientOrigin(w));
        // An absolute native frame request avoids adding a delta from a stale
        // QWidget geometry cache to a position converted on a different screen.
        if(auto* window=w->windowHandle()) window->setFramePosition(position);
        else w->move(position);
        p.expectNativePosition(clientOrigin(w));
    }
    if(qEnvironmentVariableIsSet("OPENDESIGNER_PANEL_TRACE")) {
        QString trace;QDebug(&trace)<<"after move"<<w->frameGeometry()<<w->pos()<<"requested"<<position;
        fprintf(stderr,"%s\n",qPrintable(trace));
    }
}
void CommandPanelPositioner::fit(Panel& p) {
    if(!p.widget || !p.widget->isVisible() || !p.content || p.placing) return;
    if(p.opening) return;
    p.placing=true;auto* w=p.widget.data();
    // Select the target screen before measuring decorations or issuing a native
    // frame move: setFramePosition uses the window's current screen conversion.
    auto* owner=viewport_->window()->windowHandle();
    auto* native=w->windowHandle();
    if(native && owner && owner->screen() && native->screen()!=owner->screen()) {
        native->setScreen(owner->screen());
        reconcile(true);
    }
    if(p.handle) p.handle->setText(w->windowTitle());
    // Do not measure w->sizeHint(): the outer layout caches the scroll area's
    // old hint when fields are shown/inserted or wrapped prompts grow. In
    // particular the generic panel is registered with its controls hidden.
    auto contentSize=naturalSize(p.content);
    const auto handleSize=p.handle?naturalSize(p.handle):QSize(0,0);
    contentSize.setWidth(std::max(contentSize.width(),handleSize.width()));
    if(p.content->size()!=contentSize) p.content->resize(contentSize);
    const auto natural=contentSize+QSize(0,handleSize.height());
    if(p.natural!=natural) {
        p.natural=natural;p.scroll->updateGeometry();
        w->layout()->invalidate();
    }
    const auto bounds=viewportBounds();
    p.frameMargins=native?native->frameMargins():QMargins();
    const auto decorations=panelFrame(w).size()-(native?native->size():w->size());
    const auto available=(bounds.size()-decorations-QSize(2*Margin,2*Margin)).expandedTo(QSize(1,1));
    w->setMaximumSize(available);
    // Reserve room for a necessary scrollbar before deciding whether the other
    // dimension overflows. Otherwise a vertical bar can needlessly cause a
    // horizontal bar even when the natural form's width fits the viewport.
    bool horizontal=false,vertical=false;
    const int barWidth=p.scroll->verticalScrollBar()->sizeHint().width();
    const int barHeight=p.scroll->horizontalScrollBar()->sizeHint().height();
    for(int i=0;i<3;++i) {
        horizontal=contentSize.width()>available.width()-(vertical?barWidth:0);
        vertical=natural.height()>available.height()-(horizontal?barHeight:0);
    }
    const auto desired=(natural+QSize(vertical?barWidth:0,horizontal?barHeight:0)).boundedTo(available);
    if(w->size()!=desired || (native && native->size()!=desired)) {
        p.expectNativePosition(clientOrigin(w));
        w->resize(desired);
        // QWidget::resize can be a no-op while its cache still has the old
        // requested size and the platform has already configured a new one.
        if(native && native->size()!=desired) native->resize(desired);
    }
    w->layout()->activate();
    const auto target=boundedFrame(bounds,panelFrame(w).size(),bounds.topLeft()+p.localPosition,p.anchor);
    if(qEnvironmentVariableIsSet("OPENDESIGNER_PANEL_TRACE")) {
        QString trace; QDebug(&trace)<<"fit"<<w->windowTitle()<<"bounds"<<bounds<<"frame"<<w->frameGeometry()
            <<"pos"<<w->pos()<<"global"<<clientOrigin(w)<<"dpr"<<w->devicePixelRatioF()
            <<"anchor"<<p.anchor<<"local"<<p.localPosition<<"target"<<target;
        fprintf(stderr,"%s\n",qPrintable(trace));
    }
    moveFrame(p,target.topLeft());p.placing=false;
}
void CommandPanelPositioner::update() {watchWindows();for(const auto& p:panels_) fit(*p);}
bool CommandPanelPositioner::eventFilter(QObject* watched,QEvent* event) {
#if QT_VERSION >= QT_VERSION_CHECK(6,6,0)
    if(event->type()==QEvent::DevicePixelRatioChange) reconcile(true);
#endif
    for(const auto& entry:panels_) {
        auto& p=*entry;if(!p.widget) continue;
        if(watched==p.widget || watched==p.widget->windowHandle()) {
            if(event->type()==QEvent::NonClientAreaMouseButtonPress)
                p.systemDragging=static_cast<QMouseEvent*>(event)->button()==Qt::LeftButton;
            if(event->type()==QEvent::NonClientAreaMouseButtonRelease) p.systemDragging=false;
        }
        if(watched==p.widget->windowHandle() && event->type()==QEvent::Expose) {
            if(p.opening && !p.settling && p.widget->windowHandle()->isExposed()) {
                p.settling=true;
                QTimer::singleShot(0,this,[this,guard=QPointer<CommandPanelPositioner>(this),widget=p.widget] {
                    // Drain the native mapping/configure events and obtain the
                    // WM's frame margins before measuring or moving the frame.
                    // sync() processes events: both objects may be deleted.
                    QGuiApplication::sync();
                    if(!guard || !widget) return;
                    for(const auto& item:panels_) if(item->widget==widget) {
                        item->settling=false;
                        item->opening=!widget->windowHandle() || !widget->windowHandle()->isExposed();
                    }
                    reconcile();
                    update();
                });
            }
            schedule();
        }
        if(watched==p.handle && event->type()==QEvent::MouseButtonPress) {
            auto* mouse=static_cast<QMouseEvent*>(event);
            if(mouse->button()==Qt::LeftButton) {
                p.dragging=true;p.anchor=false;p.dragOffset=mouse->globalPosition().toPoint()-panelFrame(p.widget).topLeft();
                p.handle->grabMouse();return true;
            }
        }
        if(p.dragging && watched==p.handle && event->type()==QEvent::MouseMove) {
            auto* mouse=static_cast<QMouseEvent*>(event);
            const auto bounds=viewportBounds();
            p.placing=true;moveFrame(p,boundedFrame(bounds,panelFrame(p.widget).size(),mouse->globalPosition().toPoint()-p.dragOffset,false).topLeft());p.placing=false;
            return true;
        }
        if(p.dragging && watched==p.handle && event->type()==QEvent::MouseButtonRelease) {
            p.dragging=false;p.handle->releaseMouse();return true;
        }
        if(watched==p.widget) {
            // Native Tool windows can temporarily hide on application/dialog
            // activation changes. That must not reset a user's command position.
            if(event->type()==QEvent::Hide) {p.dragging=false;p.systemDragging=false;if(p.handle && QWidget::mouseGrabber()==p.handle) p.handle->releaseMouse();}
            if(event->type()==QEvent::Move && !p.placing && p.widget->isVisible()) {
                if(qEnvironmentVariableIsSet("OPENDESIGNER_PANEL_TRACE")) {
                    QString trace; QDebug(&trace)<<"move"<<p.widget->windowTitle()<<"frame"<<panelFrame(p.widget)<<"last"<<p.lastFramePosition<<"origin"<<viewportBounds().topLeft()<<"last origin"<<p.lastViewportOrigin<<"spontaneous"<<event->spontaneous();
                    fprintf(stderr,"%s\n",qPrintable(trace));
                }
                const auto origin=viewportBounds().topLeft();
                const auto native=std::find(p.nativePositions.begin(),p.nativePositions.end(),static_cast<QMoveEvent*>(event)->pos());
                const bool acknowledgement=event->spontaneous() && native!=p.nativePositions.end();
                if(acknowledgement) p.nativePositions.erase(native);
                const auto delta=panelFrame(p.widget).topLeft()-p.lastFramePosition;
                const bool moved=event->spontaneous()?(std::abs(delta.x())>1 || std::abs(delta.y())>1):!delta.isNull();
                // Fractional native frame rounding is not a drag. During a
                // screen/configure transaction keep the requested anchor/local
                // offset, while allowing an actual title-bar drag to take over.
                const bool marginsChanged=p.widget->windowHandle() && p.widget->windowHandle()->frameMargins()!=p.frameMargins;
                const bool configuring=event->spontaneous() && ((reconcileTimer_.isActive() && screenTransition_) || marginsChanged)
                    && !p.systemDragging && !(QGuiApplication::mouseButtons() & Qt::LeftButton);
                if(!p.opening && !acknowledgement && !configuring && origin==p.lastViewportOrigin && moved) {
                    p.anchor=false;p.localPosition=panelFrame(p.widget).topLeft()-origin;
                    p.nativePositions.clear();
                }
                schedule();
            }
            if(event->type()==QEvent::Show || event->type()==QEvent::Resize || event->type()==QEvent::LayoutRequest
               || event->type()==QEvent::WindowTitleChange || event->type()==QEvent::ScreenChangeInternal
               || event->type()==QEvent::StyleChange || event->type()==QEvent::FontChange) schedule();
        }
        if(watched==p.content && event->type()==QEvent::LayoutRequest) schedule();
    }
    if(event->type()==QEvent::ScreenChangeInternal) reconcile(true);
    // Observe native geometry as well as QWidget events. The native window can
    // be updated before QWidget's cached geometry/layout catches up.
    if(auto* window=qobject_cast<QWindow*>(watched)) {
        if(event->type()==QEvent::Move || event->type()==QEvent::Resize) {
            bool panelWindow=false;
            for(const auto& p:panels_) if(p->widget && p->widget->windowHandle()==window) {panelWindow=true;break;}
            if(panelWindow) schedule();else reconcile();
        }
    }
    if(event->type()==QEvent::Move || event->type()==QEvent::Resize || event->type()==QEvent::Show
       || event->type()==QEvent::LayoutRequest || event->type()==QEvent::ParentChange || event->type()==QEvent::ScreenChangeInternal) {
        for(auto* ancestor=viewport_;ancestor;ancestor=ancestor->parentWidget()) if(watched==ancestor) {reconcile();break;}
    }
    if(watched==viewport_ && event->type()==QEvent::ParentChange)
        for(auto* ancestor=viewport_;ancestor;ancestor=ancestor->parentWidget()) ancestor->installEventFilter(this);
    return QObject::eventFilter(watched,event);
}
