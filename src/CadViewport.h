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
signals:
    void partCreated(const QString& name);
    void statusMessage(const QString& message);
protected:
    QPaintEngine* paintEngine() const override;
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
private:
    struct BoxPart {
        Handle(AIS_Shape) presentation;
        std::array<double, 3> min;
        std::array<double, 3> max;
    };
    void initializeViewer();
    Handle(Aspect_DisplayConnection) display_;
    Handle(V3d_Viewer) viewer_;
    Handle(V3d_View) view_;
    Handle(AIS_InteractiveContext) context_;
    std::vector<BoxPart> boxes_;
    QPoint previous_;
    bool rotating_ = false;
    bool panning_ = false;
    int partCount_ = 0;
};
