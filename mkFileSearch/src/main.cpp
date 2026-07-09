#include "mainwindow.h"
#include "settingsmanager.h"

#include <iostream>
#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDir>
#include <QFont>
#include <QString>
#include <QTranslator>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName("mkFileSearch");
    QCoreApplication::setApplicationVersion("1.0");

    QCommandLineParser parser;
    parser.setApplicationDescription("Qt6 based file search tool");
    auto helpOption = parser.addHelpOption();
    auto versionOption = parser.addVersionOption();
    parser.addPositionalArgument("[searchpath]", QCoreApplication::translate("main", "Path to search"));

    if (!parser.parse(QCoreApplication::arguments())) {
        std::cerr << qPrintable(parser.errorText()) << std::endl;
        return 1;
    }

    if (parser.isSet(helpOption)) {
        std::cout << qPrintable(parser.helpText()) << std::endl;
        return 0;
    }

    if (parser.isSet(versionOption)) {
        std::cout << qPrintable(QCoreApplication::applicationName() + " " + QCoreApplication::applicationVersion()) << std::endl;
        return 0;
    }

    QString pathToScan;
    if (!parser.positionalArguments().isEmpty()) {
        pathToScan = parser.positionalArguments().at(0);
        // Falls der Windows-Parser den Backslash geschluckt und ein " an den String gehängt hat:
        if (pathToScan.endsWith('"')) {
            pathToScan.chop(1);  // Das falsche Anführungszeichen abschneiden
            pathToScan += "/";   // Einen sauberen Ordner-Abschluss hinzufügen
        }
        pathToScan = QDir::cleanPath(pathToScan);
    } else {
        pathToScan = QDir::homePath();
    }

    if (!QDir(pathToScan).exists()) {
        std::cerr << "Error: Path not found." << std::endl;
        return 1;
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

    MainWindow w(pathToScan);
    w.show();
    return QCoreApplication::exec();
}
