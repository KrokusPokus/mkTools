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

    MainWindow progressDialog(opType, urls, targetDir);

    return app.exec();
}
