#include "mainwindow.h"
#include "settingsmanager.h"

#include <iostream>
#include <QApplication>
#include <QDir>
#include <QFont>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMainWindow>
#include <QSharedMemory>
#include <QString>
#include <QTranslator>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QCoreApplication::setApplicationName("mkBatchRename");
    QCoreApplication::setApplicationVersion("1.0");

    QString targetDir;
    QStringList pathList;

    if (argc == 1) {
        targetDir = QDir::homePath();
    } else if (argc == 2) {
            targetDir = argv[1];

            // Falls der Windows-Parser den Backslash geschluckt und ein " an den String gehängt hat:
            if (targetDir.endsWith('"')) {
                targetDir.chop(1);  // Das falsche Anführungszeichen abschneiden
                targetDir += "/";   // Einen sauberen Ordner-Abschluss hinzufügen
            }
            targetDir = QDir::cleanPath(targetDir);

            if (!QDir(targetDir).exists()) {
                std::cerr << "Error: Path not found." << std::endl;
                return 1;
            }
    } else if (argc == 3) {
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
            qDebug() << "[mkBatchRename] Error while accessing shared memory:" << sharedMemory.errorString();
            return -2;
        }

        QJsonParseError error;
        QJsonDocument doc = QJsonDocument::fromJson(jsonData, &error);
        if (doc.isNull()) {
            qDebug() << "[mkBatchRename] JSON-Error:" << error.errorString();
            return -3;
        }

        QJsonObject jsonObj = doc.object();
        targetDir = jsonObj["targetDir"].toString();

        QJsonArray pathArray = jsonObj["pathList"].toArray();
        for (const QJsonValue &value : std::as_const(pathArray)) {
            pathList.append(value.toString());
        }
    } else {
        qDebug() << "[mkBatchRename] Unsupported number of command line arguments!";
        return -1;
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

    //currentFont = QApplication::font();
    //qDebug() << "active font:" << currentFont.family() << currentFont.pointSize() << "pt";

    //-----------------------------------------------------------------------------------------

    QTranslator translator;
    if (translator.load(QLocale::system(), "mktools", "_", ":/i18n")) {
        app.installTranslator(&translator);
    }

    //-----------------------------------------------------------------------------------------

    MainWindow w(targetDir, pathList);
    w.show();
    return QCoreApplication::exec();
}
