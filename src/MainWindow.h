#pragma once
#include <QMainWindow>
class QTreeWidget;
class CadViewport;
class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
private:
    CadViewport* viewport_ = nullptr;
    QTreeWidget* structure_ = nullptr;
};
