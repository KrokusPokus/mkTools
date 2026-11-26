#include "mainwindow.h"

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
        pathToScan = QDir::cleanPath(pathToScan);
    } else {
        pathToScan = QDir::homePath();
    }

    if (!QDir(pathToScan).exists()) {
        std::cerr << "Error: Path not found." << std::endl;
        return 1;
    }

    //-----------------------------------------------------------------------------------------

    QTranslator translator;
    if (translator.load(QLocale::system(), "mkfiletools", "_", ":/i18n")) {
        app.installTranslator(&translator);
    }

    //-----------------------------------------------------------------------------------------

    MainWindow w(pathToScan);
    w.show();
    return QCoreApplication::exec();
}
