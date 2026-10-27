#pragma once

#include <QAbstractNativeEventFilter>

#include <functional>

/// Registers Ctrl+Alt+W as a system wide hotkey.
class HotkeyFilter : public QAbstractNativeEventFilter
{
public:
    std::function<void()> onToggle;

    bool registerToggleHotkey();
    void unregister();
    bool isRegistered() const { return m_registered; }

    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

private:
    bool m_registered = false;
};
