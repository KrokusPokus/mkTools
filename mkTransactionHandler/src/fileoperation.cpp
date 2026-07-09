#include "fileoperation.h"

#include <QDir>
#include <QFileInfo>

#include <zlib.h>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#elif defined(Q_OS_LINUX)
#include <sys/resource.h> // Für getpriority/setpriority (CPU)
#include <sys/syscall.h>
#include <unistd.h>

// Da glibc keine Header dafür hat, definieren wir die Kernel-Konstanten selbst
#ifndef IOPRIO_WHO_PROCESS
#define IOPRIO_WHO_PROCESS 1
#endif

#ifndef IOPRIO_CLASS_BE
#define IOPRIO_CLASS_BE 2
#endif

#ifndef IOPRIO_PRIO_VALUE
#define IOPRIO_PRIO_VALUE(class, data) (((class) << 13) | (data))
#endif

#endif

#ifdef Q_OS_WIN
// RAII-Klasse: Aktiviert den Hintergrundmodus beim Erstellen
// und deaktiviert ihn automatisch beim Verlassen des Scopes (auch bei Fehlern/Abbrüchen).
struct WindowsBackgroundPriorityGuard {
    WindowsBackgroundPriorityGuard() {
        // Schaltet den aktuellen Thread in den Hintergrundmodus (CPU & I/O)
        m_activated = SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN);
    }

    ~WindowsBackgroundPriorityGuard() {
        if (m_activated) {
            SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_END);
        }
    }
private:
    BOOL m_activated = FALSE;
};
#elif defined(Q_OS_LINUX)
struct LinuxBackgroundPriorityGuard {
    LinuxBackgroundPriorityGuard() {
        m_oldIoPrio = syscall(SYS_ioprio_get, IOPRIO_WHO_PROCESS, 0);

        // I/O auf Best-Effort (Klasse 2) mit niedrigster Prio (Stufe 7) drosseln
        int targetIoPrio = IOPRIO_PRIO_VALUE(IOPRIO_CLASS_BE, 7);
        syscall(SYS_ioprio_set, IOPRIO_WHO_PROCESS, 0, targetIoPrio);
    }

    ~LinuxBackgroundPriorityGuard() {
        if (m_oldIoPrio != -1) {
            syscall(SYS_ioprio_set, IOPRIO_WHO_PROCESS, 0, m_oldIoPrio);
        }
    }
private:
    int m_oldIoPrio = -1;
};
#endif

FileOperation::FileOperation(OperationType opType, QList<QUrl> urls, QString targetDir, QObject *parent)
    : QObject(parent)
    , m_urls(std::move(urls))
    , m_targetDir(std::move(targetDir))
    , m_operationType(opType)
    , m_pendingResolution(ConflictResolution::Skip)
{}

void FileOperation::run() {
#ifdef Q_OS_WIN
    // Ab hier läuft der gesamte Kopiervorgang im Windows-Hintergrundmodus
    WindowsBackgroundPriorityGuard priorityGuard;
#elif defined(Q_OS_LINUX)
    LinuxBackgroundPriorityGuard priorityGuard;
#endif

    m_isCancelled.store(false);

    QList<QString> items;
    for (const QUrl &url : std::as_const(m_urls)) {
        QString local = url.toLocalFile();
        if (!local.isEmpty())
            items.append(local);
    }

    if (!items.isEmpty()) {
        QString firstPath = items.first();
        QFileInfo firstInfo(firstPath);
        m_sourceDir = firstInfo.absolutePath();
    }

    m_stats = CopyStats();
    m_stats.currentName = tr("Scanning files...");

    m_stats.currentSourceDir = QDir::toNativeSeparators(m_sourceDir);
    m_stats.currentTargetDir = QDir::toNativeSeparators(m_targetDir);
    m_timer.start();
    updateProgress();

    // PRE-SCAN PASS
    for (const QString &item : items) {
        if (m_isCancelled.load()) break;

        QFileInfo info(item);
        if (info.isSymbolicLink() || info.isJunction()) {
            m_stats.totalFiles++;
        } else if (info.isDir() && !info.isSymbolicLink() && !info.isJunction()) {
            auto dirStats = getDirStats(item);
            m_stats.totalBytes += dirStats.first;
            m_stats.totalFiles += dirStats.second;
        } else {
            m_stats.totalBytes += info.size();
            m_stats.totalFiles++;
        }
    }

    if (m_isCancelled.load()) {
        emit finished(m_stats.filesError);
        return;
    }

    const bool isCrossDevice = !items.isEmpty() && !isOnSameDevice(items.first(), m_targetDir);
    m_stats.isSameDevice = !isCrossDevice;

    m_timer.start();

    for (int i = 0; i < items.size(); ++i) {
        if (m_isCancelled.load()) break;

        FileOpResult result = FileOpResult::Success;
        QString src = items.at(i);
        QFileInfo srcInfo(src);
        m_stats.currentName = calculateRelativeDisplayPath(srcInfo);
        updateProgress();

        if (m_operationType == OperationType::Copy || m_operationType == OperationType::Move) {
            QString dst;

            // MoveAction into same folder blocked in MainWindow::onFilesDropped(), so we don't need to do anything special here
            if (srcInfo.absolutePath() == m_targetDir) {
                dst = generateUniqueCopyName(srcInfo, m_targetDir);
            } else {
                dst = QDir(m_targetDir).filePath(srcInfo.fileName());
            }

            if (srcInfo.isDir() && !srcInfo.isSymbolicLink() && !srcInfo.isJunction()) {
                result = copyOrMoveDir(src, dst, isCrossDevice);
            }
            else {
                result = copyOrMoveFile(src, dst, isCrossDevice);
            }
        }
        else if (m_operationType == OperationType::Delete || m_operationType == OperationType::Recycle) {
            bool success = false;

            if (m_operationType == OperationType::Recycle) {
                success = QFile::moveToTrash(src);
                if (success) {
                    // Da moveToTrash extrem schnell ist (OS-Schnittstelle),
                    // können wir hier die pauschalen Dir-Stats dazurechnen

                    auto dirStats = std::make_pair(srcInfo.size(), 1);
                    if (srcInfo.isDir() && !srcInfo.isSymbolicLink() && !srcInfo.isJunction()) {
                        dirStats = getDirStats(src);
                    }

                    m_stats.bytesWritten += dirStats.first;
                    m_stats.filesWritten += dirStats.second;
                } else {
                    m_stats.filesError++;
                    qDebug() << "Could not move to trash:" << src;
                }
                updateProgress();
            } else {
                if (srcInfo.isDir() && !srcInfo.isSymbolicLink() && !srcInfo.isJunction()) {
                    result = deleteDirRecursively(src); // Errors are counted within deleteDirRecursively()
                } else {
                    removeReadOnlyAttribute(src);
                    success = QFile::remove(src);
                    if (success) {
                        m_stats.bytesWritten += srcInfo.size();
                        m_stats.filesWritten++;
                    } else {
                        m_stats.filesError++;
                        qDebug() << "Could not delete file:" << src;
                    }
                    updateProgress();
                }
            }
        }

        if (result == FileOpResult::Cancelled) {
            m_isCancelled.store(true);
            break;
        }
    }

    m_stats.currentName = QString();
    m_stats.currentSourceDir = QString();
    updateProgress(true);
    emit finished(m_stats.filesError);

    // <- Hier wird 'priorityGuard' zerstört und der Thread-Modus automatisch zurückgesetzt
}

void FileOperation::cancel() {
    QMutexLocker locker(&m_mutex);

    m_isCancelled.store(true);

    ConflictState expected = ConflictState::WaitingForUser;
    if (m_conflictState.compare_exchange_strong(expected, ConflictState::SignalSent)) {
        m_pendingResolution = ConflictResolution::Cancel;
        m_semaphore.release();
    }
}

FileOpResult FileOperation::copyOrMoveFile(const QString &src, const QString &dst, bool isCrossDevice) {
    QFileInfo srcInfo(src);
    QFileInfo dstInfo(dst);

    m_stats.currentName = calculateRelativeDisplayPath(srcInfo);
    updateProgress();

    const bool isSymlinkSource = srcInfo.isSymbolicLink() || srcInfo.isJunction();
    const bool isSymlinkTarget = dstInfo.isSymbolicLink() || dstInfo.isJunction();

    qint64 fileSizeSource = isSymlinkSource ? 0 : srcInfo.size(); // Symlinks verbrauchen keinen Platz im globalen Byte-Zähler

    // Ein Konflikt besteht, wenn das Ziel existiert ODER ein (ggf. defekter) Symlink dort liegt
    if (dstInfo.exists() || isSymlinkTarget) {
        ConflictResolution resolution = m_applyToAll ? m_applyToAllResolution : ConflictResolution::Skip;

        // Edge case: Source is file, target exists as folder
        if (dstInfo.isDir() && !isSymlinkTarget) {
            if (!m_applyToAll) {
                resolution = askUserForResolution({ src, dst });
            }

            switch (resolution) {
            case ConflictResolution::Skip:
                m_stats.filesSkipped++;
                m_stats.bytesWritten += fileSizeSource;
                updateProgress();
                return FileOpResult::Skipped;
            case ConflictResolution::Cancel:
                return FileOpResult::Cancelled;
            case ConflictResolution::Overwrite:
                if (!QDir(dst).removeRecursively()) {
                    m_stats.filesError++;
                    updateProgress();
                    if (m_isCancelled.load()) return FileOpResult::Cancelled;
                    return FileOpResult::Error;
                }
                break;
            }
        }
        else {
            if (!isSymlinkSource && !isSymlinkTarget) {
                qint64 fileSizeTarget = dstInfo.size();

                if (fileSizeSource == 0 && fileSizeTarget == 0) {
                    if (m_operationType == OperationType::Move) {
                        if (QFile::remove(src)) {
                            m_stats.filesWritten++;
                            updateProgress();
                            return FileOpResult::Success;
                        } else {
                            m_stats.filesError++;
                            updateProgress();
                            if (m_isCancelled.load()) return FileOpResult::Cancelled;
                            return FileOpResult::Error;
                        }
                    } else {
                        m_stats.filesSkipped++;
                        updateProgress();
                        return FileOpResult::Skipped;
                    }
                }

                // Only check crc for files up to 128 MiB which have the same size
                if (fileSizeSource == fileSizeTarget && fileSizeSource <= 134217728) {
                    auto crcSource = calculateCRC32(src);
                    if (!crcSource) { // Wenn crcSource kein Wert ist, gab es einen Abbruch oder Fehler
                        return m_isCancelled.load() ? FileOpResult::Cancelled : FileOpResult::Error;
                    }

                    auto crcTarget = calculateCRC32(dst);
                    if (!crcTarget) {
                        return m_isCancelled.load() ? FileOpResult::Cancelled : FileOpResult::Error;
                    }

                    // If the target file has the same crc as the source file (so same content), just skip.
                    // No point wasting the user's time and attention on a unneccessary MessageBox.
                    // Regarding "std::optional<quint32>" if both exist, "==" compares the inner quint32.
                    if (crcSource == crcTarget) {
                        if (m_operationType == OperationType::Move) {
                            // MOVE-MODUS: Source needs to be deleted since identical target exists
                            if (QFile::remove(src)) {
                                qDebug() << "Move source with same context deleted:" << src;

                                m_stats.filesWritten++; // Zählt als erfolgreich verarbeitete Datei
                                m_stats.bytesWritten += fileSizeSource; // Fortschrittsbalken auffüllen
                                updateProgress();
                                if (m_isCancelled.load()) return FileOpResult::Cancelled;
                                return FileOpResult::Success;
                            } else {
                                qDebug() << "Move source with same context couln't be deleted:" << src;
                                m_stats.filesError++;
                                updateProgress();
                                if (m_isCancelled.load()) return FileOpResult::Cancelled;
                                return FileOpResult::Error;
                            }
                        } else {
                            // COPY-MODUS: skip since same file content
                            qDebug() << "Copy target with same context already exists. Skipping:" << src;

                            m_stats.filesSkipped++;
                            m_stats.bytesWritten += fileSizeSource;
                            updateProgress();
                            return FileOpResult::Skipped;
                        }
                    }
                }
            }

            // Show conflict dialog (broken symlinks also end up here)
            if (!m_applyToAll) {
                resolution = askUserForResolution({ src, dst });
            }

            switch (resolution) {
            case ConflictResolution::Skip:
                m_stats.filesSkipped++;
                m_stats.bytesWritten += fileSizeSource;
                updateProgress();
                return FileOpResult::Skipped;
            case ConflictResolution::Cancel:
                return FileOpResult::Cancelled;
            case ConflictResolution::Overwrite:
                if (!QFile::remove(dst)) {
                    m_stats.filesError++;
                    updateProgress();
                    if (m_isCancelled.load()) return FileOpResult::Cancelled;
                    return FileOpResult::Error;
                }
                break;
            }
        }
    }

    bool ok = false;
    bool movedViaRename = false;

    if (m_operationType == OperationType::Move && !isCrossDevice) {
        ok = QFile::rename(src, dst);           // Verschieben auf demselben Laufwerk durch einfaches Umbenennen
        if (ok) {
            movedViaRename = true;
        } else {                                // Fallback falls Umbenennen fehlschlägt
            if (isSymlinkSource) {
                ok = QFile::link(srcInfo.symLinkTarget(), dst);
            } else {
                ok = copyFileInChunks(src, dst);
            }
        }
    } else { // OperationType::Copy ODER Cross-Device Move
        if (isSymlinkSource) {
            ok = QFile::link(srcInfo.symLinkTarget(), dst);
        } else {
            ok = copyFileInChunks(src, dst);
        }
    }

    bool sourceDeleteFailed = false;

    if (ok && m_operationType == OperationType::Move && !movedViaRename) {
        if (!QFile::remove(src)) { // Löschen der Quelle NUR bei Move und wenn es nicht schon durch rename() verschoben wurde
            // Wenn das Löschen der Quelle fehlschlägt, war die Move-Operation fehlerhaft
            qDebug() << "Verschieben unvollständig: Quelldatei konnte nicht gelöscht werden:" << src;
            sourceDeleteFailed = true;
        }
    }

    if (ok && !sourceDeleteFailed) {
        // Hinweis: copyFileInChunks erhöht filesWritten/bytesWritten bereits intern!
        // Wir erhöhen hier nur, wenn es via rename() oder link() ging.
        if (movedViaRename || isSymlinkSource) {
            m_stats.bytesWritten += fileSizeSource;
            m_stats.filesWritten++;
        }
    } else {
        m_stats.filesError++;
        updateProgress();
        if (m_isCancelled.load()) return FileOpResult::Cancelled;
        return FileOpResult::Error;
    }

    updateProgress();
    if (m_isCancelled.load()) return FileOpResult::Cancelled;
    return FileOpResult::Success;
}

FileOpResult FileOperation::copyOrMoveDir(const QString &src, const QString &dst, bool isCrossDevice) {
    QFileInfo srcInfo(src);
    QFileInfo dstInfo(dst);

    m_stats.currentName = calculateRelativeDisplayPath(srcInfo);
    updateProgress();

    // Wenn wir einen Ordner auf demselben Laufwerk verschieben, können wir uns die komplette Rekursion sparen!
    // Aber NUR, wenn das Ziel noch nicht existiert, sonst müssen wir erst Konflikte prüfen.
    if (m_operationType == OperationType::Move && !isCrossDevice && !dstInfo.exists()) {
        // Wir müssen die Stats VOR dem Rename holen, weil danach der Quellpfad weg ist.
        auto dirStats = getDirStats(src);

        if (QFile::rename(src, dst)) {
            m_stats.bytesWritten += dirStats.first;
            m_stats.filesWritten += dirStats.second;
            updateProgress();
            return FileOpResult::Success;
        }
    }

    if (dstInfo.exists()) {
        ConflictResolution resolution = m_applyToAll ? m_applyToAllResolution : ConflictResolution::Skip;

        // Note: src can at this point only be a directory - anything else is already handled by copyOrMoveFile()
        // If target is file OR ein Symlink/Junction: delete and replace with actual directory
        if (!dstInfo.isDir() || dstInfo.isSymbolicLink() || dstInfo.isJunction()) {
            if (!m_applyToAll) {
                resolution = askUserForResolution({ src, dst });
            }

            switch (resolution) {
            case ConflictResolution::Skip:
            {
                auto dirStats = getDirStats(src);
                m_stats.filesSkipped += dirStats.second;
                m_stats.bytesWritten += dirStats.first;
                updateProgress();
                return FileOpResult::Skipped;
            }
            case ConflictResolution::Cancel:
                return FileOpResult::Cancelled;
            case ConflictResolution::Overwrite:
                // Lösche die blockierende Datei, damit danach der Ordner erstellt werden kann
                // (QFile::remove löscht bei einem Symlink NUR die Verknüpfung,
                // nicht den Inhalt des Ordners, auf den sie zeigt.)
                if (!QFile::remove(dst)) {
                    m_stats.filesError++;
                    return FileOpResult::Error;
                }
                break;
            }
        } else {
            // Konflikt: Zielordner existiert bereits → nachfragen
            if (!m_applyToAll) {
                resolution = askUserForResolution({ src, dst });
            }

            switch (resolution) {
            case ConflictResolution::Skip:
            {
                auto dirStats = getDirStats(src);
                m_stats.bytesWritten += dirStats.first;
                m_stats.filesSkipped += dirStats.second;
                updateProgress();
                return FileOpResult::Skipped;
            }
            case ConflictResolution::Cancel:
                return FileOpResult::Cancelled;
            case ConflictResolution::Overwrite:
                break; // Zusammenführen, weiter in die Rekursion
            }
        }
    } else { // Destination does not exist as file or directory -> create directory
        if (!QDir().mkpath(dst)) return FileOpResult::Error;
    }

    QDir srcDir(src);
    const QFileInfoList entries = srcDir.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries| QDir::Hidden | QDir::System);

    bool anyErrors = false;
    bool anySkipped = false;

    for (const QFileInfo &entry : entries) {
        if (m_isCancelled.load()) return FileOpResult::Cancelled;

        QDir dstDir(dst);
        QString childDst = dstDir.filePath(entry.fileName());
        FileOpResult r = FileOpResult::Success;

        if (entry.isDir() && !entry.isSymbolicLink() && !entry.isJunction()) {
            r = copyOrMoveDir(entry.filePath(), childDst, isCrossDevice);
        } else {
            r = copyOrMoveFile(entry.filePath(), childDst, isCrossDevice);
        }

        if (r == FileOpResult::Cancelled) return FileOpResult::Cancelled;
        if (r == FileOpResult::Error)     anyErrors = true;
        if (r == FileOpResult::Skipped)   anySkipped = true;
    }

    if (!anyErrors && !anySkipped && m_operationType == OperationType::Move) {
        if (!srcDir.removeRecursively()) {
            anyErrors = true;
        }
    }

    if (anyErrors)   return FileOpResult::Error;
    if (anySkipped)  return FileOpResult::Skipped;

    return FileOpResult::Success;
}

FileOpResult FileOperation::deleteDirRecursively(const QString &dirPath) {
    if (m_isCancelled.load()) return FileOpResult::Cancelled;

    QDir dir(dirPath);
    const QFileInfoList entries = dir.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries | QDir::Hidden | QDir::System);

    bool anyErrors = false;

    for (const QFileInfo &entry : entries) {
        if (m_isCancelled.load()) return FileOpResult::Cancelled;

        if (entry.isDir() && !entry.isSymbolicLink() && !entry.isJunction()) {
            FileOpResult r = deleteDirRecursively(entry.filePath());
            if (r == FileOpResult::Cancelled) return FileOpResult::Cancelled;
            if (r == FileOpResult::Error)     anyErrors = true;
        } else {
            m_stats.currentName = calculateRelativeDisplayPath(entry);

            removeReadOnlyAttribute(entry.filePath());

            if (QFile::remove(entry.filePath())) {
                m_stats.bytesWritten += entry.size();
                m_stats.filesWritten++;
            } else {
                m_stats.filesError++;
                anyErrors = true;
                qDebug() << "Could not delete file:" << entry.filePath();
            }
            updateProgress();
        }
    }

    // Wenn der Inhalt des Ordners erfolgreich geleert wurde,
    // löschen wir jetzt den (nun leeren) Ordner selbst
    if (!anyErrors) {
        if (QDir().rmdir(dirPath)) {
            m_stats.filesWritten++; // Auch der Ordner zählt als verarbeitetes Element
            updateProgress();
            return FileOpResult::Success;
        } else {
            m_stats.filesError++;
            updateProgress();
            qDebug() << "Could not remove empty directory:" << dirPath;
            return FileOpResult::Error;
        }
    }

    return FileOpResult::Error;
}

void FileOperation::resolveConflict(ConflictResolution resolution, bool applyToAll) {
    QMutexLocker locker(&m_mutex);

    ConflictState expected = ConflictState::WaitingForUser;

    // CAS-Operation: Nur wenn der Status ECHT WaitingForUser war,
    // wird er zu SignalSent und wir betreten das if.
    if (m_conflictState.compare_exchange_strong(expected, ConflictState::SignalSent)) {
        m_pendingResolution = resolution;
        m_applyToAll = applyToAll;
        if (m_applyToAll == true) {
            m_applyToAllResolution = resolution;
        }
        m_semaphore.release();
    }
}

bool FileOperation::isOnSameDevice(const QString &src, const QString &dst) const {
    QString srcDir = QFileInfo(src).absolutePath();
    QString dstDir = QFileInfo(dst).absolutePath();

    QStorageInfo storageSrc(srcDir);
    QStorageInfo storageDst(dstDir);

    return storageSrc.isValid() && storageSrc.isReady() &&
           storageDst.isValid() && storageDst.isReady() &&
           (storageSrc == storageDst);
}

QString FileOperation::generateUniqueCopyName(const QFileInfo &srcInfo, const QString &targetDir) {
    QString base = srcInfo.completeBaseName();
    QString ext = srcInfo.suffix();
    if (!ext.isEmpty()) {
        ext = "." + ext;
    }

    int counter = 1;
    QString newPath;

    do {
        QString suffix = (counter == 1) ? tr(" (Copy)") : QString(tr(" (Copy %1)")).arg(counter);
        newPath = QDir(targetDir).filePath(base + suffix + ext);
        counter++;
    } while (QFile::exists(newPath));

    return newPath;
}

std::pair<qint64, int> FileOperation::getDirStats(const QString &dirPath) {
    qint64 bytes = 0;
    int files = 0;
    QDir dir(dirPath);
    const QFileInfoList entries = dir.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries| QDir::Hidden | QDir::System);

    for (const QFileInfo &entry : entries) {
        if (m_isCancelled.load()) return {bytes, files};

        if (entry.isDir() && !entry.isSymbolicLink() && !entry.isJunction()) {
            auto sub = getDirStats(entry.filePath());
            bytes += sub.first;
            files += sub.second;
        } else {
            bytes += entry.size();
            files++;
        }
    }
    return {bytes, files};
}

bool FileOperation::copyFileInChunks(const QString &src, const QString &dst) {
    QFile srcFile(src);
    QFile dstFile(dst);

    if (!srcFile.open(QIODevice::ReadOnly)) return false;
    if (!dstFile.open(QIODevice::WriteOnly)) return false;

    constexpr qint64 bufferSize = 4 * 1024 * 1024; // 4 MiB
    QByteArray buffer(bufferSize, 0);

    bool success = true;

    while (!srcFile.atEnd()) {
        // 1. Abbruchprüfung VOR dem nächsten Lese/Schreibzyklus
        if (m_isCancelled.load()) {
            success = false;
            break;
        }

        qint64 bytesRead = srcFile.read(buffer.data(), bufferSize);
        if (bytesRead <= 0) break;

        qint64 bytesWritten = dstFile.write(buffer.data(), bytesRead);

        // 2. Fehlerprüfung beim Schreiben (z.B. Festplatte voll)
        if (bytesWritten != bytesRead) {
            success = false;
            break;
        }

        // Statistiken updaten
        m_stats.bytesWritten += bytesWritten;
        updateProgress();
    }

    if (success && !m_isCancelled.load()) {
        QFileInfo srcInfo(src);
        dstFile.flush();
        dstFile.setFileTime(srcInfo.fileTime(QFileDevice::FileBirthTime), QFileDevice::FileBirthTime);
        dstFile.setFileTime(srcInfo.fileTime(QFileDevice::FileModificationTime), QFileDevice::FileModificationTime);
        dstFile.setFileTime(srcInfo.fileTime(QFileDevice::FileAccessTime), QFileDevice::FileAccessTime);
    }

    // 3. WICHTIG: Erst explizit schließen, damit das OS die Sperre aufhebt!
    srcFile.close();
    dstFile.close();

    // 4. MÜLLBESEITIGUNG: Wenn abgebrochen wurde oder ein Fehler auftrat, Fragment löschen
    if (!success || m_isCancelled.load()) {
        QFile::remove(dst); // Löscht die unvollständige Datei im Zielverzeichnis
        return false;
    }

    m_stats.filesWritten++;
    return true;
}

void FileOperation::updateProgress(bool force) {
    m_stats.elapsedMs = m_timer.elapsed();

    // Sende das Signal NUR, wenn 40ms vergangen sind ODER wir ein finales Update erzwingen
    if (force || !m_progressEmitTimer.isValid() || m_progressEmitTimer.elapsed() >= 40) {
        emit progress(m_stats);
        m_progressEmitTimer.restart();
    }
}

QString FileOperation::calculateRelativeDisplayPath(const QFileInfo &info) {
    QString fullPath = info.absoluteFilePath();

    if (!m_sourceDir.isEmpty() && fullPath.startsWith(m_sourceDir) && fullPath.length() > m_sourceDir.length()) {
        int skipLength = m_sourceDir.length();

        // NUR wenn m_sourceDir NICHT auf einen Slash endet, müssen wir den
        // darauffolgenden Separator manuell mitüberspringen (+1)
        if (!m_sourceDir.endsWith('/') && !m_sourceDir.endsWith('\\')) {
            skipLength += 1;
        }

        return QDir::toNativeSeparators(fullPath.sliced(skipLength));
    }

    return info.fileName(); // Fallback
}

std::optional<quint32> FileOperation::calculateCRC32(const QString &filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "CRC32 fehlgeschlagen: Datei konnte nicht geöffnet werden:" << filePath;
        return std::nullopt;
    }

    // Initialize the CRC value
    uLong crc = crc32(0L, Z_NULL, 0);

    // Read in chunks to be memory efficient
    const int bufferSize = 1024 * 1024; // 1 MB buffer
    QByteArray buffer(bufferSize, Qt::Uninitialized);

    while (!file.atEnd()) {
        if (m_isCancelled.load()) {
            file.close();
            return std::nullopt;
        }

        qint64 bytesRead = file.read(buffer.data(), bufferSize);
        if (bytesRead <= 0) {
            if (bytesRead < 0) {
                return std::nullopt;
            }
            break;
        }
        // Update the CRC with the current chunk
        crc = crc32(crc, reinterpret_cast<const Bytef*>(buffer.data()), bytesRead);
    }

    file.close();
    return static_cast<quint32>(crc);
}

ConflictResolution FileOperation::askUserForResolution(const Conflict &conflict) {
    updateProgress(true);

    {
        QMutexLocker locker(&m_mutex);

        if (m_isCancelled.load()) {
            return ConflictResolution::Cancel;
        }

        m_pendingResolution = ConflictResolution::Skip;
        m_conflictState.store(ConflictState::WaitingForUser);
    }

    emit conflictDetected(conflict);    // GUI-Thread verarbeitet das via QueuedConnection
    m_semaphore.acquire();              // wartet auf GUI

    {
        QMutexLocker locker(&m_mutex);
        m_conflictState.store(ConflictState::Idle);
    }

    // GUI-Thread schreibt m_pendingResolution via resolveConflict()
    // Kein Mutex nötig: m_semaphore.acquire() fungiert als Memory-Barrier,
    // die garantiert, dass resolveConflict() vollständig abgeschlossen ist
    // bevor wir m_pendingResolution lesen.

    return m_pendingResolution;
}

bool FileOperation::removeReadOnlyAttribute(const QString &path) {
    QFileInfo info(path);
    if (!info.exists()) return true;

    // Permissions auslesen, Schreibrechte für den Besitzer hinzufügen und neu setzen
    QFile::Permissions permissions = info.permissions();
    if (!(permissions & QFileDevice::WriteUser)) {
        permissions |= QFileDevice::WriteUser;
        return QFile::setPermissions(path, permissions);
    }
    return true;
}
