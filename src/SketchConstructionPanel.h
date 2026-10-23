#pragma once
#include "SketchConstruction.h"
#include <QDialog>
#include <functional>
class QComboBox;
class QDoubleSpinBox;
class QCheckBox;
class QPushButton;
class QLabel;
class QDialogButtonBox;

class SketchConstructionPanel final : public QDialog {
public:
    SketchConstructionPanel(Sketch::ConstructionTool tool,const QString& planeName,QWidget* parent);
    std::function<void()> parametersChanged,confirmed,cancelled;
    std::function<void(int)> referenceChanged,pickReference,solutionChanged;
    QComboBox *references[2]{},*output=nullptr,*side=nullptr,*solutions=nullptr;
    QPushButton* pickButtons[2]{};
    QDoubleSpinBox *distance=nullptr,*intervalStart=nullptr,*intervalLength=nullptr;
    QCheckBox *mouseSide=nullptr,*explicitInterval=nullptr;
    QDialogButtonBox* buttons=nullptr;
    void finishEditing();
    void setIntervalVisible(bool visible);
    void setStatus(const QString& message,bool valid);
protected:
    void reject() override;
private:
    QLabel* prompt_=nullptr;
    QWidget* intervalGroup_=nullptr;
};
