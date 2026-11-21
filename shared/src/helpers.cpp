#include "helpers.h"
#include "settingsmanager.h"

#include <QClipboard>
#include <QCollator>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
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
#include <QUuid>

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

    // Removes invisible/control Unicode characters that crafted web pages can sneak into
    // copied text (zero-width chars, bidi override as in
    // "Trojan Source" attacks, BOM). Real RTL script (Arabic/Hebrew) is
    // NOT affected - those are normal letters, not control characters.
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

    // A single pass over 'input': substituted values are NOT scanned again,
    // so chaining ($FOO expands to a string that contains ${...} again
    // and gets resolved again) is structurally impossible, not just limited.
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

    // Expands a single line purely in memory
    QString expandSingleLine(QString line) {
        line = line.trimmed();
        if (line.isEmpty()) return QString();

        // 1. Remove quotes
        if (line.length() >= 2 &&
            ((line.startsWith('"') && line.endsWith('"')) ||
             (line.startsWith('\'') && line.endsWith('\''))))
        {
            line = line.mid(1, line.length() - 2).trimmed();
            if (line.isEmpty()) return QString();
        }

        // 2. Convert file:// URLs into local paths
        QUrl url(line);
        if (url.isLocalFile()) {
            line = url.toLocalFile();
        }

        // Applies to Linux AND macOS (POSIX semantics are identical)
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

    // Checks pure syntax criteria without file system access
    bool isTechnicallyValidPath(const QString &path) {
        if (path.isEmpty()) return false;

        // After the expansion (~ / $HOME / file://), a valid local path has to be absolute
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
// Split the Exec line according to the Desktop Entry Specification and replace field codes
// https://specifications.freedesktop.org/desktop-entry-spec/latest/exec-variables.html
// ---------------------------------------------------------------------------------------------

struct ExecToken {
    QString text;
    bool quoted = false; // Argument was (completely or partially) in "..."
};

// Stage 1: string escapes that apply to every value of type "string" (\s \n \t \r \\)
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

// Stage 2: split into arguments. Separators are spaces; inside "..." \" \` \$ \\ are escapes.
// Unknown escapes are kept literally as a courtesy.
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

// Does the argument contain one of the field codes from 'codes' (e.g. "fu")? %% is skipped correctly.
bool containsFieldCode(const QString &token, const QString &codes) {
    for (qsizetype i = 0; i + 1 < token.size(); ++i) {
        if (token.at(i) != '%') continue;
        if (codes.contains(token.at(i + 1))) return true;
        ++i; // "%%" or another code -> skip the next character
    }
    return false;
}

// Replaces the field codes within an argument. Returns false = argument is dropped
// (%f/%u without a file, as well as %F/%U/%i, which are only valid as standalone arguments).
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
            break; // obsolete -> remove
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

        // Characters forbidden on Linux/Unix: / and the null terminator \0
        pattern = "[/\\x00]";

        static const QRegularExpression re(pattern);

        QString cleanedName = fileName; // local copy
        cleanedName.replace(re, "_");

        return cleanedName;
    }

    // Returns a unique identity of the file system object (follows symlinks), or an empty string on error.
    // Device + inode.
    QString fileIdentity(const QString &path) {
        if (path.isEmpty()) return QString();

        struct stat st;
        if (::stat(QFile::encodeName(path).constData(), &st) != 0) return QString();
        return QStringLiteral("%1:%2").arg(static_cast<quint64>(st.st_dev)).arg(static_cast<quint64>(st.st_ino));
    }

    // Checks whether two paths point to THE SAME file system object (follows symlinks).
    // This also recognizes symlink paths, bind mounts and differing upper/lower case
    // on case-insensitive file systems, which a pure string comparison misses.
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
            precision = 2; // e.g. 1,23 MiB
        } else if (size < 100.0) {
            precision = 1; // e.g. 12,3 MiB
        } else {
            precision = 0; // e.g. 123 MiB
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
                // URL (catches both "URL=" and "URL[$e]=")
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

    // Turns the Exec line into a list of invocations (program + arguments).
    // There are several invocations only for %f/%u with several files (once per file according to the specification).
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
            args << tokens.first().text; // Program: no field codes
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
                if (expanded.isEmpty() && !tok.text.isEmpty() && !tok.quoted) continue; // e.g. only "%d"
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
            // No file field code: append the files (previous behavior, similar to KDE)
            QStringList args = buildInvocation(nullptr);
            args << files;
            invocations << args;
        }
        return invocations;
    }

    void launchDesktopFile(const DesktopEntry &info, const QStringList &fileList) {
        if (!info.isValid) return;

        // ==========================================
        // 1. Handling of Type=Link or linked URLs
        // ==========================================
        if (info.type.compare(QLatin1String("Link"), Qt::CaseInsensitive) == 0 || !info.url.isEmpty()) {
            QString rawUrl = info.url.trimmed();
            if (rawUrl.isEmpty()) return;

            // 1. Test right away whether it already is a valid network URL with a scheme (http, https, ftp, etc.)
            QUrl url(rawUrl);
            if (!url.scheme().isEmpty() && url.isValid()) {
                QDesktopServices::openUrl(url);
                return;
            }

            // 2. Only if there is no scheme, resolve paths (~/, $HOME, relative paths)
            QString urlStr = Helpers::expandPath(rawUrl);

            QFileInfo fileInfo(urlStr);
            if (fileInfo.isRelative()) {
                QDir desktopFileDir = QFileInfo(info.path).absoluteDir();
                urlStr = desktopFileDir.absoluteFilePath(urlStr);
            }

            // Convert local paths explicitly into file URLs
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
        // 2. Handling of Type=Application (normal Exec start)
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
                // Prioritized list of known Linux terminals (Konsole first because of KDE)
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

                    // Older/lightweight terminals like xterm/alacritty usually need '-e'
                    // Modern desktop terminals (konsole, gnome-terminal) prefer '--'
                    if (terminal.endsWith("gnome-terminal")) {
                        terminalArgs << "--" << program << args;
                    } else {
                        terminalArgs << "-e" << program << args;
                    }

                    // The terminal becomes the main program, the app moves into the arguments
                    program = terminal;
                    args = terminalArgs;
                } else {
                    // Fallback: if no terminal at all was found,
                    // we log a warning and start it normally in the background.
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

    // Starts the helper program 'programName' (next to the running executable) and hands 'jsonData' over to it via
    // shared memory: the program gets the memory key and the data size as arguments. The shared memory is kept for
    // 10 seconds so that the program can attach to it. Returns true if the program was started.
    static bool startWithSharedMemory(const QString &programName, const QByteArray &jsonData, const QString &workingDir, QObject *parent) {
        const QString programPath = QDir(QCoreApplication::applicationDirPath()).filePath(programName);

        // 1. Create a unique key
        const QString memoryKey = programName + "_" + QUuid::createUuid().toString(QUuid::WithoutBraces);

        // 2. Allocate the shared memory
        auto *sharedMemory = new QSharedMemory(memoryKey, parent);
        if (!sharedMemory->create(jsonData.size())) {
            qCritical() << "Konnte Shared Memory nicht erstellen:" << sharedMemory->errorString();
            delete sharedMemory; // Delete the object in case of an error
            return false;
        }

        // 3. Write the data safely into the shared memory
        if (sharedMemory->lock()) {
            char *to = static_cast<char*>(sharedMemory->data());
            memcpy(to, jsonData.constData(), jsonData.size());
            sharedMemory->unlock();
        } else {
            qCritical() << "Konnte Shared Memory nicht sperren:" << sharedMemory->errorString();
            delete sharedMemory; // Delete the object in case of an error
            return false;
        }

        QStringList arguments;
        arguments << memoryKey;
        arguments << QString::number(jsonData.size());

        qint64 pid = 0;
        if (!QProcess::startDetached(programPath, arguments, workingDir, &pid)) {
            qCritical() << "Fehler beim Starten von" << programName;
            sharedMemory->detach();
            delete sharedMemory;
            return false;
        }

        qDebug() << programName << "erfolgreich gestartet. PID:" << pid;

        // After 10 seconds, release the shared memory and clean up the object
        QTimer::singleShot(10000, sharedMemory, [sharedMemory]() {
            if (sharedMemory->isAttached()) {
                sharedMemory->detach();
            }
            sharedMemory->deleteLater();
        });
        return true;
    }

    // Copies, moves, deletes or trashes the given items by handing them over to mkTransactionHandler.
    // Links are created directly. After moving cut items from the clipboard, the clipboard is cleared.
    void startFileOperation(OperationType operationType, const QList<QUrl> &urlList, const QString &targetDir, bool fromClipboard, const QString &workingDir, QObject *parent) {
        if (urlList.isEmpty()) return;

        QFileInfo firstFile(urlList.first().toLocalFile());
        if (firstFile.absolutePath() == targetDir && operationType == OperationType::Move) {
            return;
        }

        if (operationType == OperationType::Link) {
            for (const QUrl &url : urlList) {
                if (!url.isLocalFile()) continue;

                QString targetPath = url.toLocalFile();
                QFileInfo targetInfo(targetPath);

                QString baseName = targetInfo.completeBaseName();
                QString ext = targetInfo.suffix();
                QString dotExt = ext.isEmpty() ? "" : "." + ext;

                QString linkFileName = targetInfo.fileName();

                QDir dir(targetDir);
                QString fullLinkPath = dir.filePath(linkFileName);

                if (QFile::exists(fullLinkPath)) {
                    int counter = 1;
                    while (QFile::exists(fullLinkPath)) {
                        // Creates e.g. "file (1).txt" or "folder (1)" (symlink on Linux)
                        QString numberedName = QString("%1 (%2)%3").arg(baseName).arg(counter).arg(dotExt);
                        fullLinkPath = dir.filePath(numberedName);
                        counter++;
                    }
                }

                if (QFile::link(targetPath, fullLinkPath)) {
                    qDebug() << "Symlink created:" << fullLinkPath << "->Target->" << targetPath;
                } else {
                    qCritical() << "Symlink creation failed:" << targetPath;
                }
            }

            return;
        }

        const QString programName = "mkTransactionHandler";
        if (!QFile::exists(QDir(QCoreApplication::applicationDirPath()).filePath(programName))) {
            return;
        }

        // Pack the data into a JSON object
        QJsonObject jsonObj;
        jsonObj["targetDir"] = targetDir;
        jsonObj["opType"] = static_cast<int>(operationType);

        QJsonArray urlArray;
        for (const QUrl &url : std::as_const(urlList)) {
            urlArray.append(url.toString());
        }
        jsonObj["urls"] = urlArray;

        const QByteArray jsonData = QJsonDocument(jsonObj).toJson(QJsonDocument::Compact);

        if (startWithSharedMemory(programName, jsonData, workingDir, parent)) {
            if (fromClipboard && operationType == OperationType::Move) {
                QGuiApplication::clipboard()->clear();
            }
        }
    }

    void launchRenameTool(const QString &targetDir, const QStringList &pathList, const QString &renameTool, QObject *parent) {
        const QString programName = "mkBatchRename";
        const QString appDir = QCoreApplication::applicationDirPath();

        if (!QFile::exists(QDir(appDir).filePath(programName))) {
            if (!renameTool.isEmpty() && !targetDir.isEmpty()) {
                Helpers::openFileListWithHandler(renameTool, { targetDir });
            }
            return;
        }

        // Pack the data into a JSON object
        QJsonObject jsonObj;
        jsonObj["targetDir"] = targetDir;
        jsonObj["pathList"] = QJsonArray::fromStringList(pathList);
        const QByteArray jsonData = QJsonDocument(jsonObj).toJson(QJsonDocument::Compact);

        startWithSharedMemory(programName, jsonData, appDir, parent);
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

    QPixmap generateThumbnail(const QFileInfo &fileInfo) {
        // Cache the supported formats statically (created only once on the very first call)
        static const QSet<QByteArray> supportedFormats = []() {
            auto formats = QImageReader::supportedImageFormats();
            return QSet<QByteArray>(formats.begin(), formats.end());
        }();

        if (supportedFormats.contains(fileInfo.suffix().toLower().toUtf8())) {
            QImageReader reader(fileInfo.absoluteFilePath());
            reader.setAutoTransform(true); // Important for EXIF rotations from smartphones

            if (reader.canRead()) {
                // 1. Read the real dimensions of the image (costs hardly any performance)
                QSize originalSize = reader.size();

                // 2. Compute a proportional size that fits into a 96x96 box
                // e.g. 1920x1080 automatically becomes 96x54 here
                QSize scaledSize = originalSize.scaled(QSize(96, 96), Qt::KeepAspectRatio);

                // 3. Tell the reader the proportional size
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
        // 1. Determine the base file name (trimming removes leading/trailing whitespace)
        QString fileName = webTitle.trimmed();

        // Fallback A: if data->text() was empty, we use the domain (e.g. "www.wikipedia.org")
        if (fileName.isEmpty()) {
            QUrl url(urlStr);
            fileName = url.host();
        }

        // Fallback B: if the domain fails as well (e.g. invalid URL), a generic name
        if (fileName.isEmpty()) {
            fileName = "Internet-Link";
        }

        // 2. Sanitize the file name
        // Replaces all characters forbidden in Windows/Linux/macOS file systems with an underscore: \ / : * ? " < > |
        fileName.replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");
        fileName = fileName.trimmed();

        // Safety check: in case nothing is left after sanitizing
        if (fileName.isEmpty()) {
            fileName = "Internet-Link";
        }

        const QString extension = ".desktop";

        if (!fileName.endsWith(extension, Qt::CaseInsensitive)) {
            fileName += extension;
        }

        // 3. Check the target directory
        QDir dir(targetDir);
        if (!dir.exists()) {
            qWarning() << "Zielverzeichnis existiert nicht:" << targetDir;
            return;
        }

        // 4. Collision protection: if the file already exists, append a number
        // FIX: use QFileInfo instead of a hard chop(4), since ".desktop" is 8 characters long!
        QString fullPath = dir.filePath(fileName);
        QFileInfo fileInfo(fileName);
        QString baseName = fileInfo.completeBaseName();
        QString finalAnzeigename = baseName; // Remember for the 'Name=' field on Linux

        if (QFile::exists(fullPath)) {
            int counter = 1;
            while (QFile::exists(fullPath)) {
                finalAnzeigename = QString("%1 (%2)").arg(baseName).arg(counter);
                QString numberedName = finalAnzeigename + extension;
                fullPath = dir.filePath(numberedName);
                counter++;
            }
        }

        // 5. Write the file
        QFile file(fullPath);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&file);

            // Linux .desktop format for web links
            out << "[Desktop Entry]\n";
            out << "Icon=text-html\n";                  // Default icon for HTML/web links
            out << "Name=" << finalAnzeigename << "\n"; // This is the text Linux displays
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
        bool bMatchType2 = false; // Word boundary/start in the file name
        bool bMatchType3 = false; // First word at index 0 in the file name
        bool bMatchType4 = false; // At least one word had to fall back to the path
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
        // Only read regular files: FIFOs, sockets and device files (also listed by QDir::System)
        // would block QFile::open() or read() forever and thereby freeze the whole GUI.
        // isFile() is also true for symlinks to regular files.
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

        // Option A: force UTF-8 explicitly (documents the current behavior)
        // in.setEncoding(QStringConverter::Utf8);
        // Option B: decode based on the locale (broader, but slower)
        // in.setEncoding(QStringConverter::System);

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
        // Only read regular files: FIFOs, sockets and device files (also listed by QDir::System)
        // would block QFile::open() or read() forever and thereby freeze the whole GUI.
        // isFile() is also true for symlinks to regular files.
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

    QString getSiblingPath(const QString &path, bool previous) {
        QFileInfo info(path);
        QString currentName = info.fileName();
        QDir parentDir = info.dir();

        // Abort if the parent folder does not exist or the path is invalid
        if (!parentDir.exists() || currentName.isEmpty()) {
            return QString();
        }

        // Only list folders, ignore "." and "..".
        // QDir::Unsorted saves CPU time, since we sort the list ourselves anyway.
        parentDir.setFilter({QDir::Dirs, QDir::NoDotAndDotDot});
        parentDir.setSorting(QDir::Unsorted);

        QStringList siblings = parentDir.entryList();

        // Configure the QCollator
        QCollator collator;
        collator.setNumericMode(true); // "Natural sorting": 2 comes before 10
        collator.setCaseSensitivity(Qt::CaseInsensitive); // Treat a and A the same

        // Sort the QStringList with the QCollator as a functor.
        // QCollator overloads operator(), so std::sort accepts it directly.
        std::sort(siblings.begin(), siblings.end(), collator);

        // Find the position of the current folder in the now naturally sorted list
        int currentIndex = siblings.indexOf(currentName);

        if (currentIndex == -1) {
            return QString();
        }

        // Compute the target index
        int targetIndex = previous ? (currentIndex - 1) : (currentIndex + 1);

        // Check whether we are still within the bounds of the list
        if (targetIndex >= 0 && targetIndex < siblings.size()) {
            return parentDir.absoluteFilePath(siblings.at(targetIndex));
        }

        // Reached the beginning or end of the list -> no sibling available
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

        // Guides/tutorials often end up in the clipboard with line breaks.
        // We take the first line that leads to a valid existing path.
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
             << uint(0)                        // Replaces ID (0 = new)
             << "dialog-information"           // Icon-Name
             << title                          // Title
             << body                           // Content
             << QStringList()                  // Actions (empty)
             << QVariantMap()                  // Hints (leer)
             << int(5000);                     // Timeout in ms (-1 = standard)

        msg.setArguments(args);
        QDBusConnection::sessionBus().send(msg);
    }

    void showKdePropertiesDialog(const QStringList &filePaths, QWidget *parent = nullptr) {
        if (filePaths.isEmpty()) {
            return;
        }

        // Convert paths into QUrl
        QList<QUrl> urls;
        urls.reserve(filePaths.size());
        for (const QString &path : filePaths) {
            urls.append(QUrl::fromLocalFile(path));
        }

        // Opens the native KDE/Dolphin properties dialog
        // Takes care of single and multiple selection automatically!
        KPropertiesDialog::showDialog(urls, parent);
    }

    void openUrlsWithKIO(const QList<QUrl> &urls) {
        if (urls.isEmpty()) return;

        QMimeDatabase mimeDb;

        // Helper struct for grouping
        struct AppGroup {
            KService::Ptr service;
            QList<QUrl> urls;
        };

        // The key is a QString now (storageId, e.g. "org.kde.kate.desktop")
        QHash<QString, AppGroup> appGroups;

        for (const QUrl &url : urls) {
            QMimeType mime = mimeDb.mimeTypeForUrl(url);
            KService::Ptr service = KApplicationTrader::preferredService(mime.name());

            if (service) {
                QString appId = service->storageId(); // Unique app ID

                if (!appGroups.contains(appId)) {
                    appGroups[appId] = { service, {} };
                }
                appGroups[appId].urls.append(url);
            } else {
                // Fallback for files without an assigned app
                auto *job = new KIO::OpenUrlJob(url);
                job->start();
            }
        }

        // Now exactly ONE call per application is made
        for (auto it = appGroups.constBegin(); it != appGroups.constEnd(); ++it) {
            KService::Ptr service = it.value().service;
            const QList<QUrl> &groupUrls = it.value().urls;

            qDebug() << "openUrlsWithKIO():";
            qDebug() << "  App-ID:      " << it.key();                    // "org.kde.kate.desktop"
            qDebug() << "  App-Name:    " << service->name();              // "Kate"
            qDebug() << "  Exec-Command: " << service->exec();              // "kate --new %U"
            qDebug() << "  URLs:        " << groupUrls;                     // Now contains ALL 3 URLs!

            auto *job = new KIO::ApplicationLauncherJob(service);
            job->setUrls(groupUrls);
            job->start();
        }
    }

    OpenMethod openMethod(const QFileInfo &fileInfo, const SettingsManager &settings) {
        const QString fileExt = fileInfo.suffix().toLower();

        if (fileExt == "desktop") {
            return OpenMethod::DesktopEntry;
        }
        // Workaround on linux where executable files are not neccessarily executed when opened via QDesktopServices::openUrl().
        if (fileInfo.isExecutable()
            && !fileInfo.isDir()
            && !settings.audioExts.contains(fileExt)
            && !settings.imageExts.contains(fileExt)
            && !settings.videoExts.contains(fileExt)) {
            return OpenMethod::Execute;
        }
        return OpenMethod::PreferredApp;
    }

    // The application the "Open" action uses for the given files, if it is the same one for all of them (like Dolphin's
    // "Open with <app>"). nullptr for folders, files that are not opened with their preferred application, or if the
    // files need different (or no) applications.
    KService::Ptr openActionService(const QStringList &paths, const SettingsManager &settings) {
        QMimeDatabase mimeDb;
        KService::Ptr commonService;

        for (const QString &path : paths) {
            const QFileInfo fileInfo(path);
            if (fileInfo.isDir() || openMethod(fileInfo, settings) != OpenMethod::PreferredApp) {
                return {};
            }

            // Same MIME detection as openUrlsWithKIO()
            const QMimeType mime = mimeDb.mimeTypeForUrl(QUrl::fromLocalFile(fileInfo.absoluteFilePath()));
            const KService::Ptr service = KApplicationTrader::preferredService(mime.name());
            if (!service || (commonService && service->storageId() != commonService->storageId())) {
                return {};
            }
            commonService = service;
        }
        return commonService;
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
