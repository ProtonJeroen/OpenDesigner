#pragma once
#include <QObject>
#include <QPoint>
#include <QRect>
#include <QPointer>
#include <QTimer>
#include <memory>
#include <vector>
class QWidget;
class QWindow;
class QScreen;

// UI placement only. Does not own command state or intercept keyboard input.
class CommandPanelPositioner final : public QObject {
public:
    explicit CommandPanelPositioner(QWidget* viewport);
    ~CommandPanelPositioner() override;
    void manage(QWidget* panel,bool addDragHandle=false);
    void show(QWidget* panel);
    void update();
    static constexpr int Margin=12; // Qt logical pixels, including on HiDPI screens.
    static QRect boundedFrame(const QRect& viewport,const QSize& frameSize,
                              const QPoint& requested,bool anchor);
protected:
    bool eventFilter(QObject* watched,QEvent* event) override;
private:
    struct Panel;
    QWidget* viewport_;
    std::vector<std::unique_ptr<Panel>> panels_;
    bool pending_=false;
    QTimer reconcileTimer_;
    int reconcilePasses_=0;
    bool screenTransition_=false;
    std::vector<QPointer<QWindow>> windows_;
    std::vector<QPointer<QScreen>> screens_;
    QRect viewportBounds() const;
    void watchWindows();
    void reconcile(bool screenTransition=false);
    void schedule();
    void fit(Panel& panel);
    void moveFrame(Panel& panel,const QPoint& position);
};
