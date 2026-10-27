#include "SettingsDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

SettingsDialog::SettingsDialog(QWidget* parent)
    : QDialog(parent)
{
    buildUi();
}

void SettingsDialog::buildUi()
{
    setWindowTitle(QStringLiteral("WordLens · 设置"));
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    setModal(true);
    resize(560, 780);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 12);
    root->setSpacing(12);

    // --- OCR ---------------------------------------------------------------
    auto* ocrGroup = new QGroupBox(QStringLiteral("识别"), this);
    auto* ocrForm = new QFormLayout(ocrGroup);
    ocrForm->setLabelAlignment(Qt::AlignLeft);
    ocrForm->setSpacing(8);

    m_interval = new QSpinBox(ocrGroup);
    m_interval->setRange(300, 10000);
    m_interval->setSingleStep(100);
    m_interval->setSuffix(QStringLiteral(" ms"));
    ocrForm->addRow(QStringLiteral("识别间隔"), m_interval);

    m_captureMode = new QComboBox(ocrGroup);
    m_captureMode->addItem(QStringLiteral("整个屏幕"), 0);
    m_captureMode->addItem(QStringLiteral("当前窗口"), 1);
    m_captureMode->addItem(QStringLiteral("鼠标周围 900×560"), 2);
    m_captureMode->setToolTip(QStringLiteral("屏幕很大时，缩小识别范围可以显著加快速度"));
    ocrForm->addRow(QStringLiteral("识别范围"), m_captureMode);

    m_scale = new QComboBox(ocrGroup);
    m_scale->addItem(QStringLiteral("快速（原分辨率）"), 100);
    m_scale->addItem(QStringLiteral("标准（放大 1.5 倍）"), 150);
    m_scale->addItem(QStringLiteral("精细（放大 2 倍，较慢）"), 200);
    ocrForm->addRow(QStringLiteral("识别精度"), m_scale);

    m_psm = new QComboBox(ocrGroup);
    m_psm->addItem(QStringLiteral("稀疏文本（推荐）"), 11);
    m_psm->addItem(QStringLiteral("自动排版"), 3);
    m_psm->addItem(QStringLiteral("单一文本块"), 6);
    ocrForm->addRow(QStringLiteral("页面模式"), m_psm);

    m_confidence = new QSpinBox(ocrGroup);
    m_confidence->setRange(0, 100);
    m_confidence->setSuffix(QStringLiteral(" %"));
    ocrForm->addRow(QStringLiteral("置信度下限"), m_confidence);

    m_minWordLength = new QSpinBox(ocrGroup);
    m_minWordLength->setRange(2, 8);
    ocrForm->addRow(QStringLiteral("最小词长"), m_minWordLength);

    auto* engineRow = new QWidget(ocrGroup);
    auto* engineLayout = new QHBoxLayout(engineRow);
    engineLayout->setContentsMargins(0, 0, 0, 0);
    engineLayout->setSpacing(6);
    m_tesseractPath = new QLineEdit(engineRow);
    m_tesseractPath->setPlaceholderText(QStringLiteral("留空自动检测"));
    auto* browse = new QPushButton(QStringLiteral("浏览…"), engineRow);
    browse->setFixedWidth(64);
    engineLayout->addWidget(m_tesseractPath, 1);
    engineLayout->addWidget(browse);
    ocrForm->addRow(QStringLiteral("Tesseract"), engineRow);

    m_engineState = new QLabel(ocrGroup);
    m_engineState->setWordWrap(true);
    m_engineState->setStyleSheet(QStringLiteral("color:#98A1B4;"));
    ocrForm->addRow(QString(), m_engineState);

    root->addWidget(ocrGroup);

    // --- filtering ---------------------------------------------------------
    auto* filterGroup = new QGroupBox(QStringLiteral("生词过滤"), this);
    auto* filterForm = new QFormLayout(filterGroup);
    filterForm->setSpacing(8);

    m_filterCommon = new QCheckBox(QStringLiteral("只显示生词（过滤常用词）"), filterGroup);
    filterForm->addRow(QString(), m_filterCommon);

    m_commonCount = new QComboBox(filterGroup);
    m_commonCount->addItem(QStringLiteral("最常用 1000 词"), 1000);
    m_commonCount->addItem(QStringLiteral("最常用 2000 词"), 2000);
    m_commonCount->addItem(QStringLiteral("最常用 3000 词"), 3000);
    m_commonCount->addItem(QStringLiteral("最常用 5000 词"), 5000);
    m_commonCount->addItem(QStringLiteral("最常用 8000 词"), 8000);
    filterForm->addRow(QStringLiteral("常用词表"), m_commonCount);

    m_strictDictionary = new QCheckBox(QStringLiteral("严格词典校验（过滤 OCR 噪声）"), filterGroup);
    filterForm->addRow(QString(), m_strictDictionary);

    m_autoSnooze = new QCheckBox(QStringLiteral("自动收起反复出现、你从未看过的词"), filterGroup);
    m_autoSnooze->setToolTip(QStringLiteral("被收起的词会标记为“疑似已掌握”，可在生词列表里恢复"));
    filterForm->addRow(QString(), m_autoSnooze);

    auto* snoozeRow = new QWidget(filterGroup);
    auto* snoozeLayout = new QHBoxLayout(snoozeRow);
    snoozeLayout->setContentsMargins(0, 0, 0, 0);
    snoozeLayout->setSpacing(6);
    snoozeLayout->addWidget(new QLabel(QStringLiteral("出现"), snoozeRow));
    m_autoSnoozeScans = new QSpinBox(snoozeRow);
    m_autoSnoozeScans->setRange(2, 20);
    m_autoSnoozeScans->setSuffix(QStringLiteral(" 次"));
    snoozeLayout->addWidget(m_autoSnoozeScans);
    snoozeLayout->addWidget(new QLabel(QStringLiteral("且停留"), snoozeRow));
    m_autoSnoozeSeconds = new QSpinBox(snoozeRow);
    m_autoSnoozeSeconds->setRange(5, 600);
    m_autoSnoozeSeconds->setSuffix(QStringLiteral(" 秒后收起"));
    snoozeLayout->addWidget(m_autoSnoozeSeconds);
    snoozeLayout->addStretch(1);
    filterForm->addRow(QString(), snoozeRow);

    auto* dictNote = new QLabel(
        QStringLiteral("只会出现离线词典里查得到释义的词；查不到、或只有缩写释义的词不会显示。"),
        filterGroup);
    dictNote->setWordWrap(true);
    dictNote->setStyleSheet(QStringLiteral("color:#8A93A6;font-size:11px;"));
    filterForm->addRow(QString(), dictNote);

    root->addWidget(filterGroup);

    // --- appearance --------------------------------------------------------
    auto* uiGroup = new QGroupBox(QStringLiteral("显示"), this);
    auto* uiForm = new QFormLayout(uiGroup);
    uiForm->setSpacing(8);

    m_maxChips = new QSpinBox(uiGroup);
    m_maxChips->setRange(3, 40);
    m_maxChips->setSuffix(QStringLiteral(" 个"));
    uiForm->addRow(QStringLiteral("状态栏最多显示"), m_maxChips);

    m_fontSize = new QSpinBox(uiGroup);
    m_fontSize->setRange(10, 26);
    m_fontSize->setSuffix(QStringLiteral(" px"));
    uiForm->addRow(QStringLiteral("字号"), m_fontSize);

    m_opacity = new QSpinBox(uiGroup);
    m_opacity->setRange(30, 100);
    m_opacity->setSuffix(QStringLiteral(" %"));
    uiForm->addRow(QStringLiteral("不透明度"), m_opacity);

    m_ttl = new QSpinBox(uiGroup);
    m_ttl->setRange(10, 900);
    m_ttl->setSuffix(QStringLiteral(" 秒"));
    uiForm->addRow(QStringLiteral("过期时间"), m_ttl);

    m_keepUntilHandled = new QCheckBox(QStringLiteral("未处理的生词一直保留"), uiGroup);
    m_keepUntilHandled->setToolTip(
        QStringLiteral("勾选后词条不会自动过期，只有“认识 / 忽略 / 清空”才会移出悬浮栏"));
    uiForm->addRow(QString(), m_keepUntilHandled);

    auto* autoHideRow = new QWidget(uiGroup);
    auto* autoHideLayout = new QHBoxLayout(autoHideRow);
    autoHideLayout->setContentsMargins(0, 0, 0, 0);
    autoHideLayout->setSpacing(6);
    m_autoHide = new QCheckBox(QStringLiteral("鼠标离开后自动隐藏"), autoHideRow);
    m_autoHideSeconds = new QSpinBox(autoHideRow);
    m_autoHideSeconds->setRange(4, 120);
    m_autoHideSeconds->setSuffix(QStringLiteral(" 秒"));
    autoHideLayout->addWidget(m_autoHide);
    autoHideLayout->addWidget(m_autoHideSeconds);
    autoHideLayout->addStretch(1);
    uiForm->addRow(QString(), autoHideRow);

    root->addWidget(uiGroup);

    m_hotkeyHint = new QLabel(this);
    m_hotkeyHint->setWordWrap(true);
    m_hotkeyHint->setStyleSheet(QStringLiteral("color:#98A1B4;"));
    m_hotkeyHint->setText(
        QStringLiteral("全局快捷键 Ctrl+Alt+W 显示/隐藏状态栏。拖动状态栏可调整位置。"));
    root->addWidget(m_hotkeyHint);
    root->addStretch(1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    auto* reset = buttons->addButton(QStringLiteral("恢复默认"), QDialogButtonBox::ResetRole);
    root->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(reset, &QPushButton::clicked, this, &SettingsDialog::resetToDefaults);
    connect(browse, &QPushButton::clicked, this, &SettingsDialog::browseForExecutable);
    connect(m_autoHide, &QCheckBox::toggled, m_autoHideSeconds, &QWidget::setEnabled);
    connect(m_keepUntilHandled, &QCheckBox::toggled, this,
            [this](bool checked) { m_ttl->setEnabled(!checked); });
    connect(m_autoSnooze, &QCheckBox::toggled, this, [this](bool checked) {
        m_autoSnoozeScans->setEnabled(checked);
        m_autoSnoozeSeconds->setEnabled(checked);
    });

    applyToWidgets(m_base);
}

void SettingsDialog::setConfig(const AppConfig& cfg)
{
    m_base = cfg;
    applyToWidgets(cfg);
}

void SettingsDialog::applyToWidgets(const AppConfig& cfg)
{
    const QSignalBlocker b1(m_interval);
    m_interval->setValue(cfg.intervalMs);

    int scaleIndex = m_scale->findData(cfg.ocrScalePercent);
    if (scaleIndex < 0)
        scaleIndex = 0;
    m_scale->setCurrentIndex(scaleIndex);

    int captureIndex = m_captureMode->findData(cfg.captureMode);
    if (captureIndex < 0)
        captureIndex = 0;
    m_captureMode->setCurrentIndex(captureIndex);

    int psmIndex = m_psm->findData(cfg.psm);
    if (psmIndex < 0)
        psmIndex = 0;
    m_psm->setCurrentIndex(psmIndex);

    m_confidence->setValue(cfg.minConfidence);
    m_minWordLength->setValue(cfg.minWordLength);
    m_tesseractPath->setText(cfg.tesseractPath);

    m_filterCommon->setChecked(cfg.filterCommon);
    int countIndex = m_commonCount->findData(cfg.commonWordCount);
    if (countIndex < 0)
        countIndex = 2;
    m_commonCount->setCurrentIndex(countIndex);
    m_strictDictionary->setChecked(cfg.strictDictionary);
    m_autoSnooze->setChecked(cfg.autoSnooze);
    m_autoSnoozeScans->setValue(cfg.autoSnoozeScans);
    m_autoSnoozeSeconds->setValue(cfg.autoSnoozeSeconds);
    m_autoSnoozeScans->setEnabled(cfg.autoSnooze);
    m_autoSnoozeSeconds->setEnabled(cfg.autoSnooze);

    m_maxChips->setValue(cfg.maxChips);
    m_fontSize->setValue(cfg.fontSize);
    m_opacity->setValue(cfg.opacityPercent);
    m_ttl->setValue(cfg.activeTtlSeconds);
    m_keepUntilHandled->setChecked(cfg.keepUntilHandled);
    m_ttl->setEnabled(!cfg.keepUntilHandled);
    m_autoHide->setChecked(cfg.autoHide);
    m_autoHideSeconds->setValue(cfg.autoHideSeconds);
    m_autoHideSeconds->setEnabled(cfg.autoHide);
}

AppConfig SettingsDialog::config() const
{
    AppConfig cfg = m_base;
    cfg.intervalMs = m_interval->value();
    cfg.captureMode = m_captureMode->currentData().toInt();
    cfg.ocrScalePercent = m_scale->currentData().toInt();
    cfg.psm = m_psm->currentData().toInt();
    cfg.minConfidence = m_confidence->value();
    cfg.minWordLength = m_minWordLength->value();
    cfg.tesseractPath = m_tesseractPath->text().trimmed();

    cfg.filterCommon = m_filterCommon->isChecked();
    cfg.commonWordCount = m_commonCount->currentData().toInt();
    cfg.strictDictionary = m_strictDictionary->isChecked();
    cfg.autoSnooze = m_autoSnooze->isChecked();
    cfg.autoSnoozeScans = m_autoSnoozeScans->value();
    cfg.autoSnoozeSeconds = m_autoSnoozeSeconds->value();

    cfg.maxChips = m_maxChips->value();
    cfg.fontSize = m_fontSize->value();
    cfg.opacityPercent = m_opacity->value();
    cfg.activeTtlSeconds = m_ttl->value();
    cfg.keepUntilHandled = m_keepUntilHandled->isChecked();
    cfg.autoHide = m_autoHide->isChecked();
    cfg.autoHideSeconds = m_autoHideSeconds->value();
    return cfg;
}

void SettingsDialog::setEngineInfo(const QString& executable, const QString& tessdata)
{
    if (executable.isEmpty()) {
        m_engineState->setText(QStringLiteral("⚠ 未找到 tesseract，请在下方浏览指定 tesseract.exe"));
        m_engineState->setStyleSheet(QStringLiteral("color:#F87171;"));
        return;
    }
    m_engineState->setText(QStringLiteral("✓ 引擎: %1\n语言数据: %2")
                               .arg(executable, tessdata.isEmpty() ? QStringLiteral("默认") : tessdata));
    m_engineState->setStyleSheet(QStringLiteral("color:#4ADE80;"));
}

void SettingsDialog::setHotkeyInfo(bool available)
{
    if (available) {
        m_hotkeyHint->setText(
            QStringLiteral("全局快捷键 Ctrl+Alt+W 显示/隐藏状态栏。拖动状态栏可调整位置。"));
        m_hotkeyHint->setStyleSheet(QStringLiteral("color:#98A1B4;"));
        return;
    }
    m_hotkeyHint->setText(QStringLiteral(
        "⚠ 快捷键 Ctrl+Alt+W 已被其他程序占用，可从托盘菜单显示/隐藏状态栏。"));
    m_hotkeyHint->setStyleSheet(QStringLiteral("color:#FBBF24;"));
}

void SettingsDialog::browseForExecutable()
{
    const QString file = QFileDialog::getOpenFileName(
        this, QStringLiteral("选择 tesseract.exe"), QString(),
        QStringLiteral("可执行文件 (*.exe);;所有文件 (*)"));
    if (file.isEmpty())
        return;
    m_tesseractPath->setText(QDir::toNativeSeparators(file));
}

void SettingsDialog::resetToDefaults()
{
    AppConfig defaults;
    defaults.tesseractPath = m_tesseractPath->text().trimmed();
    defaults.barPos = m_base.barPos;
    defaults.barPosSet = m_base.barPosSet;
    defaults.knownWords = m_base.knownWords;
    defaults.blockedWords = m_base.blockedWords;
    m_base = defaults;
    applyToWidgets(m_base);
}
