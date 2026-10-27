#include "Dictionary.h"
#include "Settings.h"
#include "TesseractOcr.h"
#include "WordFilter.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDir>
#include <QGuiApplication>
#include <QImage>
#include <QPixmap>
#include <QScreen>
#include <QTextStream>

#ifdef Q_OS_WIN
#  include <windows.h>
#endif

namespace {

bool wantsCapture(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg == QLatin1String("--capture") || arg == QLatin1String("-c"))
            return true;
    }
    return false;
}

} // namespace

int main(int argc, char** argv)
{
    const bool captureMode = wantsCapture(argc, argv);
    if (!captureMode)
        qputenv("QT_QPA_PLATFORM", "offscreen");

    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("WordLens"));
    QCoreApplication::setApplicationName(QStringLiteral("wlocr"));

#ifdef Q_OS_WIN
    SetConsoleOutputCP(CP_UTF8);
#endif

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("WordLens OCR helper: capture the screen or analyse an image."));
    parser.addHelpOption();
    parser.addPositionalArgument(QStringLiteral("image"),
                                 QStringLiteral("Screenshot to analyse (png/bmp/...)."));

    QCommandLineOption captureOption({QStringLiteral("c"), QStringLiteral("capture")},
                                     QStringLiteral("Capture the primary screen to <file>."),
                                     QStringLiteral("file"));
    QCommandLineOption psmOption(QStringLiteral("psm"),
                                 QStringLiteral("Tesseract page segmentation mode."),
                                 QStringLiteral("mode"), QStringLiteral("11"));
    QCommandLineOption scaleOption(QStringLiteral("scale"),
                                   QStringLiteral("Upscale percent before OCR."),
                                   QStringLiteral("percent"), QStringLiteral("100"));
    QCommandLineOption allOption(QStringLiteral("all"),
                                 QStringLiteral("Print every OCR token."));
    QCommandLineOption minConfOption(QStringLiteral("min-conf"),
                                     QStringLiteral("Drop tokens below this confidence."),
                                     QStringLiteral("conf"), QStringLiteral("0"));
    QCommandLineOption defineOption({QStringLiteral("d"), QStringLiteral("define")},
                                    QStringLiteral("Look up words in the offline dictionary."),
                                    QStringLiteral("word"));
    parser.addOption(captureOption);
    parser.addOption(psmOption);
    parser.addOption(scaleOption);
    parser.addOption(allOption);
    parser.addOption(minConfOption);
    parser.addOption(defineOption);
    parser.process(app);

    QTextStream out(stdout);
    out.setEncoding(QStringConverter::Utf8);

    if (parser.isSet(defineOption)) {
        Dictionary dictionary;
        QString error;
        const QString path = QDir(appDataDir()).filePath(QStringLiteral("dict.bin"));
        if (!dictionary.load(path, &error)) {
            out << "dictionary failed: " << error << "\n";
            return 2;
        }
        out << "dictionary: " << path << " (" << dictionary.size() << " entries)\n";

        QStringList words = parser.values(defineOption);
        words += parser.positionalArguments();
        for (const QString& word : words) {
            const WordDefinition definition = dictionary.lookup(word);
            if (!definition.isValid()) {
                out << word << ": (not found)\n";
                continue;
            }
            out << definition.word;
            if (!definition.phonetic.isEmpty())
                out << " [" << definition.phonetic << "]";
            out << "\n";
            for (const QString& sense : definition.senses)
                out << "    " << sense << "\n";
        }
        return 0;
    }

    if (parser.isSet(captureOption)) {
        QScreen* screen = QGuiApplication::primaryScreen();
        if (!screen) {
            out << "no screen available\n";
            return 2;
        }
        const QPixmap shot = screen->grabWindow(0);
        const QString path = parser.value(captureOption);
        if (shot.isNull() || !shot.save(path)) {
            out << "capture failed\n";
            return 2;
        }
        out << "saved " << path << " (" << shot.width() << "x" << shot.height() << ")\n";
        return 0;
    }

    const QStringList files = parser.positionalArguments();
    if (files.isEmpty()) {
        out << "usage: wlocr [--all] [--scale 150] <image>\n";
        return 2;
    }

    AppConfig cfg = loadConfig();
    cfg.psm = parser.value(psmOption).toInt();
    cfg.ocrScalePercent = parser.value(scaleOption).toInt();
    cfg.minConfidence = parser.value(minConfOption).toInt();

    WordFilter filter;
    filter.setConfig(cfg);
    QString dictionaryError;
    const bool dictionaryOk = filter.load(appDataDir(), &dictionaryError);
    filter.setConfig(cfg);

    out << "engine: " << TesseractOcr::autoDetectExecutable() << "\n";
    out << "data: " << appDataDir() << "\n";
    out << "dictionary: "
        << (dictionaryOk ? QStringLiteral("ok") : dictionaryError)
        << " (" << filter.dictionarySize() << " words)\n";

    const QImage image(files.first());
    if (image.isNull()) {
        out << "cannot load image: " << files.first() << "\n";
        return 2;
    }

    TesseractOcr ocr(cfg.tesseractPath);
    if (!ocr.available()) {
        out << "tesseract not found\n";
        return 2;
    }

    const OcrResult result = ocr.recognize(image, cfg);
    if (!result.ok) {
        out << "ocr failed: " << result.error << "\n";
        return 3;
    }

    out << "image: " << result.imageSize.width() << "x" << result.imageSize.height()
        << "  scale=" << cfg.ocrScalePercent << "%  psm=" << cfg.psm << "\n";
    out << "elapsed: " << QString::number(result.elapsedMs / 1000.0, 'f', 2) << "s\n";
    out << "tokens: " << result.words.size() << "\n";

    int valid = 0;
    int interesting = 0;
    QStringList uniqueInteresting;
    out << "--- words ---\n";
    for (const WordHit& hit : result.words) {
        if (parser.isSet(allOption)) {
            out << QStringLiteral("[%1] (%2,%3 %4x%5) ")
                       .arg(hit.confidence, 0, 'f', 0)
                       .arg(hit.box.x()).arg(hit.box.y())
                       .arg(hit.box.width()).arg(hit.box.height())
                << hit.text << "\n";
        }
        if (hit.confidence < cfg.minConfidence)
            continue;
        const QString word = filter.normalize(hit.text);
        if (word.isEmpty())
            continue;
        ++valid;
        if (!filter.isInteresting(word))
            continue;
        ++interesting;
        if (!uniqueInteresting.contains(word))
            uniqueInteresting << word;
    }

    out << "plausible english words: " << valid << "\n";
    out << "new words: " << interesting << " (unique " << uniqueInteresting.size() << ")\n";
    for (const QString& word : uniqueInteresting)
        out << "  * " << word << "\n";
    return 0;
}
