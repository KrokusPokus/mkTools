#include "mainwindow.h"
#include "fileoperation.h"
#include "settingsmanager.h"

#include <QApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMainWindow>
#include <QSharedMemory>
#include <QTranslator>
#include <QUrl>

#include <KIO/CopyJob>
#include <KIO/DeleteJob>
#include <KIO/JobUiDelegateFactory>
#include <QTimer>

// Leaves the whole operation to KIO (setting "mkTransactionHandler/UseKdeFileOperations"):
// conflict dialogs, error messages and the progress display in the Plasma notification area then come from KDE.
// The job runs in the GUI thread, since KIO's dialogs are widgets (KIO jobs are asynchronous anyway).
static int runKdeFileOperation(QApplication &app, OperationType opType, const QList<QUrl> &urls, const QString &targetDir) {
    // Without desktopFileName, KUiServerV2JobTracker silently discards the job -> no display in Plasma
    QGuiApplication::setDesktopFileName(QStringLiteral("mkTransactionHandler"));

    const QUrl destUrl = QUrl::fromLocalFile(targetDir);
    KJob *job = nullptr;

    switch (opType) {
    case OperationType::Copy:    job = KIO::copy(urls, destUrl); break;
    case OperationType::Move:    job = KIO::move(urls, destUrl); break;
    case OperationType::Link:    job = KIO::link(urls, destUrl); break;
    case OperationType::Recycle: job = KIO::trash(urls); break; // Confirmation already happened in mkFolderWidget
    case OperationType::Delete:  job = KIO::del(urls); break;   // dto.
    }

    if (!job) {
        qDebug() << "[mkTransactionHandler] Error: Unsupported operation type for KIO:" << static_cast<int>(opType);
        return -4;
    }

    // Closing a conflict dialog must not quit the program while the job is still running
    app.setQuitOnLastWindowClosed(false);
    job->setUiDelegate(KIO::createDefaultJobUiDelegate(KJobUiDelegate::AutoHandlingEnabled, nullptr));

    int exitCode = 0;
    QObject::connect(job, &KJob::result, &app, [&app, &exitCode](KJob *finishedJob) {
        exitCode = (finishedJob->error() && finishedJob->error() != KJob::KilledJobError) ? 1 : 0;

        // KIO shows error messages delayed (queued) in its own dialogs.
        // Only quit when no window is visible anymore, otherwise they would disappear unread.
        auto *waitForDialogs = new QTimer(&app);
        QObject::connect(waitForDialogs, &QTimer::timeout, &app, [&app, waitForDialogs]() {
            const QWidgetList widgets = QApplication::topLevelWidgets();
            for (const QWidget *widget : widgets) {
                if (widget->isVisible()) return;
            }
            waitForDialogs->stop();
            app.quit();
        });
        waitForDialogs->start(200);
    });

    app.exec();
    return exitCode;
}

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    if (argc < 3) {
        qDebug() << "[mkTransactionHandler] Error: No memory key received!";
        return -1;
    }

    QString memoryKey = argv[1];
    bool sizeOk = false;
    const qsizetype expectedSize = QString(argv[2]).toLongLong(&sizeOk);
    if (!sizeOk || expectedSize <= 0) {
        qDebug() << "[mkTransactionHandler] Error: Invalid data size argument:" << argv[2];
        return -2;
    }
    QByteArray jsonData;

    QSharedMemory sharedMemory(memoryKey);
    if (sharedMemory.attach(QSharedMemory::ReadOnly)) {
        // Never read beyond the segment: the size from argv is not trustworthy
        if (expectedSize > sharedMemory.size()) {
            qDebug() << "[mkTransactionHandler] Error: Data size" << expectedSize << "exceeds shared memory size" << sharedMemory.size();
            sharedMemory.detach();
            return -2;
        }

        if (!sharedMemory.lock()) {
            qDebug() << "[mkTransactionHandler] Error while locking shared memory:" << sharedMemory.errorString();
            sharedMemory.detach();
            return -2;
        }

        jsonData = QByteArray(static_cast<const char*>(sharedMemory.constData()), expectedSize);

        sharedMemory.unlock();
        sharedMemory.detach();
    } else {
        qDebug() << "[mkTransactionHandler] Error while accessing shared memory:" << sharedMemory.errorString();
        return -2;
    }

    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(jsonData, &error);
    if (doc.isNull()) {
        qDebug() << "[mkTransactionHandler] JSON-Error:" << error.errorString();
        return -3;
    }

    QJsonObject jsonObj = doc.object();
    QString targetDir = jsonObj["targetDir"].toString();
    OperationType opType = static_cast<OperationType>(jsonObj["opType"].toInt());
    
    QList<QUrl> urls;
    QJsonArray urlArray = jsonObj["urls"].toArray();
    for (const QJsonValue &value : std::as_const(urlArray)) {
        urls.append(QUrl(value.toString()));
    }

    //-----------------------------------------------------------------------------------------
    // If neccessary, override default application font

    SettingsManager m_settings;

    QFont currentFont = QApplication::font();

    int targetFontSize = currentFont.pointSize();

    if ((m_settings.fontSizeOverride > 0) && (m_settings.fontSizeOverride < 100) && (targetFontSize != m_settings.fontSizeOverride)) {
        targetFontSize = m_settings.fontSizeOverride;
    }

    if (!m_settings.fontNameOverride.isEmpty() && (QString::compare(currentFont.family(), m_settings.fontNameOverride, Qt::CaseInsensitive) != 0)) {
        QFont globalFont(m_settings.fontNameOverride, targetFontSize);
        QApplication::setFont(globalFont);
    } else if (targetFontSize != currentFont.pointSize()) {
        currentFont.setPointSize(targetFontSize);
        QApplication::setFont(currentFont);
    }

    //-----------------------------------------------------------------------------------------

    QTranslator translator;
    if (translator.load(QLocale::system(), "mktools", "_", ":/i18n")) {
        app.installTranslator(&translator);
    }

    //-----------------------------------------------------------------------------------------

    if (m_settings.useKdeFileOperations) {
        return runKdeFileOperation(app, opType, urls, targetDir);
    }

    MainWindow progressDialog(opType, urls, targetDir);

    return app.exec();
}
