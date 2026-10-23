#pragma once
#include "SketchGeometry.h"
#include <QDialog>
#include <functional>
class QDoubleSpinBox;
class QComboBox;
class QPushButton;
class QLabel;
class QDialogButtonBox;
namespace Sketch { enum class DrawTool { Segment, InfiniteLine, Circle, Arc }; }

class SketchDrawingPanel final : public QDialog {
public:
    SketchDrawingPanel(Sketch::DrawTool tool, Sketch::Role role, const QString& planeName, QWidget* parent);
    std::function<void()> useXY, usePolar, back, confirmed, cancelled, roleChanged, directionChanged;
    QDoubleSpinBox *x=nullptr,*y=nullptr,*distance=nullptr,*angle=nullptr;
    QComboBox *role=nullptr,*direction=nullptr;
    QPushButton *xyButton=nullptr,*polarButton=nullptr,*backButton=nullptr;
    void finishEditing();
    void setCursor(Sketch::Point p,double length,double degrees);
    void setArcSweep(double degrees);
    void setStep(int points,bool complete,bool valid,const QString& message);
protected:
    void reject() override;
private:
    Sketch::DrawTool tool_;
    QLabel *status_=nullptr,*distanceLabel_=nullptr,*angleLabel_=nullptr;
    QDialogButtonBox* buttons_=nullptr;
};
