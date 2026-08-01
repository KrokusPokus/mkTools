#include "mainwindow.h"
#include "settingsmanager.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDir>
#include <QFont>
#include <QString>
#include <QTranslator>
#include <QWindow>

#include <iostream>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

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
    QCommandLineOption pOpt("p", "target directory", "sPathDir");
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

    // Holen aller optionslosen Argumente
    const QStringList positionalArgs = parser.positionalArguments();

    // .value(0) gibt das erste Argument zurück oder einen leeren QString(),
    // falls kein Argument übergeben wurde (ohne Out-of-bounds Absturz)
    QString targetDir = positionalArgs.value(0);

    // Falls der Windows-Parser den Backslash geschluckt und ein " an den String gehängt hat:
    if (targetDir.endsWith('"')) {
        targetDir.chop(1);  // Das falsche Anführungszeichen abschneiden
        targetDir += "/";   // Einen sauberen Ordner-Abschluss hinzufügen
    }
    targetDir = QDir::cleanPath(targetDir);

    if (targetDir == "::{20d04fe0-3aea-1069-a2d8-08002b30309d}") {
        targetDir = "drives://";
    } else if (targetDir.isEmpty() || !QDir(targetDir).exists()) {
        targetDir = QDir::homePath();
    }

    QString focusPath;

    QString rawFocus = parser.value(fOpt);
    if (!rawFocus.isEmpty()) {
        QFileInfo focusInfo(rawFocus);

        // focusInfo.absolutePath() gibt den Ordner ZUR Datei zurück
        // (z. B. aus "/home/user/bla/blub.txt" wird "/home/user/bla")
        targetDir = focusInfo.absolutePath();

        // focusPath speichert den bereinigten, absoluten Pfad zur Datei für den Fokus
        focusPath = focusInfo.absoluteFilePath();
    }

    MainWindow w(targetDir, focusPath);

    if ((parser.isSet(xOpt) && parser.isSet(yOpt)) || (parser.isSet(wOpt) && parser.isSet(hOpt))) {
        w.ensurePolished(); // Bereitet das Widget-Styling vor
        w.winId();          // Erzwingt die Erstellung des nativen OS-Fenster-Handles

        if (QWindow *window = w.windowHandle()) {
            window->create(); // Zwingt das OS, die Fensterdekoration (Rahmen) zu berechnen
            QMargins margins = window->frameMargins(); // Hier bekommen wir die exakten Rahmenbreiten
            int left = margins.left();
            int right = margins.right();
            int bottom = margins.bottom();
            int top = margins.top();

#ifdef Q_OS_WIN
            if (left == 0 && right == 0 && bottom == 0) {
                HWND hwnd = (HWND)w.winId();
                LONG style = GetWindowLongPtr(hwnd, GWL_STYLE);
                LONG exStyle = GetWindowLongPtr(hwnd, GWL_EXSTYLE);

                // Wir simulieren eine hypothetische Client-Area von 100x100 Pixeln
                RECT rect = {0, 0, 100, 100};

                // AdjustWindowRectEx rechnet die Ränder basierend auf dem Stil oben drauf
                if (AdjustWindowRectEx(&rect, style, FALSE, exStyle)) {
                    left = abs(rect.left);
                    right = rect.right - 100;
                    bottom = rect.bottom - 100;
                    top = abs(rect.top); // Überschreibt die 27px mit dem exakten OS-Wert
                }
            }
#endif

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
