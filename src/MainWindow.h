#pragma once
#include <QMainWindow>
#include <QString>
class QTreeWidget;
class QTreeWidgetItem;
class QAction;
class QCloseEvent;
class CadViewport;
class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
private:
    friend struct NativeDocumentSessionTest;
    bool maybeSave();
    bool saveDocument();
    bool saveDocumentAs();
    void newDocument();
    void openDocument();
    void markDocumentDirty();
    void updateWindowTitle();
    void rebuildStructureBrowser();
    CadViewport* viewport_ = nullptr;
    QTreeWidget* structure_ = nullptr;
    QTreeWidgetItem* root_ = nullptr;
    QString documentPath_;
    bool documentDirty_ = false;
    bool documentTransition_ = false;
    QAction* saveAction_ = nullptr;
    QAction* saveAsAction_ = nullptr;
protected:
    void closeEvent(QCloseEvent* event) override;
};
