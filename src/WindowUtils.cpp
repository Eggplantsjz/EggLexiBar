#include "WindowUtils.h"

#include <QCursor>
#include <QGuiApplication>
#include <QScreen>

#ifdef Q_OS_WIN
#  include <windows.h>
#  include <dwmapi.h>
#endif

QRect activeWindowRect()
{
#ifdef Q_OS_WIN
    const HWND hwnd = GetForegroundWindow();
    if (!hwnd || IsIconic(hwnd))
        return {};

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == GetCurrentProcessId())
        return {};

    wchar_t className[128] = {};
    GetClassNameW(hwnd, className, 127);
    const QString cls = QString::fromWCharArray(className);
    if (cls == QLatin1String("Progman") || cls == QLatin1String("WorkerW")
        || cls == QLatin1String("Shell_TrayWnd") || cls == QLatin1String("Shell_SecondaryTrayWnd")
        || cls == QLatin1String("SysShadow"))
        return {};

    RECT rect{};
    if (FAILED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &rect, sizeof(rect)))) {
        if (!GetWindowRect(hwnd, &rect))
            return {};
    }

    const int width = int(rect.right - rect.left);
    const int height = int(rect.bottom - rect.top);
    if (width < 160 || height < 100)
        return {};

    QScreen* screen = QGuiApplication::screenAt(QPoint((rect.left + rect.right) / 2,
                                                       (rect.top + rect.bottom) / 2));
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    const qreal dpr = screen ? screen->devicePixelRatio() : 1.0;

    return QRect(int(rect.left / dpr), int(rect.top / dpr),
                 int(width / dpr), int(height / dpr));
#else
    return {};
#endif
}

QRect cursorRegionRect(const QSize& size)
{
    const QPoint cursor = QCursor::pos();
    return QRect(cursor - QPoint(size.width() / 2, size.height() / 2), size);
}

QRect screenRectAtCursor()
{
    QScreen* screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    return screen ? screen->geometry() : QRect();
}
