/// Renders the hover card offscreen so its layout can be inspected without a
/// desktop session (used while tuning the UI).
#include "DefinitionCard.h"
#include "Dictionary.h"
#include "Settings.h"

#include <QApplication>
#include <QDir>
#include <QImage>
#include <QPainter>
#include <QPixmap>

int main(int argc, char** argv)
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    Dictionary dictionary;
    QString error;
    const QString path = QDir(appDataDir()).filePath(QStringLiteral("dict.bin"));
    if (!dictionary.load(path, &error)) {
        qCritical("cannot load %s: %s", qPrintable(path), qPrintable(error));
        return 2;
    }

    const QString word = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("debug");
    const QString out = argc > 2 ? QString::fromLocal8Bit(argv[2]) : QStringLiteral("card.png");

    const WordDefinition definition = dictionary.lookup(word);
    if (!definition.isValid()) {
        qCritical("no definition for %s", qPrintable(word));
        return 2;
    }

    DefinitionCard card;
    card.showFor(definition, QRect(400, 60, 80, 30));
    const QPixmap shot = card.grab();

    QImage image(shot.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(QColor(74, 92, 112));
    QPainter painter(&image);
    painter.drawPixmap(0, 0, shot);
    painter.end();

    if (!image.save(out)) {
        qCritical("cannot save %s", qPrintable(out));
        return 2;
    }
    qInfo("saved %s (%dx%d)", qPrintable(out), image.width(), image.height());
    return 0;
}
