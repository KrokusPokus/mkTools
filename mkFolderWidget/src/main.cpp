#include "mainwindow.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDir>
#include <QFont>
#include <QProcess>
#include <QStandardPaths>
#include <QString>
#include <QTranslator>
#include <QWindow>

#include <iostream>

bool delegateNonLocalToDolphin(const QString &inputArg) {
    if (inputArg.isEmpty()) return false;

    // Parse the path/URL
    QUrl url = QUrl::fromUserInput(inputArg);

    // Check whether it is NOT a local path (e.g. smb://, nfs://, sftp://, trash://, etc.)
    bool isRemoteOrVirtual = !url.isLocalFile() && !url.scheme().isEmpty() && url.scheme() != QLatin1String("file");

    if (isRemoteOrVirtual) {
        QString dolphin = QStandardPaths::findExecutable(QStringLiteral("dolphin"));
        if (!dolphin.isEmpty()) {
            // Hand over to Dolphin and abort the mkFolderWidget start
            QProcess::startDetached(dolphin, { inputArg });
            return true;
        }
    }
    return false;
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QCoreApplication::setApplicationName("mkFolderWidget");
    QCoreApplication::setApplicationVersion("1.0");

    QCommandLineParser parser;
    parser.setApplicationDescription("Qt6 based minimalistic file manager");
    auto helpOption = parser.addHelpOption();
    auto versionOption = parser.addVersionOption();

    parser.addPositionalArgument("directory", "target directory to open", "[directory]");

    QCommandLineOption xOpt("X", "x-position of window", "PosX");
    QCommandLineOption yOpt("Y", "y-position of window", "PosY");
    QCommandLineOption wOpt("W", "window width", "WinW");
    QCommandLineOption hOpt("H", "window height", "WinH");
    QCommandLineOption fOpt("f", "filepath to focus", "sPathFocus");

    parser.addOptions({xOpt, yOpt, wOpt, hOpt, fOpt});

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

    // Get all arguments without an option
    const QStringList positionalArgs = parser.positionalArguments();

    // .value(0) returns the first argument, or an empty QString()
    // if no argument was passed (without an out-of-bounds crash)
    QString targetDir = positionalArgs.value(0);

    if (delegateNonLocalToDolphin(targetDir)) {
        return 0; // Exit right away, Dolphin takes over
    }

    targetDir = QDir::cleanPath(targetDir);

    if (targetDir.isEmpty() || !QDir(targetDir).exists()) {
        targetDir = QDir::homePath();
    }

    QString focusPath;

    QString rawFocus = parser.value(fOpt);
    if (!rawFocus.isEmpty()) {
        QFileInfo focusInfo(rawFocus);

        // focusInfo.absolutePath() returns the folder OF the file
        // (e.g. "/home/user/bla/blub.txt" becomes "/home/user/bla")
        targetDir = focusInfo.absolutePath();

        // focusPath stores the cleaned, absolute path of the file to focus
        focusPath = focusInfo.absoluteFilePath();
    }

    MainWindow w(targetDir, focusPath);

    if ((parser.isSet(xOpt) && parser.isSet(yOpt)) || (parser.isSet(wOpt) && parser.isSet(hOpt))) {
        w.ensurePolished(); // Prepares the widget styling
        w.winId();          // Forces the creation of the native OS window handle

        if (QWindow *window = w.windowHandle()) {
            window->create(); // Forces the OS to compute the window decoration (frame)
            QMargins margins = window->frameMargins(); // This is where we get the exact frame widths
            int left = margins.left();
            int right = margins.right();
            int bottom = margins.bottom();
            int top = margins.top();

            if (parser.isSet(wOpt) && parser.isSet(hOpt)) {
                int clientWidth = parser.value(wOpt).toInt() - left - right;
                int clientHeight = parser.value(hOpt).toInt() - top - bottom;

                w.resize(clientWidth, clientHeight);
            }

            if (parser.isSet(xOpt) && parser.isSet(yOpt)) {
                int clientPosX = parser.value(xOpt).toInt() + left;
                int clientPosY = parser.value(yOpt).toInt() /*+ top*/; // it's unknown why we don't need "top" in this case.
                w.move(clientPosX, clientPosY);
            }
        } else {
            if (parser.isSet(wOpt) && parser.isSet(hOpt)) {
                w.resize(parser.value(wOpt).toInt(), parser.value(hOpt).toInt());
            }
            if (parser.isSet(xOpt) && parser.isSet(yOpt)) {
                w.move(parser.value(xOpt).toInt(), parser.value(yOpt).toInt());
            }
        }
    }

    w.show();
    return QCoreApplication::exec();
}
