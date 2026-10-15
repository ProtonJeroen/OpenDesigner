#pragma once
#include <QWidget>
#include <QPoint>
#include <AIS_InteractiveContext.hxx>
#include <AIS_Shape.hxx>
#include <V3d_Viewer.hxx>
#include <V3d_View.hxx>
#include <Aspect_DisplayConnection.hxx>
#include <vector>
#include <array>

class CadViewport final : public QWidget {
    Q_OBJECT
public:
    explicit CadViewport(QWidget* parent = nullptr);
    void addBox();
    void fitAll();
    void pullSelectedFace();
    void undoPull();
    void redoPull();
signals:
    void partCreated(const QString& name);
    void statusMessage(const QString& message);
protected:
    QPaintEngine* paintEngine() const override;
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
private:
    struct BoxPart {
        Handle(AIS_Shape) presentation;
        std::array<double, 3> min;
        std::array<double, 3> max;
    };
    struct PullChange {
        size_t partIndex;
        std::array<double, 3> beforeMin, beforeMax;
        std::array<double, 3> afterMin, afterMax;
    };
    void applyBoxBounds(size_t index, const std::array<double, 3>& min,
                        const std::array<double, 3>& max);
    bool beginFaceDrag(const QPoint& origin);
    void initializeViewer();
    Handle(Aspect_DisplayConnection) display_;
    Handle(V3d_Viewer) viewer_;
    Handle(V3d_View) view_;
    Handle(AIS_InteractiveContext) context_;
    std::vector<BoxPart> boxes_;
    std::vector<PullChange> undoStack_;
    std::vector<PullChange> redoStack_;
    QPoint previous_;
    bool rotating_ = false;
    bool panning_ = false;
    bool draggingFace_ = false;
    size_t dragIndex_ = 0;
    int dragAxis_ = 0;
    bool dragMax_ = true;
    QPoint dragStart_;
    std::array<double, 3> dragMin_{}, dragMaxBounds_{};
    double dragDistance_ = 0.0;
    int partCount_ = 0;
};
