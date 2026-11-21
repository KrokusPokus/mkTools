#include "helpers.h"

#include <QCollator>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QMimeType>
#include <QMimeDatabase>
#include <QProcess>
#include <QRegularExpression>
#include <QSharedMemory>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QTimer>
#include <QUrl>

#include <zlib.h>

#include <algorithm>

#include <sys/stat.h>

#include <KApplicationTrader>
#include <KIO/ApplicationLauncherJob>
#include <KIO/OpenUrlJob>
#include <KPropertiesDialog>
#include <KService>
#include <QDBusConnection>
#include <QDBusMessage>

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
    QString expandVariablesOnce(const QString &input,
                                const QRegularExpression &regex,
                                const std::function<QString(const QRegularExpressionMatch&)> &resolve) {

        auto it = regex.globalMatch(input);

        if (!it.hasNext()) {
            return input;
        }

        QString result;
        result.reserve(input.size());
        qsizetype lastEnd = 0;
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            result += input.sliced(lastEnd, m.capturedStart() - lastEnd);
            result += resolve(m);
            lastEnd = m.capturedEnd();
        }
        result += input.sliced(lastEnd);
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
    }

} // namespace


namespace {
// ---------------------------------------------------------------------------------------------
// Exec-Zeile nach der Desktop Entry Specification zerlegen und Feldcodes ersetzen
// https://specifications.freedesktop.org/desktop-entry-spec/latest/exec-variables.html
// ---------------------------------------------------------------------------------------------

struct ExecToken {
    QString text;
    bool quoted = false; // Argument stand (ganz oder teilweise) in "..."
};

// Stufe 1: String-Escapes, die für jeden Wert vom Typ "string" gelten (\s \n \t \r \\)
QString unescapeDesktopString(const QString &in) {
    QString out;
    out.reserve(in.size());
    for (qsizetype i = 0; i < in.size(); ++i) {
        const QChar c = in.at(i);
        if (c == '\\' && i + 1 < in.size()) {
            const QChar n = in.at(i + 1);
            if (n == 's')  { out += ' ';  ++i; continue; }
            if (n == 'n')  { out += '\n'; ++i; continue; }
            if (n == 't')  { out += '\t'; ++i; continue; }
            if (n == 'r')  { out += '\r'; ++i; continue; }
            if (n == '\\') { out += '\\'; ++i; continue; }
        }
        out += c;
    }
    return out;
}

// Stufe 2: In Argumente zerlegen. Trenner sind Leerzeichen; in "..." sind \" \` \$ \\ Escapes.
// Unbekannte Escapes bleiben aus Kulanz wörtlich stehen.
QList<ExecToken> splitDesktopExec(const QString &exec) {
    QList<ExecToken> tokens;
    ExecToken current;
    bool inQuotes = false;
    bool hasToken = false;

    for (qsizetype i = 0; i < exec.size(); ++i) {
        const QChar c = exec.at(i);
        if (inQuotes) {
            if (c == '\\' && i + 1 < exec.size() && QStringLiteral("\"`$\\").contains(exec.at(i + 1))) {
                current.text += exec.at(++i);
            } else if (c == '"') {
                inQuotes = false;
            } else {
                current.text += c;
            }
        } else if (c == ' ' || c == '\t' || c == '\n') {
            if (hasToken) {
                tokens << current;
                current = ExecToken();
                hasToken = false;
            }
        } else if (c == '"') {
            inQuotes = true;
            current.quoted = true;
            hasToken = true;
        } else {
            current.text += c;
            hasToken = true;
        }
    }
    if (hasToken) tokens << current;
    return tokens;
}

// Enthält das Argument einen der Feldcodes aus 'codes' (z.B. "fu")? %% wird korrekt übersprungen.
bool containsFieldCode(const QString &token, const QString &codes) {
    for (qsizetype i = 0; i + 1 < token.size(); ++i) {
        if (token.at(i) != '%') continue;
        if (codes.contains(token.at(i + 1))) return true;
        ++i; // "%%" oder anderer Code -> nächstes Zeichen überspringen
    }
    return false;
}

// Ersetzt die Feldcodes innerhalb eines Arguments. Rückgabe false = Argument entfällt
// (%f/%u ohne Datei, sowie %F/%U/%i, die nur als eigenständiges Argument gültig sind).
bool expandFieldCodes(const QString &token, const QString *file, const DesktopEntry &entry, QString &out) {
    out.clear();
    for (qsizetype i = 0; i < token.size(); ++i) {
        const QChar c = token.at(i);
        if (c != '%' || i + 1 >= token.size()) {
            out += c;
            continue;
        }
        const QChar code = token.at(++i);
        switch (code.unicode()) {
        case '%': out += '%'; break;
        case 'f':
        case 'u':
            if (!file) return false;
            out += *file;
            break;
        case 'c': out += entry.name; break;
        case 'k': out += QFileInfo(entry.path).absoluteFilePath(); break;
        case 'd': case 'D': case 'n': case 'N': case 'v': case 'm':
            break; // veraltet -> entfernen
        case 'F': case 'U': case 'i':
            return false;
        default:
            out += '%';
            out += code;
            break;
        }
    }
    return true;
}
} // namespace

namespace Helpers {
    /*
    bool isTextFile(const QString &filePath) {
        QMimeDatabase db;
        QMimeType mime = db.mimeTypeForFile(filePath);
        return mime.inherits("text/plain");
    }
    */

    QString cleanFileName(const QString &fileName) {
        QString pattern;

        // Linux/Unix verbotene Zeichen: / und der Null-Terminator \0
        pattern = "[/\\x00]";

        static const QRegularExpression re(pattern);

        QString cleanedName = fileName; // local copy
        cleanedName.replace(re, "_");

        return cleanedName;
    }

    // Liefert eine eindeutige Identität des Dateisystem-Objekts (folgt Symlinks), oder einen leeren String bei Fehler.
    // Unix: Gerät + Inode, Windows: Volume-Seriennummer + File-Index.
    QString fileIdentity(const QString &path) {
        if (path.isEmpty()) return QString();

        struct stat st;
        if (::stat(QFile::encodeName(path).constData(), &st) != 0) return QString();
        return QStringLiteral("%1:%2").arg(static_cast<quint64>(st.st_dev)).arg(static_cast<quint64>(st.st_ino));
    }

    // Prüft, ob zwei Pfade auf DASSELBE Dateisystem-Objekt zeigen (folgt Symlinks).
    // Erkennt damit auch Symlink-Pfade, Bind-Mounts und abweichende Groß-/Kleinschreibung
    // auf case-insensitiven Dateisystemen, die ein reiner String-Vergleich übersieht.
    bool isSameFile(const QString &pathA, const QString &pathB) {
        const QString idA = fileIdentity(pathA);
        return !idA.isEmpty() && idA == fileIdentity(pathB);
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

    quint32  calculateCRC32(const QString &filePath) {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly)) {
            qWarning() << "CRC32 fehlgeschlagen: Datei konnte nicht geöffnet werden:" << filePath;
            return 0;
        }

        // Initialize the CRC value
        uLong crc = crc32(0L, Z_NULL, 0);

        // Read in chunks to be memory efficient
        const int bufferSize = 1024 * 1024; // 1 MB buffer
        QByteArray buffer(bufferSize, Qt::Uninitialized);

        while (!file.atEnd()) {
            qint64 bytesRead = file.read(buffer.data(), bufferSize);
            if (bytesRead <= 0) {
                if (bytesRead < 0) {
                    return 0;
                }
                break;
            }
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

    // Opens a file or URL using the system's default application.
    // We deliberately avoid using QDesktopServices directly: in that case, Qt—even outside of
    //  a sandbox—uses xdg-desktop-portal, which ignores the user's configured default for local files
    // (instead presenting a selection dialog, followed by remembering the chosen application).
    // In contrast, xdg-open queries the desktop environment (e.g., using kde-open on Plasma).
    // Returns true if the launch process was initiated.
    // This does not guarantee that xdg-open was actually able to open the file.
    bool openWithSystemDefault(const QUrl &url) {
        if (!url.isValid()) return false;

        const QString xdgOpen = QStandardPaths::findExecutable(QStringLiteral("xdg-open"));
        if (!xdgOpen.isEmpty()) {
            const QString target = url.isLocalFile() ? url.toLocalFile() : url.toString(QUrl::FullyEncoded);
            if (QProcess::startDetached(xdgOpen, { target })) return true;
        }
        return QDesktopServices::openUrl(url);
    }

    void openFileListWithHandler(const QString &handlerApp, const QStringList &fileList) {
        if (handlerApp.endsWith(".desktop", Qt::CaseInsensitive)) {
            launchDesktopFile(getDesktopEntryById(handlerApp), fileList);
            return;
        }

        if (!handlerApp.contains('/') && !handlerApp.contains('\\')) {
            QString localAppPath = QDir(QCoreApplication::applicationDirPath()).filePath(handlerApp);
            if (QFile::exists(localAppPath)) {
                QProcess::startDetached(localAppPath, fileList);
                return;
            }

            QString absolutePath = QStandardPaths::findExecutable(handlerApp);
            if (!absolutePath.isEmpty()) {
                // bool QProcess::startDetached(const QString &program, const QStringList &arguments = {}, const QString &workingDirectory = QString(), qint64 *pid = nullptr)
                QProcess::startDetached(absolutePath, fileList);
                return;
            }
        } else if (QFile::exists(handlerApp)) {
            QProcess::startDetached(handlerApp, fileList);
            return;
        }
    }

    // Wandelt die Exec-Zeile in eine Liste von Aufrufen (Programm + Argumente) um.
    // Mehrere Aufrufe gibt es nur bei %f/%u mit mehreren Dateien (laut Spezifikation einmal pro Datei).
    QList<QStringList> expandDesktopExec(const DesktopEntry &entry, const QStringList &files) {
        const QList<ExecToken> tokens = splitDesktopExec(unescapeDesktopString(entry.exec));
        if (tokens.isEmpty()) return {};

        bool hasListCode = false;   // %F / %U
        bool hasSingleCode = false; // %f / %u
        for (qsizetype t = 1; t < tokens.size(); ++t) {
            const ExecToken &tok = tokens.at(t);
            if (!tok.quoted && (tok.text == "%F" || tok.text == "%U")) {
                hasListCode = true;
            } else if (containsFieldCode(tok.text, QStringLiteral("fu"))) {
                hasSingleCode = true;
            }
        }

        auto buildInvocation = [&](const QString *file) {
            QStringList args;
            args << tokens.first().text; // Programm: keine Feldcodes
            for (qsizetype t = 1; t < tokens.size(); ++t) {
                const ExecToken &tok = tokens.at(t);
                if (!tok.quoted && (tok.text == "%F" || tok.text == "%U")) {
                    args << files;
                    continue;
                }
                if (!tok.quoted && tok.text == "%i") {
                    if (!entry.icon.isEmpty()) args << QStringLiteral("--icon") << entry.icon;
                    continue;
                }
                QString expanded;
                if (!expandFieldCodes(tok.text, file, entry, expanded)) continue;
                if (expanded.isEmpty() && !tok.text.isEmpty() && !tok.quoted) continue; // z.B. nur "%d"
                args << expanded;
            }
            return args;
        };

        QList<QStringList> invocations;
        if (hasListCode) {
            invocations << buildInvocation(files.isEmpty() ? nullptr : &files.first());
        } else if (hasSingleCode) {
            if (files.isEmpty()) {
                invocations << buildInvocation(nullptr);
            } else {
                for (const QString &file : files) invocations << buildInvocation(&file);
            }
        } else {
            // Kein Datei-Feldcode: Dateien anhängen (bisheriges Verhalten, ähnlich wie KDE)
            QStringList args = buildInvocation(nullptr);
            args << files;
            invocations << args;
        }
        return invocations;
    }

    void launchDesktopFile(const DesktopEntry &info, const QStringList &fileList) {
        if (!info.isValid) return;

        // ==========================================
        // 1. Handhabung von Type=Link oder verknüpften URLs
        // ==========================================
        if (info.type.compare(QLatin1String("Link"), Qt::CaseInsensitive) == 0 || !info.url.isEmpty()) {
            QString rawUrl = info.url.trimmed();
            if (rawUrl.isEmpty()) return;

            // 1. Direkt testen, ob es bereits eine valide Netzwerk-URL mit Schema ist (http, https, ftp, etc.)
            QUrl url(rawUrl);
            if (!url.scheme().isEmpty() && url.isValid()) {
                QDesktopServices::openUrl(url);
                return;
            }

            // 2. Erst wenn kein Schema vorliegt, Pfade auflösen (~/, $HOME, relative Pfade)
            QString urlStr = Helpers::expandPath(rawUrl);

            QFileInfo fileInfo(urlStr);
            if (fileInfo.isRelative()) {
                QDir desktopFileDir = QFileInfo(info.path).absoluteDir();
                urlStr = desktopFileDir.absoluteFilePath(urlStr);
            }

            // Lokale Pfade explizit als File-URL konvertieren
            url = QUrl::fromLocalFile(urlStr);
            if (!url.isValid()) {
                url = QUrl::fromUserInput(urlStr);
            }

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

        for (QStringList args : expandDesktopExec(info, fileList)) {
            if (args.isEmpty()) continue;
            const QStringList invocation = args;
            QString program = args.takeFirst();

            program = Helpers::expandPath(program);
            if (!QFileInfo(program).isAbsolute()) {
                program = QStandardPaths::findExecutable(program);
                if (program.isEmpty()) {
                    qWarning() << "launchDesktopFile(): Programm nicht gefunden:" << invocation.first();
                    continue;
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
    }

    void launchRenameTool(const QString &targetDir, const QStringList &pathList, const QString &renameTool, QObject *parent) {
        QString programName = "mkBatchRename";
        QString appDir = QCoreApplication::applicationDirPath();
        QString programPath = QDir(appDir).filePath(programName);

        if (!QFile::exists(programPath)) {
            if (!renameTool.isEmpty() && !targetDir.isEmpty()) {
                Helpers::openFileListWithHandler(renameTool, { targetDir });
            }
            return;
        }

        // 1. Daten in ein JSON-Objekt verpacken
        QJsonObject jsonObj;
        jsonObj["targetDir"] = targetDir;
        jsonObj["pathList"] = QJsonArray::fromStringList(pathList);
        QByteArray jsonData = QJsonDocument(jsonObj).toJson(QJsonDocument::Compact);

        // 2. Einzigartigen Schlüssel erzeugen
        QString memoryKey = "mkBatchRename_" + QUuid::createUuid().toString(QUuid::WithoutBraces);

        // 3. Shared Memory reservieren
        auto *sharedMemory = new QSharedMemory(memoryKey, parent);
        if (!sharedMemory->create(jsonData.size())) {
            qCritical() << "Konnte Shared Memory nicht erstellen:" << sharedMemory->errorString();
            delete sharedMemory; // Objekt im Fehlerfall löschen
            return;
        }

        // 4. Daten sicher in den Shared Memory schreiben
        if (sharedMemory->lock()) {
            char *to = static_cast<char*>(sharedMemory->data());
            memcpy(to, jsonData.constData(), jsonData.size());
            sharedMemory->unlock();
        } else {
            qCritical() << "Konnte Shared Memory nicht sperren:" << sharedMemory->errorString();
            delete sharedMemory; // Objekt im Fehlerfall löschen
            return;
        }

        QStringList arguments;
        arguments << memoryKey;
        arguments << QString::number(jsonData.size());

        qint64 pid = 0;
        bool success = QProcess::startDetached(programPath, arguments, appDir, &pid);

        if (success) {
            qDebug() << "mkBatchRename erfolgreich gestartet. PID:" << pid;

            // Nach 10 Sekunden den Shared Memory freigeben und das Objekt aufräumen
            QTimer::singleShot(10000, sharedMemory, [sharedMemory]() {
                if (sharedMemory->isAttached()) {
                    sharedMemory->detach();
                }
                sharedMemory->deleteLater();
            });
        } else {
            qCritical() << "Fehler beim Starten von mkBatchRename!";
            sharedMemory->detach();
            delete sharedMemory;
        }
    }

    void browseToFile(const QString &path, const QString &fileManager) {
        if (path.isEmpty()) return;

        QFileInfo fileInfo(path);
        if (!fileInfo.exists() && !fileInfo.isSymLink()) return;

        QString targetPath = path;
        QString targetFile = "";
        if (fileInfo.isFile()) {
            targetPath = fileInfo.absoluteDir().path();
            targetFile = path;
        }

        QDir appDir(QCoreApplication::applicationDirPath());
        QString mkFolderWidgetPath = appDir.filePath("mkFolderWidget");

        if (QFile::exists(mkFolderWidgetPath) && (fileManager.isEmpty() || fileManager == "mkFolderWidget")) {
            QStringList args;
            args << targetPath;
            if (!targetFile.isEmpty()) {
                args << "-f" << targetFile;
            }
            QProcess::startDetached(mkFolderWidgetPath, args);
            return;
        }

        if (fileManager.endsWith(".desktop", Qt::CaseInsensitive)) {
            launchDesktopFile(getDesktopEntryById(fileManager), {targetPath});
            return;
        }

        QProcess::startDetached("dbus-send", {
            "--session",
            "--print-reply",
            "--dest=org.freedesktop.FileManager1",
            "/org/freedesktop/FileManager1",
            "org.freedesktop.FileManager1.ShowItems",
            "array:string:" + QUrl::fromLocalFile(path).toString(),
                                "string:\"\""
        });
    }

    /*
    QString getDisplayName(const QFileInfo &fileInfo, bool showFileExtensions) {
        if (showFileExtensions || fileInfo.isDir()) {
            return fileInfo.fileName();
        }

        QString displayName = fileInfo.completeBaseName();
        return displayName.isEmpty() ? fileInfo.fileName() : displayName;
    }
    */

    /*
    QString getDisplayName(const QString &filePath, bool isDir, bool showFileExtensions) {
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
    */

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
        for (const QString &path : pathList) {
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
        extension = ".desktop";

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

            // Linux .desktop Format für Web-Links
            out << "[Desktop Entry]\n";
            out << "Icon=text-html\n";                  // Standard-Icon für HTML/Web-Links
            out << "Name=" << finalAnzeigename << "\n"; // Das ist der Text, den Linux anzeigt
            out << "Type=Link\n";
            out << "URL=" << urlStr << "\n";

            file.close();
        } else {
            qCritical() << "Fehler beim Erstellen der Verknüpfung:" << file.errorString();
        }
    }

    bool isCurrentProcessElevated() {
        return (geteuid() == 0);
    }

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
            pathMatchingString = itemPath;
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
        // Nur reguläre Dateien lesen: FIFOs, Sockets und Gerätedateien (von QDir::System mitgelistet)
        // würden QFile::open() bzw. read() dauerhaft blockieren und damit die ganze GUI einfrieren.
        // isFile() ist auch für Symlinks auf reguläre Dateien true.
        if (!fileInfo.isFile()) {
            return 0;
        }
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
        // Nur reguläre Dateien lesen: FIFOs, Sockets und Gerätedateien (von QDir::System mitgelistet)
        // würden QFile::open() bzw. read() dauerhaft blockieren und damit die ganze GUI einfrieren.
        // isFile() ist auch für Symlinks auf reguläre Dateien true.
        if (!fileInfo.isFile()) {
            return 0;
        }
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

/*
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
*/

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
        parentDir.setFilter({QDir::Dirs, QDir::NoDotAndDotDot});
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

    QString toSentenceCase(const QString &input) {
        if (input.isEmpty()) return input;
        return input.at(0).toUpper() + input.mid(1).toLower();
    }

    QString toTitleCase(const QString &input) {
        if (input.isEmpty()) return input;

        QStringList words = input.split(' ', Qt::SkipEmptyParts);
        for (QString &word : words) {
            word = word.at(0).toUpper() + word.mid(1).toLower();
        }
        return words.join(' ');
    }

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


    void showPopup(const QString &appName, const QString &title, const QString &body) {
        QDBusMessage msg = QDBusMessage::createMethodCall(
            "org.freedesktop.Notifications",
            "/org/freedesktop/Notifications",
            "org.freedesktop.Notifications",
            "Notify"
            );

        QVariantList args;
        args << appName                        // App-Name
             << uint(0)                        // Replaces ID (0 = neu)
             << "dialog-information"           // Icon-Name
             << title                          // Titel
             << body                           // Inhalt
             << QStringList()                  // Aktionen (leer)
             << QVariantMap()                  // Hints (leer)
             << int(5000);                     // Timeout in ms (-1 = standard)

        msg.setArguments(args);
        QDBusConnection::sessionBus().send(msg);
    }

    void showKdePropertiesDialog(const QStringList &filePaths, QWidget *parent = nullptr) {
        if (filePaths.isEmpty()) {
            return;
        }

        // Pfade in QUrl umwandeln
        QList<QUrl> urls;
        urls.reserve(filePaths.size());
        for (const QString &path : filePaths) {
            urls.append(QUrl::fromLocalFile(path));
        }

        // Öffnet den nativen KDE/Dolphin-Eigenschaften-Dialog
        // Kümmert sich automatisch um Einzel- und Mehrfachauswahl!
        KPropertiesDialog::showDialog(urls, parent);
    }

    void openUrlsWithKIO(const QList<QUrl> &urls) {
        if (urls.isEmpty()) return;

        QMimeDatabase mimeDb;

        // Hilfsstruktur für die Gruppierung
        struct AppGroup {
            KService::Ptr service;
            QList<QUrl> urls;
        };

        // Schlüssel ist jetzt QString (storageId, z.B. "org.kde.kate.desktop")
        QHash<QString, AppGroup> appGroups;

        for (const QUrl &url : urls) {
            QMimeType mime = mimeDb.mimeTypeForUrl(url);
            KService::Ptr service = KApplicationTrader::preferredService(mime.name());

            if (service) {
                QString appId = service->storageId(); // Eindeutige App-ID

                if (!appGroups.contains(appId)) {
                    appGroups[appId] = { service, {} };
                }
                appGroups[appId].urls.append(url);
            } else {
                // Fallback für Dateien ohne zugewiesene App
                auto *job = new KIO::OpenUrlJob(url);
                job->start();
            }
        }

        // Jetzt wird genau EIN Aufruf pro Anwendung getätigt
        for (auto it = appGroups.constBegin(); it != appGroups.constEnd(); ++it) {
            KService::Ptr service = it.value().service;
            const QList<QUrl> &groupUrls = it.value().urls;

            qDebug() << "openUrlsWithKIO():";
            qDebug() << "  App-ID:      " << it.key();                    // "org.kde.kate.desktop"
            qDebug() << "  App-Name:    " << service->name();              // "Kate"
            qDebug() << "  Exec-Command: " << service->exec();              // "kate --new %U"
            qDebug() << "  URLs:        " << groupUrls;                     // Enthält jetzt ALLE 3 URLs!

            auto *job = new KIO::ApplicationLauncherJob(service);
            job->setUrls(groupUrls);
            job->start();
        }
    }

    // Opens a file with the application at position 'rank' of the applications associated with its MIME type,
    // sorted by user preference (0 = default application, 1 = 2nd preferred, ...).
    // Returns false if fewer applications are associated.
    bool openFileWithPreferredApp(const QString &path, int rank) {
        QMimeDatabase mimeDb;
        const KService::List services = KApplicationTrader::queryByMimeType(mimeDb.mimeTypeForFile(path).name());
        if (rank < 0 || services.size() <= rank) return false;

        auto *job = new KIO::ApplicationLauncherJob(services.at(rank));
        job->setUrls({QUrl::fromLocalFile(QFileInfo(path).absoluteFilePath())});
        job->start();
        return true;
    }

} // namespace Helpers
