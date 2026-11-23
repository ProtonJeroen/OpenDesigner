#pragma once
// Shared GUI-test utility for the modal "Unsaved changes" prompt.
//
// MainWindow::maybeSave() (src/MainWindow.cpp:586) opens a modal QMessageBox
// offering Save / Discard / Cancel whenever the document is dirty. It is reached
// from closeEvent(), newDocument() and openDocument(). Under an unattended
// CTest run nobody clicks it, so the guarded call blocks inside QDialog::exec
// until the test times out with no output.
//
// UnsavedPromptGuard answers that prompt with Discard, and only:
//   * for a prompt that belongs to that window - the window must report unsaved
//     changes and the box must carry the Save/Discard/Cancel set that only
//     maybeSave() creates (Qt reparents an open modal QDialog, so the parent
//     link itself is checked only when Qt keeps it);
//   * for the document the test declares it owns (see the two claims below);
//   * for the lifetime of the guard, never globally.
// A prompt the guard does not recognise is left open and reported once on
// stderr, so a production change to the dialog surfaces as a diagnosable
// timeout instead of being silently swallowed.
//
// Discard is the only non-blocking answer: Save would run saveDocument(), which
// with an empty documentPath_ opens a modal QFileDialog, and Cancel makes the
// guarded call a no-op and leaves the window open.

#include <QAbstractButton>
#include <QApplication>
#include <QChar>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QString>
#include <QTimer>
#include <QWidget>
#include <iostream>
#include <stdexcept>
#include <string>

#include "MainWindow.h"

namespace MainWindowTest {

// True when `path` lies inside the system temporary directory, which is where
// QTemporaryDir/QTemporaryFile put the fixtures the repository tests create.
// Anything else is treated as a real user document.
inline bool insideTemporaryDirectory(const QString& path) {
    const auto root = QFileInfo(QDir::tempPath()).canonicalFilePath();
    if (root.isEmpty()) return false;
    const auto directory = QFileInfo(QDir::cleanPath(path)).absolutePath();
    return directory == root || directory.startsWith(root + QLatin1Char('/'));
}

// The document label MainWindow::updateWindowTitle() shows: "Untitled" while
// no file is bound to the document, otherwise the file name of documentPath_.
// A trailing " *" marks a dirty document and is stripped.
inline QString documentLabel(const MainWindow& window) {
    const auto title = window.windowTitle();
    const auto separator = title.indexOf(QChar(0x2014)); // the em dash separator
    if (separator < 0) return QString();
    auto label = title.mid(separator + 1).trimmed();
    if (label.endsWith(QStringLiteral(" *"))) label.chop(2);
    return label;
}

struct PromptReport {
    int answered = 0; // prompts answered with Discard
    int refused = 0;  // modal prompts deliberately left open
};

// Declare the document the test owns, then run the guarded call:
//
//   UnsavedPromptGuard guard(window, QString(), report);   // untitled document
//   UnsavedPromptGuard guard(window, window.documentPath_, report);
//
// An empty path claims the document was never bound to a file, so Discard
// cannot destroy anything on disk. A non-empty path must be a fixture the test
// created under the system temporary directory, and must be the document the
// window actually shows. A mismatch throws from the constructor before the
// guarded call starts, so a real user document is never silently discarded.
class UnsavedPromptGuard {
public:
    explicit UnsavedPromptGuard(MainWindow& window, const QString& testOwnedPath, PromptReport& report)
        : window_(window), report_(report) {
        validate(testOwnedPath);
        timer_.setInterval(20);
        QObject::connect(&timer_, &QTimer::timeout, [this] { answer(); });
        timer_.start();
    }
    ~UnsavedPromptGuard() { timer_.stop(); }

private:
    void validate(const QString& testOwnedPath) const {
        const auto label = documentLabel(window_);
        if (testOwnedPath.isEmpty()) {
            if (label != QStringLiteral("Untitled"))
                throw std::runtime_error("UnsavedPromptGuard: window shows \"" + label.toStdString()
                    + "\"; declare the test-owned path instead of claiming an untitled document");
            return;
        }
        if (!insideTemporaryDirectory(testOwnedPath))
            throw std::runtime_error("UnsavedPromptGuard: \"" + testOwnedPath.toStdString()
                + "\" is outside the system temporary directory; refusing to discard a real user document");
        const auto fileName = QFileInfo(testOwnedPath).fileName();
        if (label != fileName)
            throw std::runtime_error("UnsavedPromptGuard: window shows \"" + label.toStdString()
                + "\", not the declared test document \"" + fileName.toStdString() + "\"");
    }

    void answer() {
        auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (!box) return;
        // Qt reparents a modal QDialog while it is open: measured against the
        // production prompt, QWidget::parentWidget() is null there, so the
        // window link is only checked when Qt keeps it.
        if (auto* owner = box->parentWidget(); owner && owner != &window_) {
            diagnose("modal prompt belongs to another window");
            return;
        }
        // The prompt must belong to a document this window reports as dirty.
        if (!window_.windowTitle().endsWith(QStringLiteral(" *"))) {
            diagnose("window reports no unsaved changes, so the prompt is not ours");
            return;
        }
        auto* discard = box->button(QMessageBox::Discard);
        if (!discard || !box->button(QMessageBox::Save) || !box->button(QMessageBox::Cancel)) {
            diagnose("modal prompt is not the Unsaved changes dialog: \""
                + box->text().toStdString() + "\"");
            return;
        }
        ++report_.answered;
        discard->click();
    }

    void diagnose(const std::string& reason) {
        if (diagnosed_) return;
        diagnosed_ = true;
        ++report_.refused;
        std::cerr << "UnsavedPromptGuard: leaving prompt open - " << reason << '\n';
    }

    MainWindow& window_;
    PromptReport& report_;
    QTimer timer_;
    bool diagnosed_ = false;
};

}
