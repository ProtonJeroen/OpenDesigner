#include "CommandPanelPositioner.h"
#include <QDialog>
#include <QDebug>
#include <cstdio>
#include <QEvent>
#include <QLabel>
#include <QLayout>
#include <QMouseEvent>
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
    QPoint localPosition,lastFramePosition,lastViewportOrigin,dragOffset;
    std::vector<QPoint> nativePositions;
    bool anchor=true,placing=false,dragging=false,opening=true,settling=false;
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
}
CommandPanelPositioner::~CommandPanelPositioner() = default;
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
    if(widget->windowHandle()) widget->windowHandle()->installEventFilter(this);
    update();schedule();
}
void CommandPanelPositioner::schedule() {
    if(pending_) return;
    pending_=true;QTimer::singleShot(0,this,[this]{pending_=false;update();});
}
void CommandPanelPositioner::moveFrame(Panel& p,const QPoint& position) {
    p.lastFramePosition=position;p.lastViewportOrigin=viewport_->mapToGlobal(QPoint());
    p.localPosition=position-p.lastViewportOrigin;
    auto* w=p.widget.data();const auto delta=position-w->frameGeometry().topLeft();
    if(!delta.isNull()) {
        p.expectNativePosition(w->mapToGlobal(QPoint()));
        w->move(w->pos()+delta);
        p.expectNativePosition(w->mapToGlobal(QPoint()));
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
    const auto decorations=w->frameGeometry().size()-w->size();
    const auto available=(viewport_->size()-decorations-QSize(2*Margin,2*Margin)).expandedTo(QSize(1,1));
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
    if(w->size()!=desired) {
        p.expectNativePosition(w->mapToGlobal(QPoint()));
        w->resize(desired);
    }
    w->layout()->activate();
    const QRect bounds(viewport_->mapToGlobal(QPoint()),viewport_->size());
    const auto target=boundedFrame(bounds,w->frameGeometry().size(),bounds.topLeft()+p.localPosition,p.anchor);
    if(qEnvironmentVariableIsSet("OPENDESIGNER_PANEL_TRACE")) {
        QString trace; QDebug(&trace)<<"fit"<<w->windowTitle()<<"bounds"<<bounds<<"frame"<<w->frameGeometry()
            <<"pos"<<w->pos()<<"global"<<w->mapToGlobal(QPoint())<<"dpr"<<w->devicePixelRatioF()
            <<"anchor"<<p.anchor<<"local"<<p.localPosition<<"target"<<target;
        fprintf(stderr,"%s\n",qPrintable(trace));
    }
    moveFrame(p,target.topLeft());p.placing=false;
}
void CommandPanelPositioner::update() {for(const auto& p:panels_) fit(*p);}
bool CommandPanelPositioner::eventFilter(QObject* watched,QEvent* event) {
#if QT_VERSION >= QT_VERSION_CHECK(6,6,0)
    if(event->type()==QEvent::DevicePixelRatioChange) schedule();
#endif
    for(const auto& entry:panels_) {
        auto& p=*entry;if(!p.widget) continue;
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
                    update();
                });
            }
            schedule();
        }
        if(watched==p.handle && event->type()==QEvent::MouseButtonPress) {
            auto* mouse=static_cast<QMouseEvent*>(event);
            if(mouse->button()==Qt::LeftButton) {
                p.dragging=true;p.anchor=false;p.dragOffset=mouse->globalPosition().toPoint()-p.widget->frameGeometry().topLeft();
                p.handle->grabMouse();return true;
            }
        }
        if(p.dragging && watched==p.handle && event->type()==QEvent::MouseMove) {
            auto* mouse=static_cast<QMouseEvent*>(event);
            const QRect bounds(viewport_->mapToGlobal(QPoint()),viewport_->size());
            p.placing=true;moveFrame(p,boundedFrame(bounds,p.widget->frameGeometry().size(),mouse->globalPosition().toPoint()-p.dragOffset,false).topLeft());p.placing=false;
            return true;
        }
        if(p.dragging && watched==p.handle && event->type()==QEvent::MouseButtonRelease) {
            p.dragging=false;p.handle->releaseMouse();return true;
        }
        if(watched==p.widget) {
            // Native Tool windows can temporarily hide on application/dialog
            // activation changes. That must not reset a user's command position.
            if(event->type()==QEvent::Hide) {p.dragging=false;if(p.handle && QWidget::mouseGrabber()==p.handle) p.handle->releaseMouse();}
            if(event->type()==QEvent::Move && !p.placing && p.widget->isVisible()) {
                if(qEnvironmentVariableIsSet("OPENDESIGNER_PANEL_TRACE")) {
                    QString trace; QDebug(&trace)<<"move"<<p.widget->windowTitle()<<"frame"<<p.widget->frameGeometry()<<"last"<<p.lastFramePosition<<"origin"<<viewport_->mapToGlobal(QPoint())<<"last origin"<<p.lastViewportOrigin<<"spontaneous"<<event->spontaneous();
                    fprintf(stderr,"%s\n",qPrintable(trace));
                }
                const auto origin=viewport_->mapToGlobal(QPoint());
                const auto native=std::find(p.nativePositions.begin(),p.nativePositions.end(),p.widget->mapToGlobal(QPoint()));
                const bool acknowledgement=event->spontaneous() && native!=p.nativePositions.end();
                if(acknowledgement) p.nativePositions.erase(native);
                if(!p.opening && !acknowledgement && origin==p.lastViewportOrigin && p.widget->frameGeometry().topLeft()!=p.lastFramePosition) {
                    p.anchor=false;p.localPosition=p.widget->frameGeometry().topLeft()-origin;
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
    if(event->type()==QEvent::Move || event->type()==QEvent::Resize || event->type()==QEvent::Show
       || event->type()==QEvent::LayoutRequest || event->type()==QEvent::ParentChange || event->type()==QEvent::ScreenChangeInternal) {
        for(auto* ancestor=viewport_;ancestor;ancestor=ancestor->parentWidget()) if(watched==ancestor) {schedule();break;}
    }
    if(watched==viewport_ && event->type()==QEvent::ParentChange)
        for(auto* ancestor=viewport_;ancestor;ancestor=ancestor->parentWidget()) ancestor->installEventFilter(this);
    return QObject::eventFilter(watched,event);
}
