#pragma once

#include "Settings.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QSpinBox;

/// Configuration editor.
class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget* parent = nullptr);

    void setConfig(const AppConfig& cfg);
    AppConfig config() const;

    /// Human readable status of the OCR engine shown in the dialog.
    void setEngineInfo(const QString& executable, const QString& tessdata);

    /// Shows whether the global show/hide hotkey could be registered.
    void setHotkeyInfo(bool available);

signals:
    /// Emitted whenever a visual-setting widget (font size, opacity, chip count)
    /// changes so the overlay bar can update its appearance live without OK.
    void previewRequested(const AppConfig& cfg);

private:
    void buildUi();
    void browseForExecutable();
    void applyToWidgets(const AppConfig& cfg);
    void resetToDefaults();

    AppConfig m_base;
    QSpinBox* m_interval = nullptr;
    QComboBox* m_captureMode = nullptr;
    QComboBox* m_scale = nullptr;
    QComboBox* m_psm = nullptr;
    QSpinBox* m_confidence = nullptr;
    QSpinBox* m_minWordLength = nullptr;
    QLineEdit* m_tesseractPath = nullptr;
    QLabel* m_engineState = nullptr;
    QCheckBox* m_filterCommon = nullptr;
    QComboBox* m_commonCount = nullptr;
    QCheckBox* m_strictDictionary = nullptr;
    QSpinBox* m_maxChips = nullptr;
    QSpinBox* m_fontSize = nullptr;
    QSpinBox* m_opacity = nullptr;
    QSpinBox* m_ttl = nullptr;
    QCheckBox* m_keepUntilHandled = nullptr;
    QCheckBox* m_autoSnooze = nullptr;
    QSpinBox* m_autoSnoozeScans = nullptr;
    QSpinBox* m_autoSnoozeSeconds = nullptr;
    QCheckBox* m_autoHide = nullptr;
    QSpinBox* m_autoHideSeconds = nullptr;
    QLabel* m_hotkeyHint = nullptr;
};
