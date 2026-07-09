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

    if (argc < 2) {
        qDebug() << "[mkTransactionHandler] Error: No memory key received!";
        return -1;
    }

    QString memoryKey = argv[1];
    int expectedSize = QString(argv[2]).toInt();
    QByteArray jsonData;

    QSharedMemory sharedMemory(memoryKey);
    if (sharedMemory.attach()) {
        sharedMemory.lock();
        
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
