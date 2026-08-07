#include "helpers.h"

#include <QCollator>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QImageReader>
#include <QFileInfo>
#include <QMimeType>
#include <QMimeDatabase>
#include <QProcess>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QRegularExpression>
#include <QUrl>

#include <zlib.h>

#ifdef Q_OS_WIN
#include <shellapi.h>   // needed for ShellExecuteEx in startProcessElevatedWin()
#endif

bool isTextFile(const QString &filePath) {
    QMimeDatabase db;
    QMimeType mime = db.mimeTypeForFile(filePath);
    return mime.inherits("text/plain");
}

QString cleanFileName(const QString &fileName) {
    QString pattern;

#ifdef Q_OS_WIN
    // Windows verbotene Zeichen: < > : " / \ | ? *
    // Plus: Steuerzeichen (0-31)
    pattern = "[<>:\"/\\\\|?*\\x00-\\x1F]";
#else
    // Linux/Unix verbotene Zeichen: / und der Null-Terminator \0
    pattern = "[/\\x00]";
#endif

    static const QRegularExpression re(pattern);

    QString cleanedName = fileName; // local copy
    cleanedName.replace(re, "_");

#ifdef Q_OS_WIN
    cleanedName = cleanedName.trimmed();
    while (cleanedName.endsWith('.')) {
        cleanedName.chop(1);
    }
#endif

    return cleanedName;
}

QString formatAdaptiveSize(quint64 bytes) {
    static const QLocale locale = QLocale::system();

    if (bytes < 1024) {
        return locale.toString(bytes) + " Bytes";
    }

    double size = static_cast<double>(bytes);
    static const QStringList units = {"Bytes", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB", "ZiB", "YiB"};
    int unitIndex = 0;

    while (size >= 1024.0 && unitIndex < units.size() - 1) {
        size /= 1024.0;
        unitIndex++;
    }

    int precision = 0;
    if (size < 10.0) {
        precision = 2; // z.B. 1,23 MiB
    } else if (size < 100.0) {
        precision = 1; // z.B. 12,3 MiB
    } else {
        precision = 0; // z.B. 123 MiB
    }

    return locale.toString(size, 'f', precision) + " " + units[unitIndex];
}

quint32 calculateCRC32(const QString &filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return 0; // Or handle error accordingly
    }

    // Initialize the CRC value
    uLong crc = crc32(0L, Z_NULL, 0);

    // Read in chunks to be memory efficient
    const int bufferSize = 1024 * 1024; // 1 MB buffer
    QByteArray buffer(bufferSize, Qt::Uninitialized);

    while (!file.atEnd()) {
        qint64 bytesRead = file.read(buffer.data(), bufferSize);
        // Update the CRC with the current chunk
        crc = crc32(crc, reinterpret_cast<const Bytef*>(buffer.data()), bytesRead);
    }

    file.close();
    return static_cast<quint32>(crc);
}

DesktopEntry getDesktopEntryById(const QString &id) {
    static const QStringList appDirs = QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation);
    for (const QString &dirPath : appDirs) {
        QFileInfo fileInfo(QDir(dirPath).filePath(id));
        DesktopEntry entry = getDesktopEntry(fileInfo);
        if (entry.isValid) {
            return entry;
        }
    }
    return DesktopEntry();
}

DesktopEntry getDesktopEntry(const QFileInfo &fileInfo) {

    static const QString localeName = QLocale::system().name().left(2); // "de", "fr", ...
    static const QString localKey = QString("Name[%1]=").arg(localeName);

    DesktopEntry currentEntry;

    QFile file(fileInfo.filePath());
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
        in.setEncoding(QStringConverter::Utf8);

        QString iniName, iniNameLocalised;
        bool bInMainSection = false;

        while (!in.atEnd()) {
            QString line = in.readLine().trimmed();

            if (line.startsWith('[') && line.endsWith(']')) {
                if (bInMainSection) break;
                bInMainSection = (line == "[Desktop Entry]");
                continue;
            }

            if (!bInMainSection || line.startsWith('#')) continue;

            if (line.startsWith("Name=")) iniName = line.mid(5);
            else if (line.startsWith(localKey)) iniNameLocalised = line.mid(localKey.size());
            else if (line.startsWith("Type=")) currentEntry.type = line.mid(5);
            // URL (fängt sowohl "URL=" als auch "URL[$e]=" ab)
            else if (line.startsWith("URL=") || line.startsWith("URL[")) {
                int idx = line.indexOf('=');
                if (idx != -1) currentEntry.url = line.mid(idx + 1).trimmed();
            }
            else if (line.startsWith("Icon=")) currentEntry.icon = line.mid(5);
            else if (line.startsWith("Exec=")) currentEntry.exec = line.mid(5);
            else if (line.startsWith("Path=")) currentEntry.workDir = line.mid(5);
            else if (line.startsWith("Terminal=true")) currentEntry.useTerminal = true;
            else if (line.startsWith("Hidden=true")) {
                return DesktopEntry();
            }
        }

        currentEntry.name = iniNameLocalised.isEmpty() ? iniName : iniNameLocalised;
        currentEntry.isValid = !currentEntry.name.isEmpty();
        currentEntry.id = fileInfo.fileName();
        currentEntry.path = fileInfo.filePath();

        if (currentEntry.isValid) {
            return currentEntry;
        }
    }

    return DesktopEntry();
}

void openFileListWithHandler(const QString &handlerApp, const QStringList &fileList) {
    if (handlerApp.endsWith(".desktop", Qt::CaseInsensitive)) {
        launchDesktopFile(getDesktopEntryById(handlerApp), fileList);
        return;
    }

    if (QFile::exists(handlerApp)) {
        QProcess::startDetached(QDir::toNativeSeparators(handlerApp), fileList);
        return;
    }

    QString absolutePath = QStandardPaths::findExecutable(handlerApp);
    if (!absolutePath.isEmpty()) {
        // bool QProcess::startDetached(const QString &program, const QStringList &arguments = {}, const QString &workingDirectory = QString(), qint64 *pid = nullptr)
        QProcess::startDetached(QDir::toNativeSeparators(absolutePath), fileList);
        return;
    }

    qDebug() << "openFileListWithHandler() No absolute path found for:" << handlerApp;
}

void launchDesktopFile(const DesktopEntry &info, const QStringList &fileList) {
    if (!info.isValid) return;

    // ==========================================
    // 1. Handhabung von Type=Link oder verknüpften URLs
    // ==========================================
    if (info.type.compare(QLatin1String("Link"), Qt::CaseInsensitive) == 0 || !info.url.isEmpty()) {
        QString urlStr = Helpers::expandPath(info.url.trimmed());
        if (urlStr.isEmpty()) return;

        // Keine Netzwerkschemata wie http://, https://, ftp:// etc.
        if (!urlStr.contains("://")) {
            QFileInfo fileInfo(urlStr);

            // Prüft plattformübergreifend auf relative Pfade
            // (deckt "./", "../", "subfolder/file", etc. ab)
            if (fileInfo.isRelative()) {
                QDir desktopFileDir = QFileInfo(info.path).absoluteDir();
                urlStr = desktopFileDir.absoluteFilePath(urlStr);
            }
        }

        QUrl url = QUrl::fromUserInput(urlStr);
        if (url.isValid()) {
            QDesktopServices::openUrl(url);
        } else {
            qWarning() << "launchDesktopFile(): Ungültige URL:" << urlStr;
        }
        return;
    }

    // ==========================================
    // 2. Handhabung von Type=Application (Normaler Exec-Start)
    // ==========================================
    if (info.exec.isEmpty()) return;

    QStringList args = QProcess::splitCommand(info.exec);
    if (args.isEmpty()) return;
    QString program = args.takeFirst();

    if (!fileList.isEmpty()) {
        for (int i = 0; i < args.size(); ++i) {
            if (args[i] == "%f" || args[i] == "%u") {
                args[i] = fileList.first();
            } else if (args[i] == "%F" || args[i] == "%U") {
                args.removeAt(i);
                for (int j = 0; j < fileList.size(); ++j)
                    args.insert(i + j, fileList.at(j));
                break;
            }
        }
        if (!info.exec.contains('%'))
            args.append(fileList);
    } else {
        static const QRegularExpression placeholders("%[uUfFiIcK]");
        args.removeIf([&](const QString &a) { return a.contains(placeholders); });
    }

    program = Helpers::expandPath(program);
    if (!QFileInfo(program).isAbsolute()) {
        program = QStandardPaths::findExecutable(program);
        if (program.isEmpty()) {
            return;
        }
    }

    // ==========================================
    // Terminal-Handling
    // ==========================================
    if (info.useTerminal) {
        QString terminal;
        // Priorisierte Liste bekannter Linux-Terminals (Konsole zuerst wegen KDE)
        const QStringList terminalCandidates = {
            "konsole", "gnome-terminal", "xfce4-terminal", "alacritty", "kitty", "xterm"
        };

        for (const QString &candidate : terminalCandidates) {
            terminal = QStandardPaths::findExecutable(candidate);
            if (!terminal.isEmpty()) {
                break;
            }
        }

        if (!terminal.isEmpty()) {
            QStringList terminalArgs;

            // Ältere/Schlanke Terminals wie xterm/alacritty brauchen meist '-e'
            // Moderne Desktoptop-Terminals (konsole, gnome-terminal) bevorzugen '--'
            if (terminal.endsWith("gnome-terminal")) {
                terminalArgs << "--" << program << args;
            } else {
                terminalArgs << "-e" << program << args;
            }

            // Das Terminal wird zum Hauptprogramm, die App wandert in die Argumente
            program = terminal;
            args = terminalArgs;
        } else {
            // Fallback: Wenn absolut kein Terminal gefunden wurde,
            // loggen wir eine Warnung und starten es normal im Hintergrund.
            qWarning("Konnte keinen Terminal-Emulator finden. Starte App normal.");
        }
    }
    // ==========================================

    QString workDir = Helpers::expandPath(info.workDir.trimmed());
    if (workDir.startsWith('"') && workDir.endsWith('"'))
        workDir = workDir.mid(1, workDir.length() - 2);
    if (workDir.isEmpty() || !QDir(workDir).exists())
        workDir = QFileInfo(program).absolutePath();
    qDebug() << "launchDesktopFile() program:" << program << "args:" << args;
    QProcess::startDetached(program, args, workDir);
}

void browseToFile(const QString &path, const QString &fileManager) {
    if (path.isEmpty()) return;

    QFileInfo fileInfo(path);
    if (!fileInfo.exists() && !(fileInfo.isSymLink() || fileInfo.isShortcut())) return;

    QString targetPath = path;
    QString targetFile = "";
    if (fileInfo.isFile()) {
        targetPath = fileInfo.absoluteDir().path();
        targetFile = path;
    }

    QDir appDir(QCoreApplication::applicationDirPath());
#ifdef Q_OS_WIN
    QString mkFolderWidgetPath = appDir.filePath("mkFolderWidget.exe");
#elif defined(Q_OS_LINUX)
    QString mkFolderWidgetPath = appDir.filePath("mkFolderWidget");
#endif

    if (QFile::exists(mkFolderWidgetPath) && (fileManager.isEmpty() || fileManager == "mkFolderWidget")) {
        QStringList args;
        args << QDir::toNativeSeparators(targetPath);
        if (!targetFile.isEmpty()) {
            args << "-f" << QDir::toNativeSeparators(targetFile);
        }
        QProcess::startDetached(mkFolderWidgetPath, args);
        return;
    }

    if (fileManager.endsWith(".desktop", Qt::CaseInsensitive)) {
        launchDesktopFile(getDesktopEntryById(fileManager), {targetPath});
        return;
    }

#ifdef Q_OS_WIN
    QStringList args;
    args << "/select," + QDir::toNativeSeparators(path);
    QProcess::startDetached("explorer.exe", args);
#elif defined(Q_OS_LINUX)
    QProcess::startDetached("dbus-send", {
                                             "--session",
                                             "--print-reply",
                                             "--dest=org.freedesktop.FileManager1",
                                             "/org/freedesktop/FileManager1",
                                             "org.freedesktop.FileManager1.ShowItems",
                                             "array:string:" + QUrl::fromLocalFile(path).toString(),
                                             "string:\"\""
                                         });
#else
    QDesktopServices::openUrl(QUrl::fromLocalFile(targetPath));
#endif
}

QString getDisplayName(const QFileInfo &fileInfo, bool showFileExtensions) {
    if (showFileExtensions || fileInfo.isDir()) {
        return fileInfo.fileName();
    }

    QString displayName = fileInfo.completeBaseName();
    return displayName.isEmpty() ? fileInfo.fileName() : displayName;
}

QString getDisplayName(const QString &filePath, bool isDir, bool showFileExtensions) {
    /* Alternative way
    // QFileInfo wird hier rein als Text-Parser genutzt! Kein Disk-I/O.
    QFileInfo finfo(filePath);

    if (showFileExtensions || isDir) {
        return finfo.fileName();
    }

    QString displayName = finfo.completeBaseName();
    return displayName.isEmpty() ? finfo.fileName() : displayName;
    */

    int lastSeparator = filePath.lastIndexOf('/');
    if (lastSeparator == -1) {
        lastSeparator = filePath.lastIndexOf('\\');
    }

    QString fileName = (lastSeparator == -1) ? filePath : filePath.mid(lastSeparator + 1);

    if (showFileExtensions || isDir) {
        return fileName;
    }

    int lastDot = fileName.lastIndexOf('.');
    if (lastDot <= 0) {
        return fileName;
    }
    return fileName.sliced(0, lastDot);
}

QPixmap generateThumbnail(const QFileInfo &fileInfo) {
    // Unterstützte Formate statisch cachen (wird nur einmalig beim allerersten Aufruf erstellt)
    static const QSet<QByteArray> supportedFormats = []() {
        auto formats = QImageReader::supportedImageFormats();
        return QSet<QByteArray>(formats.begin(), formats.end());
    }();

    if (supportedFormats.contains(fileInfo.suffix().toLower().toUtf8())) {
        QImageReader reader(fileInfo.absoluteFilePath());
        reader.setAutoTransform(true); // Wichtig für EXIF-Rotationen von Smartphones

        if (reader.canRead()) {
            // 1. Die echte Dimension des Bildes auslesen (kostet kaum Performance)
            QSize originalSize = reader.size();

            // 2. Proportionale Größe berechnen, die in eine 96x96 Box passt
            // Aus z.B. 1920x1080 wird hier automatisch 96x54
            QSize scaledSize = originalSize.scaled(QSize(96, 96), Qt::KeepAspectRatio);

            // 3. Dem Reader die proportionale Größe mitteilen
            reader.setScaledSize(scaledSize);

            QImage img = reader.read();
            if (!img.isNull()) {
                return QPixmap::fromImage(img);
            }
        }
    }

    return QPixmap(); // Fallback
}

bool hasIconExt(const QFileInfo &fileInfo) {
    QString ext = fileInfo.suffix().toLower();
    return (ext == "cur" || ext == "ico" || ext == "icns");
}

bool hasOnlyFiles(const QStringList &pathList) {
    bool onlyFiles = true;
    for (QString path : pathList) {
        if (!QFileInfo(path).isFile()) {
            onlyFiles = false;
            break;
        }
    }

    return onlyFiles;
}

bool onSameStorageDevice(const QString &pathA, const QString &pathB) {
    QString folderA = QFileInfo(pathA).absolutePath();
    QString folderB = QFileInfo(pathB).absolutePath();

    QStorageInfo storageA(folderA);
    QStorageInfo storageB(folderB);

    return storageA.isValid() && storageA.isReady() &&
           storageB.isValid() && storageB.isReady() &&
           (storageA == storageB);
}

void createInternetShortcut(const QString &urlStr, const QString &targetDir, const QString &webTitle) {
    // 1. Basis-Dateinamen bestimmen (Trimmen entfernt führende/nachstehende Leerzeichen)
    QString fileName = webTitle.trimmed();

    // Fallback A: Wenn data->text() leer war, nutzen wir die Domain (z.B. "www.wikipedia.org")
    if (fileName.isEmpty()) {
        QUrl url(urlStr);
        fileName = url.host();
    }

    // Fallback B: Wenn auch die Domain fehlschlägt (z.B. ungültige URL), generischer Name
    if (fileName.isEmpty()) {
        fileName = "Internet-Link";
    }

    // 2. Dateinamen bereinigen (Sanitize)
    // Ersetzt alle im Windows/Linux/macOS-Dateisystem verbotenen Zeichen durch einen Unterstrich: \ / : * ? " < > |
    fileName.replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");
    fileName = fileName.trimmed();

    // Sicherheitsprüfung: Falls nach der Bereinigung nichts mehr übrig ist
    if (fileName.isEmpty()) {
        fileName = "Internet-Link";
    }

    // --- NEU: Plattformabhängige Dateiendung festlegen ---
    QString extension = ".url";
#if defined(Q_OS_LINUX)
    extension = ".desktop";
#endif

    if (!fileName.endsWith(extension, Qt::CaseInsensitive)) {
        fileName += extension;
    }

    // 3. Zielverzeichnis prüfen
    QDir dir(targetDir);
    if (!dir.exists()) {
        qWarning() << "Zielverzeichnis existiert nicht:" << targetDir;
        return;
    }

    // 4. Kollisionsschutz: Falls die Datei schon existiert, eine Nummerierung anhängen
    // REPARATUR: QFileInfo nutzen statt hartem chop(4), da ".desktop" 8 Zeichen lang ist!
    QString fullPath = dir.filePath(fileName);
    QFileInfo fileInfo(fileName);
    QString baseName = fileInfo.completeBaseName();
    QString finalAnzeigename = baseName; // Merken für das 'Name=' Feld unter Linux

    if (QFile::exists(fullPath)) {
        int counter = 1;
        while (QFile::exists(fullPath)) {
            finalAnzeigename = QString("%1 (%2)").arg(baseName).arg(counter);
            QString numberedName = finalAnzeigename + extension;
            fullPath = dir.filePath(numberedName);
            counter++;
        }
    }

    // 5. Die Datei schreiben
    QFile file(fullPath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);

#ifdef Q_OS_WIN
        // Windows .url Format
        out << "[InternetShortcut]\n";
        out << "URL=" << urlStr << "\n";
#elif defined(Q_OS_LINUX)
        // Linux .desktop Format für Web-Links
        out << "[Desktop Entry]\n";
        out << "Icon=text-html\n";                  // Standard-Icon für HTML/Web-Links
        out << "Name=" << finalAnzeigename << "\n"; // Das ist der Text, den Linux anzeigt
        out << "Type=Link\n";
        out << "URL=" << urlStr << "\n";
#endif

        file.close();
    } else {
        qCritical() << "Fehler beim Erstellen der Verknüpfung:" << file.errorString();
    }
}

bool isCurrentProcessElevated() {
#ifdef Q_OS_WIN
    HANDLE hProcess = GetCurrentProcess();
    HANDLE hToken = NULL;
    bool elevated = false;

    // 1. Zugriff auf das Sicherheits-Token des Prozesses anfordern
    if (OpenProcessToken(hProcess, TOKEN_QUERY, &hToken)) {
        TOKEN_ELEVATION elevation;
        DWORD dwSize = sizeof(elevation);

        // 2. Abfragen, ob das Token angehoben (elevated) ist
        if (GetTokenInformation(hToken, TokenElevation, &elevation, sizeof(elevation), &dwSize)) {
            elevated = elevation.TokenIsElevated;
        }
        CloseHandle(hToken);
    }
    return elevated;
#elif defined(Q_OS_LINUX)
    return (geteuid() == 0);
#else
    return false;
#endif
}

#ifdef Q_OS_WIN
bool startProcessElevatedWin(const QString &programPath, const QString &arguments) {
    SHELLEXECUTEINFO shExecInfo = { 0 };
    shExecInfo.cbSize = sizeof(SHELLEXECUTEINFO);
    shExecInfo.fMask = SEE_MASK_NOCLOSEPROCESS; // Erlaubt es uns, das Prozess-Handle zu tracken
    shExecInfo.hwnd = NULL;

    // "runas" ist der magische Schlüssel, der den UAC-Admin-Dialog erzwingt!
    shExecInfo.lpVerb = L"runas";

    // Qt-Strings in Windows-kompatible WCHAR-Arrays umwandeln (.utf16())
    shExecInfo.lpFile = reinterpret_cast<LPCWSTR>(programPath.utf16());
    shExecInfo.lpParameters = reinterpret_cast<LPCWSTR>(arguments.utf16());
    shExecInfo.lpDirectory = NULL;
    shExecInfo.nShow = SW_SHOWNORMAL;

    // Ausführen
    if (ShellExecuteEx(&shExecInfo)) {
        // Der Prozess wurde erfolgreich gestartet!
        // Da wir SEE_MASK_NOCLOSEPROCESS gesetzt haben, MÜSSEN wir das Handle schließen,
        // um Speicherlecks zu verhindern.
        if (shExecInfo.hProcess) {
            CloseHandle(shExecInfo.hProcess);
        }
        return true;
    }

    // Wenn wir hier landen, hat der Nutzer im UAC-Dialog auf "Nein" geklickt
    // oder die Datei wurde nicht gefunden (GetLastError() gibt Aufschluss).
    return false;
}


QString argumentsToWinString(const QStringList &args) {
    QStringList escapedArgs;

    for (QString arg : args) {
        // Falls das Argument Leerzeichen enthält oder leer ist, müssen wir es quoten
        if (arg.contains(' ') || arg.contains('\t') || arg.isEmpty()) {

            // Fiese Windows-Falle: Wenn das Argument auf einen Backslash endet,
            // würde das abschließende " durch den Backslash maskiert werden.
            // Lösung: Den letzten Backslash verdoppeln!
            if (arg.endsWith('\\')) {
                arg += '\\';
            }

            // In Anführungszeichen packen
            escapedArgs << QString("\"%1\"").arg(arg);
        } else {
            // Keine Leerzeichen? Einfach so übernehmen
            escapedArgs << arg;
        }
    }

    // Alle Argumente mit einem Leerzeichen dazwischen zusammenfügen
    return escapedArgs.join(' ');
}
#endif

bool atWordBoundary(const QString &fileName, const QString &word, Qt::CaseSensitivity caseSensitivity) {
    static const QString separators = " .(-_[";
    int pos = 0;
    while ((pos = fileName.indexOf(word, pos, caseSensitivity)) != -1) {
        if (pos > 0 && separators.contains(fileName[pos - 1])) {
            return true;
        }
        pos += word.length();
    }
    return false;
};

uint getNameMatchQuality(const QString &itemName, const QString &path, const QString &searchString, const QStringList &searchStringSplit, Qt::CaseSensitivity caseSensitivity) {
    if (QString::compare(itemName , searchString, caseSensitivity) == 0) {
        return 1;
    }

    if (itemName.startsWith(searchString, caseSensitivity)) {
        int searchLen = searchString.length();
        if (itemName.length() > searchLen && itemName.at(searchLen) == '.') {
            return 2;
        }
        return 3;
    }
    
    QString pathMatchingString;
    if (searchString.contains(QDir::separator())) {
        QString itemPath = path.endsWith('/') ? (path + itemName) : (path + '/' + itemName);
        pathMatchingString = QDir::toNativeSeparators(itemPath);
        if (pathMatchingString.contains(searchString, caseSensitivity)) {
            return 4;
        }
    }

    // Match search terms separately
    bool bMatchType1 = true;
    bool bMatchType2 = false; // Wortgrenze/Start im Dateinamen
    bool bMatchType3 = false; // Erstes Wort bei Index 0 im Dateinamen
    bool bMatchType4 = false; // Mindestens ein Wort musste in den Pfad ausweichen
    int iIndex = 0;

    for (const QString &word : std::as_const(searchStringSplit)) {
        int iFoundPos = itemName.indexOf(word, 0, caseSensitivity);

        if (iFoundPos == -1) { // not found in file name
            if (!pathMatchingString.isEmpty()) {
                if (pathMatchingString.indexOf(word, 0, caseSensitivity) == -1) { // not found in path either
                    bMatchType1 = false;
                    break;
                } else {
                    bMatchType4 = true;
                    iIndex++;
                    continue;
                }
            }

            bMatchType1 = false;
            break;
        }

        if (iFoundPos == 0) {   // Improved match, since one searchterm found at very beginning of filename
            bMatchType2 = true;
            if (iIndex == 0) {  // Further improved match, since *first* searchterm found at very beginning of filename
                bMatchType3 = true;
            }
        } else if (atWordBoundary(itemName, word, caseSensitivity)) {
            // Improved match: one searchterm found at a word boundary in filename
            bMatchType2 = true;
        }

        iIndex++;
    }

    if (bMatchType1 == false) {
        return 0;
    }

    if (bMatchType3) return 5;
    if (bMatchType2) return 6;
    if (bMatchType4) return 7;
    return 8;
}

uint getRegExNameMatchQuality(const QFileInfo &fileInfo, const QRegularExpression &re) {
    if (re.match(fileInfo.fileName()).hasMatch()) {
        return 1;
    }
    return 0;
}

uint getContentMatchCount(const QFileInfo &fileInfo, const QString &searchStringContent, Qt::CaseSensitivity caseSensitivity, const QSet<QString> &FileExtTextSet) {
    //if (!isTextFile(fileInfo.filePath())) {   // much, MUCH slower...

    QString sFileExt = fileInfo.suffix().toLower(); // since contains() is CaseSensitive by default
    if (!FileExtTextSet.contains(sFileExt)) {
        return 0;
    }

    if (fileInfo.size() > 64 * 1024 * 1024) {
        return 0;
    }

    QFile file(fileInfo.filePath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return 0;
    }

    QTextStream in(&file);

    // Option A: explizit UTF-8 erzwingen (aktuelles Verhalten dokumentieren)
    //in.setEncoding(QStringConverter::Utf8);
    // Option B: Locale-basiert dekodieren (breiter, aber langsamer)
    //in.setEncoding(QStringConverter::System);

    QString fileContent = in.readAll();

    int searchStringLength = searchStringContent.length();
    int iCount = 0;
    int iPos = 0;

    while ((iPos = fileContent.indexOf(searchStringContent, iPos, caseSensitivity)) != -1) {
        iCount++;
        iPos += searchStringLength;
    }
    return iCount;
}

uint getRegExContentMatchCount(const QFileInfo &fileInfo, const QRegularExpression &re, const QSet<QString> &FileExtTextSet) {
    //if (!isTextFile(fileInfo.filePath())) {   // much, MUCH slower...

    QString sFileExt = fileInfo.suffix().toLower(); // since contains() is CaseSensitive by default
    if (!FileExtTextSet.contains(sFileExt)) {
        return 0;
    }

    QFile file(fileInfo.filePath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return 0;
    }

    if (file.size() > 64 * 1024 * 1024) {
        return 0;
    }

    QTextStream in(&file);
    QString fileContent = in.readAll();

    QRegularExpressionMatchIterator it = re.globalMatch(fileContent);
    int iCount = 0;

    while (it.hasNext()) {
        it.next();
        iCount++;
    }

    return iCount;
}

#ifdef Q_OS_WIN
LnkInfo getLnkInfo(const QString &filePath) {
    LnkInfo info;
    WIN32_FILE_ATTRIBUTE_DATA fileData;

    if (GetFileAttributesExW(reinterpret_cast<LPCWSTR>(filePath.utf16()),
                             GetFileExInfoStandard,
                             &fileData))
    {
        ULARGE_INTEGER size;
        size.LowPart = fileData.nFileSizeLow;
        size.HighPart = fileData.nFileSizeHigh;
        info.size = static_cast<qint64>(size.QuadPart);

        info.birthTime      = fileTimeToQDateTime(fileData.ftCreationTime);
        info.lastAccessTime = fileTimeToQDateTime(fileData.ftLastAccessTime);
        info.lastWriteTime  = fileTimeToQDateTime(fileData.ftLastWriteTime);

        info.isReadOnly = (fileData.dwFileAttributes & FILE_ATTRIBUTE_READONLY) != 0;
        info.isHidden   = (fileData.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) != 0;
        info.isSystem   = (fileData.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM) != 0;

        info.exists = true;
    }

    return info;
}
qint64 getLnkSize(const QString &filePath) {
    WIN32_FILE_ATTRIBUTE_DATA fileData;
    if (GetFileAttributesExW(reinterpret_cast<LPCWSTR>(filePath.utf16()), GetFileExInfoStandard, &fileData)) {
        ULARGE_INTEGER size;
        size.LowPart = fileData.nFileSizeLow;
        size.HighPart = fileData.nFileSizeHigh;
        return static_cast<qint64>(size.QuadPart);
    }

    return 0;
}
QDateTime fileTimeToQDateTime(const FILETIME &ft) {
    ULARGE_INTEGER uli;
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;

    const ULONGLONG epochOffset = 116444736000000000ULL;

    // Underflow verhindern! (Falls Zeitstempel 0 oder vor 1.1.1970 ist)
    if (uli.QuadPart < epochOffset) {
        return QDateTime();
    }

    qint64 msecs = static_cast<qint64>((uli.QuadPart - epochOffset) / 10000ULL);

    return QDateTime::fromMSecsSinceEpoch(msecs, QTimeZone::UTC).toLocalTime();
}
#endif

QString readLnkTargetOnLinux(const QString &lnkFilePath) {
    QFile file(lnkFilePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return QString();
    }

    QByteArray data = file.readAll();
    file.close();

    // 1. Minimum Header-Größe prüfen (76 Bytes Header)
    if (data.size() < 0x4C) return QString();

    // 2. Prüfen, ob es eine valide LNK-Datei ist (Header GUID prüfen)
    static const char lnkGuid[] = "\x01\x14\x02\x00\x00\x00\x00\x00\xC0\x00\x00\x00\x00\x00\x00\x46";
    if (data.mid(4, 16) != QByteArray(lnkGuid, 16)) {
        return QString(); // Keine echte .lnk Datei
    }

    // 3. Flags an Offset 0x14 lesen
    quint32 flags = *reinterpret_cast<const quint32*>(data.constData() + 0x14);
    bool hasLinkInfo = (flags & 0x02) != 0;
    bool hasTargetIDList = (flags & 0x01) != 0;

    if (!hasLinkInfo) return QString(); // Enthält keine Pfad-Info

    // 4. Offset der LinkInfo-Struktur ermitteln
    int offset = 0x4C;
    if (hasTargetIDList) {
        if (data.size() < offset + 2) return QString();
        quint16 idListSize = *reinterpret_cast<const quint16*>(data.constData() + offset);
        offset += 2 + idListSize; // IDList überspringen
    }

    if (data.size() < offset + 0x10) return QString();

    // 5. Lokalen Pfad-Offset aus der LinkInfo-Struktur lesen
    quint32 localPathOffset = *reinterpret_cast<const quint32*>(data.constData() + offset + 0x10);
    int finalPathOffset = offset + localPathOffset;

    if (finalPathOffset >= data.size()) return QString();

    // 6. Pfad als ASCII-Null-terminierten String auslesen
    const char* pathPtr = data.constData() + finalPathOffset;
    return QString::fromLocal8Bit(pathPtr);
}

QString getSiblingPath(const QString &path, bool previous) {
    QFileInfo info(path);
    QString currentName = info.fileName();
    QDir parentDir = info.dir();

    // Abbruch, falls der Parent-Ordner nicht existiert oder der Pfad ungültig ist
    if (!parentDir.exists() || currentName.isEmpty()) {
        return QString();
    }

    // Nur Ordner auflisten, "." und ".." ignorieren.
    // QDir::Unsorted spart CPU-Zeit, da wir die Liste ohnehin selbst sortieren.
    parentDir.setFilter(QDir::Dirs | QDir::NoDotAndDotDot);
    parentDir.setSorting(QDir::Unsorted);

    QStringList siblings = parentDir.entryList();

    // QCollator konfigurieren
    QCollator collator;
    collator.setNumericMode(true); // "Natural Sorting": 2 kommt vor 10
    collator.setCaseSensitivity(Qt::CaseInsensitive); // a und A gleich behandeln

    // QStringList mit dem QCollator als Funktor sortieren.
    // QCollator hat den operator() überladen, std::sort akzeptiert ihn daher direkt.
    std::sort(siblings.begin(), siblings.end(), collator);

    // Position des aktuellen Ordners in der nun natürlich sortierten Liste finden
    int currentIndex = siblings.indexOf(currentName);

    if (currentIndex == -1) {
        return QString();
    }

    // Ziel-Index berechnen
    int targetIndex = previous ? (currentIndex - 1) : (currentIndex + 1);

    // Prüfen, ob wir noch im Gültigkeitsbereich der Liste sind
    if (targetIndex >= 0 && targetIndex < siblings.size()) {
        return parentDir.absoluteFilePath(siblings.at(targetIndex));
    }

    // Am Anfang oder Ende der Liste angekommen -> Kein Sibling verfügbar
    return QString();
}

namespace {

    constexpr int kMaxInputLength = 8192;
    constexpr int kMaxLinesChecked = 200;

    // Entfernt unsichtbare/steuernde Unicode-Zeichen, die präparierte Webseiten in
    // kopierten Text einschleusen können (Zero-Width-Chars, Bidi-Override wie bei
    // "Trojan Source"-Angriffen, BOM). Echte RTL-Schrift (Arabisch/Hebräisch) ist
    // davon NICHT betroffen - das sind normale Buchstaben, keine Steuerzeichen.
    QString stripInvisibleAndControlChars(const QString &input) {
        QString out;
        out.reserve(input.size());
        for (QChar c : input) {
            const ushort u = c.unicode();
            if (c == '\n' || c == '\r' || c == '\t') { out += c; continue; }
            if (u < 0x20 || (u >= 0x7F && u <= 0x9F)) continue;
            if (u == 0x200B || u == 0x200C || u == 0x200D || u == 0xFEFF ||
                (u >= 0x202A && u <= 0x202E) || (u >= 0x2066 && u <= 0x2069))
                continue;
            out += c;
        }
        return out;
    }

    // Ein einziger Durchlauf über 'input': ersetzte Werte werden NICHT erneut gescannt,
    // daher ist eine Verkettung ($FOO expandiert zu einem String, der wieder ${...} enthält
    // und erneut aufgelöst wird) strukturell unmöglich, nicht nur begrenzt.
    QString expandVariablesOnce(const QString &input, const QRegularExpression &regex,
                                const std::function<QString(const QRegularExpressionMatch&)> &resolve) {
        QString result;
        result.reserve(input.size());
        int lastEnd = 0;
        auto it = regex.globalMatch(input);
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            result += input.mid(lastEnd, m.capturedStart() - lastEnd);
            result += resolve(m);
            lastEnd = m.capturedEnd();
        }
        result += input.mid(lastEnd);
        return result;
    }

    // Expandiert eine einzelne Zeile rein im Speicher
    QString expandSingleLine(QString line) {
        line = line.trimmed();
        if (line.isEmpty()) return QString();

        // 1. Anführungszeichen entfernen
        if (line.length() >= 2 &&
            ((line.startsWith('"') && line.endsWith('"')) ||
             (line.startsWith('\'') && line.endsWith('\''))))
        {
            line = line.mid(1, line.length() - 2).trimmed();
            if (line.isEmpty()) return QString();
        }

        // 2. file:// URLs in lokale Pfade umwandeln
        QUrl url(line);
        if (url.isLocalFile()) {
            line = url.toLocalFile();
        }

#ifdef Q_OS_WIN
        static const QRegularExpression winEnvRegex(R"(%([A-Za-z0-9_()]+)%)");
        line = expandVariablesOnce(line, winEnvRegex, [](const QRegularExpressionMatch &m) {
            const QByteArray name = m.captured(1).toUtf8();
            if (!qEnvironmentVariableIsSet(name.constData())) return m.captured(0);
            return qEnvironmentVariable(name.constData());
        });
#else
        // Gilt für Linux UND macOS (POSIX-Semantik ist identisch)
        if (line == "~") {
            line = QDir::homePath();
        } else if (line.startsWith("~/") || line.startsWith("~\\")) {
            line = QDir::homePath() + line.mid(1);
        }

        static const QRegularExpression posixEnvRegex(R"(\$\{([A-Za-z0-9_]+)\}|\$([A-Za-z0-9_]+))");
        line = expandVariablesOnce(line, posixEnvRegex, [](const QRegularExpressionMatch &m) {
            const QString varName = m.captured(1).isEmpty() ? m.captured(2) : m.captured(1);
            if (varName == "HOME") return QDir::homePath();
            const QByteArray name = varName.toUtf8();
            if (!qEnvironmentVariableIsSet(name.constData())) return m.captured(0);
            return qEnvironmentVariable(name.constData());
        });
#endif

        return QDir::cleanPath(line);
    }

    // Prüft reine Syntax-Kriterien ohne Dateisystem-Zugriff
    bool isTechnicallyValidPath(const QString &path) {
        if (path.isEmpty()) return false;

        // Nach der Expansion (~ / $HOME / %APPDATA% / file://) muss ein gültiger lokaler Pfad absolut sein
        if (!QDir::isAbsolutePath(path)) {
            return false;
        }

        return true;
    }

} // namespace


namespace Helpers {

    QString expandPath(QString path) {
        if (path.size() > kMaxInputLength) {
            path.truncate(kMaxInputLength);
        }
        path = stripInvisibleAndControlChars(path);
        return expandSingleLine(path);
    }

    QString getPathFromClipboard(QString text) {
        if (text.size() > kMaxInputLength) {
            text.truncate(kMaxInputLength);
        }

        text = stripInvisibleAndControlChars(text);
        if (text.trimmed().isEmpty()) return QString();

        // Guides/Tutorials landen oft mit Zeilenumbrüchen in der Zwischenablage.
        // Wir nehmen die erste Zeile, die zu einem gültigen existierenden Pfad führt.
        const QStringList lines = text.split(QRegularExpression(R"(\r\n|\r|\n)"), Qt::SkipEmptyParts);

        int checked = 0;
        for (const QString &rawLine : lines) {
            if (checked++ >= kMaxLinesChecked) break;

            const QString expanded = expandSingleLine(rawLine);

            if (isTechnicallyValidPath(expanded)) {
                return expanded;
            }
        }

        return QString();
    }

} // namespace Helpers
