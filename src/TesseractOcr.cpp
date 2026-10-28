#include "TesseractOcr.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QTemporaryFile>

namespace {

QStringList executableCandidates()
{
    QStringList list;
    list << QCoreApplication::applicationDirPath() + QStringLiteral("/tools/tesseract/tesseract.exe");
#ifdef WORLDLENS_SOURCE_DIR
    list << QStringLiteral(WORLDLENS_SOURCE_DIR) + QStringLiteral("/tools/tesseract/tesseract.exe");
#endif
    const QString onPath = QStandardPaths::findExecutable(QStringLiteral("tesseract"));
    if (!onPath.isEmpty())
        list << onPath;
    list << QStringLiteral("D:/msys64/ucrt64/bin/tesseract.exe");
    list << QStringLiteral("D:/msys64/mingw64/bin/tesseract.exe");
    list << QStringLiteral("C:/Program Files/Tesseract-OCR/tesseract.exe");
    return list;
}

/// Otsu's method: pick the threshold that best separates dark glyphs from a
/// light background by maximising between-class variance. Gives a per-image
/// threshold that adapts to the current theme/brightness instead of a hardcoded
/// value.
int otsuThreshold(const QImage& img)
{
    int hist[256] = {0};
    const int w = img.width();
    const int h = img.height();
    for (int y = 0; y < h; ++y) {
        const uchar* line = img.constScanLine(y);
        for (int x = 0; x < w; ++x)
            ++hist[line[x]];
    }
    const int total = w * h;
    if (total <= 0)
        return 128;

    double sum = 0.0;
    for (int i = 0; i < 256; ++i)
        sum += double(i) * hist[i];

    double sumB = 0.0;
    int wB = 0;
    double maxVar = -1.0;
    int threshold = 128;
    for (int i = 0; i < 256; ++i) {
        wB += hist[i];
        if (wB == 0)
            continue;
        const int wF = total - wB;
        if (wF == 0)
            break;
        sumB += double(i) * hist[i];
        const double mB = sumB / wB;
        const double mF = (sum - sumB) / wF;
        const double variance = double(wB) * wF * (mB - mF) * (mB - mF);
        if (variance > maxVar) {
            maxVar = variance;
            threshold = i;
        }
    }
    return threshold;
}

/// Collapse the grayscale image to crisp black text on white — what Tesseract
/// recognises best (it struggles with the soft grey ramps of anti-aliased
/// screen fonts).
void binarize(QImage& img, int threshold)
{
    const int w = img.width();
    const int h = img.height();
    for (int y = 0; y < h; ++y) {
        uchar* line = img.scanLine(y);
        for (int x = 0; x < w; ++x)
            line[x] = line[x] < threshold ? 0 : 255;
    }
}

/// Gray scale + optional upscaling. Tesseract is noticeably better when the
/// input is roughly 30+ pixel tall text, so upscaling helps small UI fonts.
/// Dark backgrounds are inverted, so the tesseract inverter can stay disabled
/// (that saves a full extra pass over the image).
QImage prepareImage(const QImage& source, int scalePercent)
{
    if (source.isNull())
        return {};

    QImage img = source.convertToFormat(QImage::Format_Grayscale8);
    const int percent = qBound(100, scalePercent, 300);
    if (percent != 100) {
        const QSize target(qMax(1, img.width() * percent / 100),
                           qMax(1, img.height() * percent / 100));
        // Guard against absurd memory usage on multi monitor / high dpi setups.
        if (qint64(target.width()) * target.height() <= 24LL * 1024 * 1024)
            // Nearest-neighbour keeps glyph edges sharp; smooth interpolation
            // smears anti-aliased text even further and hurts recognition.
            img = img.scaled(target, Qt::IgnoreAspectRatio, Qt::FastTransformation);
    }

    // Mean luminance decides whether the UI uses a dark theme.
    const int stepX = qMax(1, img.width() / 160);
    const int stepY = qMax(1, img.height() / 90);
    quint64 sum = 0;
    quint64 count = 0;
    for (int y = 0; y < img.height(); y += stepY) {
        const uchar* line = img.constScanLine(y);
        for (int x = 0; x < img.width(); x += stepX) {
            sum += line[x];
            ++count;
        }
    }
    if (count > 0 && (sum / count) < 110)
        img.invertPixels(QImage::InvertRgb);

    // Binarise to crisp black-on-white. This is the single biggest accuracy win
    // for anti-aliased screen text.
    binarize(img, otsuThreshold(img));

    return img;
}

} // namespace

TesseractOcr::TesseractOcr(const QString& configuredPath)
    : m_configured(configuredPath)
{
    resolve();
}

void TesseractOcr::setConfiguredPath(const QString& path)
{
    m_configured = path;
    resolve();
}

QString TesseractOcr::autoDetectExecutable()
{
    const QStringList candidates = executableCandidates();
    for (const QString& candidate : candidates) {
        if (QFileInfo(candidate).isFile())
            return QDir::cleanPath(candidate);
    }
    return {};
}

void TesseractOcr::resolve()
{
    QString path = m_configured.trimmed();
    if (path.isEmpty()) {
        path = autoDetectExecutable();
    } else if (QFileInfo(path).isDir()) {
        path = QDir(path).filePath(QStringLiteral("tesseract.exe"));
    }

    if (!QFileInfo(path).isFile()) {
        m_exe.clear();
        m_tessdata.clear();
        m_lastError = QStringLiteral("未找到 tesseract 可执行文件");
        return;
    }

    m_exe = QDir::cleanPath(QFileInfo(path).absoluteFilePath());

    const QDir binDir = QFileInfo(m_exe).absoluteDir();
    const QStringList tessdataCandidates = {
        binDir.filePath(QStringLiteral("tessdata")),
        binDir.filePath(QStringLiteral("../share/tessdata")),
        binDir.filePath(QStringLiteral("../../share/tessdata")),
        QStringLiteral("D:/msys64/ucrt64/share/tessdata"),
        QStringLiteral("C:/Program Files/Tesseract-OCR/tessdata"),
    };
    m_tessdata.clear();
    for (const QString& dir : tessdataCandidates) {
        const QString absolute = QDir::cleanPath(QDir(dir).absolutePath());
        if (QFileInfo::exists(absolute + QStringLiteral("/eng.traineddata"))) {
            m_tessdata = absolute;
            break;
        }
    }
    m_lastError.clear();
}

OcrResult TesseractOcr::recognize(const QImage& image, const AppConfig& cfg)
{
    OcrResult result;
    result.imageSize = image.size();

    QElapsedTimer timer;
    timer.start();

    if (!available()) {
        result.error = m_lastError.isEmpty() ? QStringLiteral("未找到 tesseract") : m_lastError;
        return result;
    }
    if (image.isNull()) {
        result.error = QStringLiteral("屏幕截图为空");
        return result;
    }

    if (!m_tessdata.isEmpty()) {
        const QString languageFile =
            m_tessdata + QLatin1Char('/') + cfg.language + QStringLiteral(".traineddata");
        if (!QFileInfo::exists(languageFile)) {
            result.error = QStringLiteral("缺少语言数据: %1").arg(cfg.language);
            return result;
        }
    }

    const QImage prepared = prepareImage(image, cfg.ocrScalePercent);
    if (prepared.isNull()) {
        result.error = QStringLiteral("图像预处理失败");
        return result;
    }

    QTemporaryFile tempFile(QDir::tempPath() + QStringLiteral("/wordlens_XXXXXX.bmp"));
    tempFile.setAutoRemove(true);
    if (!tempFile.open()) {
        result.error = QStringLiteral("无法创建临时文件");
        return result;
    }
    const QString imagePath = tempFile.fileName();
    if (!prepared.save(imagePath, "BMP")) {
        result.error = QStringLiteral("无法写入临时图像");
        return result;
    }
    tempFile.close();

    QStringList args;
    args << imagePath                            // input
         << QStringLiteral("stdout")             // output to stdout
         << QStringLiteral("-l") << cfg.language
         << QStringLiteral("--psm") << QString::number(cfg.psm)
         << QStringLiteral("--oem") << QStringLiteral("1")
         // We only ever care about English words (and apostrophes), so pin the
         // character set — this stops Tesseract from hallucinating digits and
         // punctuation for ambiguous glyphs.
         << QStringLiteral("-c")
         << QStringLiteral("tessedit_char_whitelist=ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz'")
         // Page layout analysis for tables is expensive and useless for a
         // screen grab; disabling it is a big speed win on large monitors.
         << QStringLiteral("-c") << QStringLiteral("textord_tabfind_find_tables=0")
         << QStringLiteral("-c") << QStringLiteral("textord_tablefind_recognize_tables=0")
         // We normalise the polarity ourselves in prepareImage().
         << QStringLiteral("-c") << QStringLiteral("tessedit_do_invert=0");
    if (!m_tessdata.isEmpty())
        args << QStringLiteral("--tessdata-dir") << m_tessdata;
    args << QStringLiteral("tsv");

    QProcess process;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    // OpenMP makes tesseract *slower* on this kind of input (measured 3x on a
    // 4K screen shot), so pin it to one thread.
    env.insert(QStringLiteral("OMP_THREAD_LIMIT"), QStringLiteral("1"));
    env.insert(QStringLiteral("OMP_NUM_THREADS"), QStringLiteral("1"));
    process.setProcessEnvironment(env);
    process.setProgram(m_exe);
    process.setArguments(args);
    process.start();

    if (!process.waitForStarted(5000)) {
        result.error = QStringLiteral("无法启动 tesseract: %1").arg(process.errorString());
        return result;
    }

    // Generous timeout: a dense 4K screen needs roughly 10 s on a laptop CPU.
    const int timeoutMs =
        qBound(20000, 20000 + int(qint64(prepared.width()) * prepared.height() / 1500), 120000);
    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished(1000);
        result.error = QStringLiteral("识别超时（%1 秒）").arg(timeoutMs / 1000);
        return result;
    }

    const QByteArray out = process.readAllStandardOutput();
    const QByteArray err = process.readAllStandardError();
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        result.error = QString::fromUtf8(err).trimmed();
        if (result.error.isEmpty())
            result.error = QStringLiteral("tesseract 退出码 %1").arg(process.exitCode());
        return result;
    }

    // level page block par line word left top width height conf text
    const QList<QByteArray> lines = out.split('\n');
    result.words.reserve(lines.size() / 4);
    for (int i = 1; i < lines.size(); ++i) {
        const QByteArray& line = lines.at(i);
        if (line.isEmpty())
            continue;
        const QList<QByteArray> fields = line.split('\t');
        if (fields.size() < 12)
            continue;
        if (fields.at(0).trimmed() != "5")
            continue;

        bool confOk = false;
        const double confidence = fields.at(10).toDouble(&confOk);
        if (!confOk || confidence < 0)
            continue;

        WordHit hit;
        hit.text = QString::fromUtf8(fields.at(11)).trimmed();
        hit.confidence = confidence;
        hit.box = QRect(fields.at(6).toInt(), fields.at(7).toInt(),
                        fields.at(8).toInt(), fields.at(9).toInt());
        if (hit.text.isEmpty())
            continue;
        result.words.append(hit);
    }

    result.elapsedMs = int(timer.elapsed());
    result.ok = true;
    return result;
}
