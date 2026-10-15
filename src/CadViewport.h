#pragma once
#include <QWidget>
#include <QPoint>
#include <AIS_InteractiveContext.hxx>
#include <V3d_Viewer.hxx>
#include <V3d_View.hxx>
#include <Aspect_DisplayConnection.hxx>

class CadViewport final : public QWidget {
    Q_OBJECT
public:
    explicit CadViewport(QWidget* parent = nullptr);
    void addBox();
    void fitAll();
signals:
    void partCreated(const QString& name);
protected:
    QPaintEngine* paintEngine() const override;
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
private:
    void initializeViewer();
    Handle(Aspect_DisplayConnection) display_;
    Handle(V3d_Viewer) viewer_;
    Handle(V3d_View) view_;
    Handle(AIS_InteractiveContext) context_;
    QPoint previous_;
    bool rotating_ = false;
    bool panning_ = false;
    int partCount_ = 0;
};
