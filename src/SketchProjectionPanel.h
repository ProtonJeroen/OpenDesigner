#pragma once
#include <QDialog>
#include <QStringList>
#include <functional>
class QComboBox;
class QLabel;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QDialogButtonBox;

class SketchProjectionPanel final : public QDialog {
public:
    enum class Mode { Vertex, Edge, Face, Mixed };
    explicit SketchProjectionPanel(const QString& workplaneName,QWidget* parent);
    std::function<void()> modeChanged,clearSources,removeSources,confirmed,cancelled;
    QComboBox* mode=nullptr;
    QListWidget* sources=nullptr;
    QPushButton *clear=nullptr,*remove=nullptr;
    QDialogButtonBox* buttons=nullptr;
    void setResult(const QStringList& labels,int entities,const QString& message,
                   const QStringList& diagnostics,bool valid);
protected:
    void reject() override;
    void closeEvent(QCloseEvent* event) override;
private:
    QLabel *counts_=nullptr,*status_=nullptr;
    QPlainTextEdit* diagnostics_=nullptr;
};
