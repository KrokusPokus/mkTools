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

#if defined(Q_OS_LINUX)
#include <KIO/CopyJob>
#include <KIO/DeleteJob>
#include <KIO/JobUiDelegateFactory>
#include <QTimer>

// Überlässt die gesamte Operation KIO (Setting "mkTransactionHandler/UseKdeFileOperations"):
// Konfliktdialoge, Fehlermeldungen und die Fortschrittsanzeige im Plasma-Benachrichtigungsbereich kommen dann von KDE.
// Der Job läuft im GUI-Thread, da KIOs Dialoge Widgets sind (KIO-Jobs sind ohnehin asynchron).
static int runKdeFileOperation(QApplication &app, OperationType opType, const QList<QUrl> &urls, const QString &targetDir) {
    // Ohne desktopFileName verwirft KUiServerV2JobTracker den Job stillschweigend -> keine Anzeige in Plasma
    QGuiApplication::setDesktopFileName(QStringLiteral("mkTransactionHandler"));

    const QUrl destUrl = QUrl::fromLocalFile(targetDir);
    KJob *job = nullptr;

    switch (opType) {
    case OperationType::Copy:    job = KIO::copy(urls, destUrl); break;
    case OperationType::Move:    job = KIO::move(urls, destUrl); break;
    case OperationType::Link:    job = KIO::link(urls, destUrl); break;
    case OperationType::Recycle: job = KIO::trash(urls); break; // Bestätigung erfolgte bereits in mkFolderWidget
    case OperationType::Delete:  job = KIO::del(urls); break;   // dto.
    }

    if (!job) {
        qDebug() << "[mkTransactionHandler] Error: Unsupported operation type for KIO:" << static_cast<int>(opType);
        return -4;
    }

    // Schließen eines Konfliktdialogs darf das Programm nicht beenden, solange der Job noch läuft
    app.setQuitOnLastWindowClosed(false);
    job->setUiDelegate(KIO::createDefaultJobUiDelegate(KJobUiDelegate::AutoHandlingEnabled, nullptr));

    int exitCode = 0;
    QObject::connect(job, &KJob::result, &app, [&app, &exitCode](KJob *finishedJob) {
        exitCode = (finishedJob->error() && finishedJob->error() != KJob::KilledJobError) ? 1 : 0;

        // KIO zeigt Fehlermeldungen zeitversetzt (queued) in eigenen Dialogen an.
        // Erst beenden, wenn kein Fenster mehr sichtbar ist, sonst verschwänden sie ungelesen.
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
#endif

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
        // Niemals über das Segment hinaus lesen: Die Größe aus argv ist nicht vertrauenswürdig
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
    //qDebug() << "original font:" << currentFont.family() << currentFont.pointSize() << "pt";

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

#if defined(Q_OS_LINUX)
    if (m_settings.useKdeFileOperations) {
        return runKdeFileOperation(app, opType, urls, targetDir);
    }
#endif

    MainWindow progressDialog(opType, urls, targetDir);

    return app.exec();
}
