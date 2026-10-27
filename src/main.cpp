#include "AppController.h"

#include <QApplication>
#include <QIcon>
#include <QLinearGradient>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QSharedMemory>

namespace {

QIcon makeAppIcon()
{
    QPixmap pixmap(64, 64);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QLinearGradient gradient(0, 0, 64, 64);
    gradient.setColorAt(0.0, QColor(74, 110, 245));
    gradient.setColorAt(1.0, QColor(122, 162, 247));
    painter.setPen(Qt::NoPen);
    painter.setBrush(gradient);
    painter.drawRoundedRect(QRectF(2, 2, 60, 60), 16, 16);

    painter.setPen(QColor(255, 255, 255));
    QFont font(QStringLiteral("Segoe UI"));
    font.setPixelSize(36);
    font.setBold(true);
    painter.setFont(font);
    painter.drawText(QRectF(0, -2, 64, 64), Qt::AlignCenter, QStringLiteral("W"));
    painter.end();
    return QIcon(pixmap);
}

void applyDarkPalette()
{
    QApplication::setStyle(QStringLiteral("Fusion"));

    QPalette palette;
    palette.setColor(QPalette::Window, QColor(24, 26, 32));
    palette.setColor(QPalette::WindowText, QColor(232, 236, 244));
    palette.setColor(QPalette::Base, QColor(18, 20, 25));
    palette.setColor(QPalette::AlternateBase, QColor(28, 31, 38));
    palette.setColor(QPalette::ToolTipBase, QColor(30, 33, 41));
    palette.setColor(QPalette::ToolTipText, QColor(232, 236, 244));
    palette.setColor(QPalette::Text, QColor(232, 236, 244));
    palette.setColor(QPalette::Button, QColor(38, 42, 52));
    palette.setColor(QPalette::ButtonText, QColor(232, 236, 244));
    palette.setColor(QPalette::Highlight, QColor(122, 162, 247));
    palette.setColor(QPalette::HighlightedText, QColor(16, 18, 22));
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor(120, 126, 140));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(120, 126, 140));
    QApplication::setPalette(palette);
}

} // namespace

int main(int argc, char* argv[])
{
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    QApplication app(argc, argv);
    QApplication::setQuitOnLastWindowClosed(false);
    QCoreApplication::setOrganizationName(QStringLiteral("WordLens"));
    QCoreApplication::setApplicationName(QStringLiteral("WordLens"));
    QApplication::setApplicationDisplayName(QStringLiteral("WordLens"));
    QApplication::setWindowIcon(makeAppIcon());
    applyDarkPalette();

    QSharedMemory guard(QStringLiteral("WordLens.SingleInstance.v1"));
    if (!guard.create(1)) {
        QMessageBox::information(
            nullptr, QStringLiteral("WordLens"),
            QStringLiteral("WordLens 已经在运行。\n\n按 Ctrl+Alt+W 或点击托盘图标可显示状态栏。"));
        return 0;
    }

    AppController controller;
    controller.start();
    return app.exec();
}
