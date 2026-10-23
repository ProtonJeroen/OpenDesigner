#pragma once
#include <QDialog>
#include <functional>
class QLabel;
class QComboBox;
class QPushButton;
class QDialogButtonBox;
class SketchOverdrawPanel final : public QDialog {
public:
    explicit SketchOverdrawPanel(const QString& planeName,QWidget* parent);
    std::function<void()> choose,back,newStart,confirmed,cancelled;
    std::function<void(int)> candidateChanged;
    QComboBox* candidates=nullptr;
    QPushButton *chooseButton=nullptr,*backButton=nullptr,*newStartButton=nullptr;
    QDialogButtonBox* buttons=nullptr;
    void update(const QString& prompt,const QString& notice,const QStringList& labels,
                int selected,int chosen,bool hasStart,bool valid);
protected:
    void reject() override;
private:
    QLabel *prompt_=nullptr,*notice_=nullptr,*count_=nullptr;
};
