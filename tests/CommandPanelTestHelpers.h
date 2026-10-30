#pragma once
#include <QDebug>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QTimer>
#include <QWidget>
#include <QWindow>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace CommandPanelTest {
inline void waitForInitialAnchor(QWidget& panel,QWidget& viewport,const char* failureMessage) {
    // Mapping, exposure and the positioner's queued fit involve native events.
    // A fixed settle interval can expire before the WM has placed the frame.
    QElapsedTimer timer;timer.start();
    constexpr int timeoutMs=2000;
    for(;;) {
        const QRect bounds(viewport.mapToGlobal(QPoint()),viewport.size());
        const auto frame=panel.frameGeometry();
        if(bounds.contains(frame) && std::abs(bounds.right()-frame.right()-12)<=1
           && std::abs(frame.top()-bounds.top()-12)<=1) return;
        const auto elapsed=timer.elapsed();
        if(elapsed>=timeoutMs) {
            auto* native=panel.windowHandle();
            QString diagnostic;
            QDebug(&diagnostic).nospace()<<failureMessage<<": anchor timeout: panel frame="<<frame
                <<" client="<<panel.geometry()<<" viewport="<<bounds
                <<" expected frame top-left="<<QPoint(bounds.right()-frame.width()+1-12,bounds.top()+12)
                <<" visible="<<panel.isVisible()<<" native="<<native
                <<" native visible="<<(native && native->isVisible())
                <<" exposed="<<(native && native->isExposed())
                <<" window state="<<(native?native->windowState():panel.windowState())
                <<" native geometry="<<(native?native->geometry():QRect())
                <<" frame margins="<<(native?native->frameMargins():QMargins())
                <<" platform="<<QGuiApplication::platformName()<<" dpr="<<panel.devicePixelRatioF()
                <<" elapsed ms="<<elapsed;
            std::cerr<<diagnostic.toStdString()<<'\n';
            throw std::runtime_error(failureMessage);
        }
        QEventLoop loop;
        QTimer::singleShot(static_cast<int>(std::min<qint64>(10,timeoutMs-elapsed)),&loop,&QEventLoop::quit);
        loop.exec();
    }
}
}
