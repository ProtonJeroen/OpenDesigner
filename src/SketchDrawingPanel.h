#pragma once
#include "SketchGeometry.h"
#include <QDialog>
#include <functional>
#include <set>
class QDoubleSpinBox;
class QComboBox;
class QPushButton;
class QLabel;
class QDialogButtonBox;
class QFormLayout;
class QIcon;
namespace Sketch {
enum class DrawTool { Segment, InfiniteLine, Circle, Arc, Ellipse, EllipticalArc, HorizontalInfiniteLine, VerticalInfiniteLine, Spline };
inline bool ellipseTool(DrawTool tool) {return tool==DrawTool::Ellipse || tool==DrawTool::EllipticalArc;}
inline bool orthogonalLineTool(DrawTool tool) {return tool==DrawTool::HorizontalInfiniteLine || tool==DrawTool::VerticalInfiniteLine;}
inline bool infiniteLineTool(DrawTool tool) {return tool==DrawTool::InfiniteLine || orthogonalLineTool(tool);}
QIcon orthogonalLineIcon(DrawTool tool);
}

class SketchDrawingPanel final : public QDialog {
public:
    SketchDrawingPanel(Sketch::DrawTool tool, Sketch::Role role, const QString& planeName, QWidget* parent);
    std::function<void()> useXY, usePolar, back, confirmed, cancelled, roleChanged, directionChanged;
    std::function<void()> ellipseParametersChanged;
    std::function<void()> orthogonalPositionChanged;
    QDoubleSpinBox *x=nullptr,*y=nullptr,*distance=nullptr,*angle=nullptr;
    QDoubleSpinBox *majorRadius=nullptr,*axisAngle=nullptr,*minorRadius=nullptr,*startParameter=nullptr,*sweepParameter=nullptr;
    QComboBox *role=nullptr,*direction=nullptr;
    QPushButton *xyButton=nullptr,*polarButton=nullptr,*backButton=nullptr;
    void finishEditing();
    void setCursor(Sketch::Point p,double length,double degrees);
    void setArcSweep(double degrees);
    void setStep(int points,bool complete,bool valid,const QString& message);
    void setEllipseValues(const Sketch::Ellipse& ellipse,double start,double sweep,bool force=false);
    void resetEllipseEdits() {ellipseEdits_.clear();}
    bool hasEllipseEdits() const {return !ellipseEdits_.empty();}
    bool ellipseInputEdited(const QDoubleSpinBox* field) const {return ellipseEdits_.contains(field);}
    bool parameterWidget(const QWidget* widget) const;
protected:
    void reject() override;
private:
    Sketch::DrawTool tool_;
    QLabel *status_=nullptr,*distanceLabel_=nullptr,*angleLabel_=nullptr;
    QDialogButtonBox* buttons_=nullptr;
    QFormLayout* form_=nullptr;
    std::set<const QDoubleSpinBox*> ellipseEdits_;
    void setEllipseStep(int points,bool complete,bool valid,const QString& message);
};
