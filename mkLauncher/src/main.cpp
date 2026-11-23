#include "mainwindow.h"

#include <iostream>
#include <QApplication>
#include <QCommandLineParser>
#include <QFont>
#include <QString>
#include <QTranslator>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName("mkLauncher");
    QCoreApplication::setApplicationVersion("1.0");

    QCommandLineParser parser;
    parser.setApplicationDescription("Qt6 based apps and file launcher tool");
    auto helpOption = parser.addHelpOption();
    auto versionOption = parser.addVersionOption();

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

    //-----------------------------------------------------------------------------------------

    QTranslator translator;
    if (translator.load(QLocale::system(), "mktools", "_", ":/i18n")) {
        app.installTranslator(&translator);
    }

    //-----------------------------------------------------------------------------------------

    MainWindow w;

    w.adjustSize();
    w.resize(w.width(), w.layout()->minimumSize().height());

    w.show();
    return QCoreApplication::exec();
}
