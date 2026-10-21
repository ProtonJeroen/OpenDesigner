#pragma once
#include "ConstructionGeometry.h"
#include <QDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QLabel>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <array>
#include <functional>

// Parameter UI only. The viewport adapter supplies references and owns the session.
class ConstructionPanel final : public QDialog {
public:
    explicit ConstructionPanel(const Construction::Definition& definition,QWidget* parent);
    std::function<void()> changed, confirmed, cancelled;
    std::function<void(size_t)> pickRequested;
    Construction::Request request() const;
    void finishEditing();
    void setValid(bool valid,const QString& message);
    void setVector(const gp_Vec& value);
    void setValue(double value);
    QLineEdit* name = nullptr;
    std::vector<QComboBox*> roles;
    std::array<QDoubleSpinBox*,3> vector{},angles{};
    QDoubleSpinBox* value = nullptr;
    QCheckBox* reverse = nullptr;
    QComboBox* midplaneSolution = nullptr;
protected:
    void reject() override;
private:
    Construction::Method method_;
    QLabel* status_ = nullptr;
    QDialogButtonBox* buttons_ = nullptr;
};
