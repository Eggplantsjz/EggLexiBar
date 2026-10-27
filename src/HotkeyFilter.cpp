#include "HotkeyFilter.h"

#include <QByteArray>

#ifdef Q_OS_WIN
#  include <windows.h>
#endif

namespace {
constexpr int kHotkeyId = 0xA117;
}

bool HotkeyFilter::registerToggleHotkey()
{
#ifdef Q_OS_WIN
    if (m_registered)
        return true;
    m_registered = RegisterHotKey(nullptr, kHotkeyId,
                                  MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'W')
        != FALSE;
    return m_registered;
#else
    return false;
#endif
}

void HotkeyFilter::unregister()
{
#ifdef Q_OS_WIN
    if (m_registered) {
        UnregisterHotKey(nullptr, kHotkeyId);
        m_registered = false;
    }
#endif
}

bool HotkeyFilter::nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result)
{
    Q_UNUSED(result);
#ifdef Q_OS_WIN
    if (eventType != QByteArrayLiteral("windows_generic_MSG")
        && eventType != QByteArrayLiteral("windows_dispatcher_MSG"))
        return false;
    auto* msg = static_cast<MSG*>(message);
    if (msg && msg->message == WM_HOTKEY && int(msg->wParam) == kHotkeyId) {
        if (onToggle)
            onToggle();
        return true;
    }
#else
    Q_UNUSED(eventType);
    Q_UNUSED(message);
#endif
    return false;
}
