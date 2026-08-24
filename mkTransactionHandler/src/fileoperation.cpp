#include "fileoperation.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QStack>

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
    auto [totalBytes, totalFiles] = calculateStats(items);

    m_stats.totalBytes = totalBytes;
    m_stats.totalFiles = totalFiles;

    if (!checkInterruption()) {
        emit wasCanceled(m_stats.filesError);
        return;
    }

    const bool isCrossDevice = !items.isEmpty() && !isOnSameDevice(items.first(), m_targetDir);
    m_stats.isSameDevice = !isCrossDevice;

    m_timer.start();

    for (int i = 0; i < items.size(); ++i) {
        if (!checkInterruption()) break;

        FileOpResult result = FileOpResult::Success;
        QString srcPath = items.at(i);
        QFileInfo srcInfo(srcPath);
        m_stats.currentName = calculateRelativeDisplayPath(srcInfo);
        updateProgress();

        if (m_operationType == OperationType::Copy || m_operationType == OperationType::Move) {
            QString dstPath;
            if (srcInfo.absolutePath() == m_targetDir) {
                dstPath = generateUniqueCopyName(srcInfo, m_targetDir);
            } else {
                dstPath = QDir(m_targetDir).filePath(srcInfo.fileName());
            }

            bool isSymlink = srcInfo.isSymbolicLink() || srcInfo.isJunction();

            // FALL 1: Ziel unterstützt KEINE Symlinks -> Deep Copy Fallback
            if (isSymlink && !targetSupportsSymlinks()) {
                QString targetResolved = srcInfo.isJunction() ? srcInfo.junctionTarget() : srcInfo.symLinkTarget();
                QFileInfo resolvedInfo(targetResolved);

                if (!resolvedInfo.exists()) {
                    m_stats.filesError++;
                    m_failedItems.append(FailedItem{srcPath, dstPath, m_operationType, tr("Symlink target does not exist")});
                    result = FileOpResult::Error;
                } else {
                    // Bei Move erzwingen wir für den Inhalt ein Kopieren, damit das Quell-Ziel des Links nicht gelöscht wird
                    bool forceCopyOnly = (m_operationType == OperationType::Move);

                    if (resolvedInfo.isDir()) {
                        result = copyOrMoveDir(targetResolved, dstPath, isCrossDevice, forceCopyOnly);
                    } else {
                        result = copyOrMoveFile(targetResolved, dstPath, isCrossDevice, forceCopyOnly);
                    }

                    // Nach erfolgreichem Verschieben des Inhalts löschen wir NUR die Verknüpfung selbst
                    if (result == FileOpResult::Success && m_operationType == OperationType::Move) {
                        removeReadOnlyAttribute(srcPath);
                        if (!QFile::remove(srcPath)) {
                            m_stats.filesError++;
                            m_failedItems.append(FailedItem{srcPath, dstPath, m_operationType, tr("Could not remove source symlink after move")});
                            result = FileOpResult::Error;
                        }
                    }
                }
            }
            // FALL 2: Ziel unterstützt Symlinks ODER normale Datei/Ordner
            else {
                if (srcInfo.isDir() && !isSymlink) {
                    result = copyOrMoveDir(srcPath, dstPath, isCrossDevice, false);
                } else {
                    result = copyOrMoveFile(srcPath, dstPath, isCrossDevice, false);
                }
            }
        }
        else if (m_operationType == OperationType::Delete || m_operationType == OperationType::Recycle) {
            bool success = false;

            if (m_operationType == OperationType::Recycle) {
                success = QFile::moveToTrash(srcPath);
                if (success) {
                    // Da moveToTrash extrem schnell ist (OS-Schnittstelle),
                    // können wir hier die pauschalen Dir-Stats dazurechnen

                    auto dirStats = std::make_pair(srcInfo.size(), 1);
                    if (srcInfo.isDir() && !srcInfo.isSymbolicLink() && !srcInfo.isJunction()) {
                        dirStats = calculateStats({srcPath});
                    }

                    m_stats.bytesWritten += dirStats.first;
                    m_stats.filesWritten += dirStats.second;
                } else {
                    m_stats.filesError++;
                    m_failedItems.append({srcPath, QString(), m_operationType, tr("moveToTrash() failed")});
                }
                updateProgress();
            } else {
                if (srcInfo.isDir() && !srcInfo.isSymbolicLink() && !srcInfo.isJunction()) {
                    result = deleteDirRecursively(srcPath); // Errors are counted within deleteDirRecursively()
                } else {
                    removeReadOnlyAttribute(srcPath);
                    success = QFile::remove(srcPath);
                    if (success) {
                        m_stats.bytesWritten += srcInfo.size();
                        m_stats.filesWritten++;
                    } else {
                        m_stats.filesError++;
                        m_failedItems.append({srcPath, QString(), m_operationType, tr("File deletion failed")});
                    }
                    updateProgress();
                }
            }
        }
        else if (m_operationType == OperationType::Link) {
            QString baseName = srcInfo.fileName();

#ifdef Q_OS_WIN
            // Unter Windows MÜSSEN Shortcuts zwingend die Dateiendung .lnk haben,
            // damit QFile::link() eine korrekte Shell-Verknüpfung via COM-API erstellt.
            if (!baseName.endsWith(".lnk", Qt::CaseInsensitive)) {
                baseName += ".lnk";
            }
#endif

            QString dstPath = QDir(m_targetDir).filePath(baseName);
            QFileInfo linkDstInfo(dstPath);

            // Konfliktprüfung mit dem neuen Namen (inkl. evtl. .lnk Endung)
            if (linkDstInfo.exists() || linkDstInfo.isSymbolicLink()) {
                // Wir erzeugen eine temporäre QFileInfo für generateUniqueCopyName,
                // damit die "(Copy)"-Endung korrekt VOR dem ".lnk" eingefügt wird.
                dstPath = generateUniqueCopyName(QFileInfo(dstPath), m_targetDir);
            }

            if (QFile::link(srcPath, dstPath)) {
                m_stats.filesWritten++;
            } else {
                m_stats.filesError++;
                qDebug() << "Could not create link for:" << srcPath << "at" << dstPath;
                m_failedItems.append({srcPath, dstPath, m_operationType, tr("Link creation failed")});
            }
            updateProgress();
        }

        if (result == FileOpResult::Cancelled) {
            m_isCancelled.store(true);
            break;
        }
    }

    m_stats.currentName = QString();
    m_stats.currentSourceDir = QString();
    updateProgress(true);

    if (!checkInterruption()) {
        emit wasCanceled(m_stats.filesError);
    } else {
        emit wasFinished(m_stats);
    }

    // <- Hier wird 'priorityGuard' zerstört und der Thread-Modus automatisch zurückgesetzt
}

void FileOperation::doCancel() {
    QMutexLocker locker(&m_mutex);

    m_isCancelled.store(true);

    // Falls der Thread in der Pause-Bedingung schläft -> Aufwecken!
    {
        QMutexLocker pauseLocker(&m_pauseMutex);
        m_pauseCond.wakeAll();
    }

    ConflictState expected = ConflictState::WaitingForUser;
    if (m_conflictState.compare_exchange_strong(expected, ConflictState::SignalSent)) {
        m_pendingResolution = ConflictResolution::Cancel;
        m_semaphore.release();
    }
}

FileOpResult FileOperation::copyOrMoveFile(const QString &src, const QString &dst, bool isCrossDevice, bool forceCopyOnly) {
    QFileInfo srcInfo(src);
    QFileInfo dstInfo(dst);

    m_stats.currentName = calculateRelativeDisplayPath(srcInfo);
    updateProgress();

    // Effektiver Move-Status: Wenn forceCopyOnly == true ist (z.B. bei Deep Copy eines Symlinks),
    // behandeln wir die Operation intern strikt als Copy, damit die Quelldatei NICHT gelöscht wird!
    const bool isMove = (m_operationType == OperationType::Move) && !forceCopyOnly;

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
                    if (!checkInterruption()) return FileOpResult::Cancelled;
                    m_stats.filesError++;
                    updateProgress();
                    m_failedItems.append({src, dst, m_operationType, tr("Target folder deletion failed")});
                    return FileOpResult::Error;
                }
                break;
            }
        }
        else {
            if (!isSymlinkSource && !isSymlinkTarget) {
                qint64 fileSizeTarget = dstInfo.size();

                if (fileSizeSource == 0 && fileSizeTarget == 0) {
                    if (isMove) {
                        if (QFile::remove(src)) {
                            m_stats.filesWritten++;
                            updateProgress();
                            return FileOpResult::Success;
                        } else {
                            if (!checkInterruption()) return FileOpResult::Cancelled;
                            m_stats.filesError++;
                            updateProgress();
                            m_failedItems.append({src, dst, m_operationType, tr("Source deletion during move failed")});
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
                    if (!crcSource) {
                        if (!checkInterruption()) return FileOpResult::Cancelled;
                        m_stats.filesError++;
                        updateProgress();
                        m_failedItems.append({src, dst, forceCopyOnly ? OperationType::Copy : m_operationType, tr("CRC32 calculation failed for source")});
                        return FileOpResult::Error;
                    }

                    auto crcTarget = calculateCRC32(dst);
                    if (!crcTarget) {
                        if (!checkInterruption()) return FileOpResult::Cancelled;
                        m_stats.filesError++;
                        updateProgress();
                        m_failedItems.append({src, dst, forceCopyOnly ? OperationType::Copy : m_operationType, tr("CRC32 calculation failed for target")});
                        return FileOpResult::Error;
                    }

                    // If the target file has the same crc as the source file (so same content), just skip.
                    // No point wasting the user's time and attention on a unneccessary MessageBox.
                    // Regarding "std::optional<quint32>" if both exist, "==" compares the inner quint32.
                    if (crcSource == crcTarget) {
                        if (isMove) {
                            // MOVE-MODUS: Source needs to be deleted since identical target exists
                            if (QFile::remove(src)) {
                                qDebug() << "Move-source with identical CRC deleted:" << src;

                                m_stats.filesDuplicate++;
                                m_stats.filesWritten++; // Zählt als erfolgreich verarbeitete Datei
                                m_stats.bytesWritten += fileSizeSource; // Fortschrittsbalken auffüllen
                                updateProgress();
                                if (!checkInterruption()) return FileOpResult::Cancelled;
                                return FileOpResult::Success;
                            } else {
                                if (!checkInterruption()) return FileOpResult::Cancelled;
                                m_stats.filesError++;
                                updateProgress();
                                m_failedItems.append({src, dst, m_operationType, tr("CRC-Twin source deletion during move failed")});
                                return FileOpResult::Error;
                            }
                        } else {
                            // COPY-MODUS: skip since same file content
                            qDebug() << "Copy target with same context already exists. Skipping:" << src;

                            m_stats.filesDuplicate++;
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
                    if (!checkInterruption()) return FileOpResult::Cancelled;
                    m_stats.filesError++;
                    updateProgress();
                    m_failedItems.append({src, dst, m_operationType, tr("Destination deletion failed")});
                    return FileOpResult::Error;
                }
                break;
            }
        }
    }

    bool ok = false;
    bool movedViaRename = false;

    if (isMove && !isCrossDevice) {
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
    } else { // OperationType::Copy ODER Cross-Device Move ODER forceCopyOnly
        if (isSymlinkSource) {
            ok = QFile::link(srcInfo.symLinkTarget(), dst);
        } else {
            ok = copyFileInChunks(src, dst);
        }
    }

    bool sourceDeleteFailed = false;

    if (ok && isMove && !movedViaRename) {
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
        if (!checkInterruption()) return FileOpResult::Cancelled;
        m_stats.filesError++;
        updateProgress();

        if (!ok) {
            m_failedItems.append({src, dst, forceCopyOnly ? OperationType::Copy : m_operationType, tr("CopyOrMove failed.")});
        }

        if (sourceDeleteFailed) {
            m_failedItems.append({src, dst, m_operationType, tr("Source deletion after successful move failed.")});
        }

        return FileOpResult::Error;
    }

    updateProgress();
    if (!checkInterruption()) return FileOpResult::Cancelled;
    return FileOpResult::Success;
}

FileOpResult FileOperation::copyOrMoveDir(const QString &src, const QString &dst, bool isCrossDevice, bool forceCopyOnly) {
    QFileInfo srcInfo(src);
    QFileInfo dstInfo(dst);

    m_stats.currentName = calculateRelativeDisplayPath(srcInfo);
    updateProgress();

    // Wenn wir einen Ordner auf demselben Laufwerk verschieben, können wir uns die komplette Rekursion sparen!
    // Aber NUR, wenn das Ziel noch nicht existiert und wir NICHT im forceCopyOnly-Modus sind!
    if (m_operationType == OperationType::Move && !forceCopyOnly && !isCrossDevice && !dstInfo.exists()) {
        // Wir müssen die Stats VOR dem Rename holen, weil danach der Quellpfad weg ist.
        auto dirStats = calculateStats({src});

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
                auto dirStats = calculateStats({src});
                m_stats.filesSkipped += dirStats.second;
                m_stats.bytesWritten += dirStats.first;
                updateProgress();
                return FileOpResult::Skipped;
            }
            case ConflictResolution::Cancel:
                return FileOpResult::Cancelled;
            case ConflictResolution::Overwrite:
                // Lösche die blockierende Datei, damit danach der Ordner erstellt werden kann
                // (QFile::remove löscht bei einem Symlink NUR die Verknüpfung, nicht den Inhalt des Ordners, auf den sie zeigt.)
                if (!QFile::remove(dst)) {
                    if (!checkInterruption()) return FileOpResult::Cancelled;
                    m_stats.filesError++;
                    updateProgress();
                    m_failedItems.append({src, dst, forceCopyOnly ? OperationType::Copy : m_operationType, tr("Failed to remove blocking file for directory creation")});
                    return FileOpResult::Error;
                }

                if (!QDir().mkpath(dst)) {
                    if (!checkInterruption()) return FileOpResult::Cancelled;
                    m_stats.filesError++;
                    updateProgress();
                    m_failedItems.append({src, dst, forceCopyOnly ? OperationType::Copy : m_operationType, tr("Failed to create directory")});
                    return FileOpResult::Error;
                }

                m_stats.filesWritten++; // Ordner erfolgreich erstellt
                updateProgress();
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
                auto dirStats = calculateStats({src});
                m_stats.bytesWritten += dirStats.first;
                m_stats.filesSkipped += dirStats.second;
                updateProgress();
                return FileOpResult::Skipped;
            }
            case ConflictResolution::Cancel:
                return FileOpResult::Cancelled;
            case ConflictResolution::Overwrite:
                m_stats.filesWritten++; // Ordner erfolgreich zusammengeführt (Merge)
                updateProgress();
                break; // Zusammenführen, weiter in die Rekursion
            }
        }
    } else { // Destination does not exist as file or directory -> create directory
        if (!QDir().mkpath(dst)) {
            if (!checkInterruption()) return FileOpResult::Cancelled;
            m_stats.filesError++;
            updateProgress();
            m_failedItems.append({src, dst, forceCopyOnly ? OperationType::Copy : m_operationType, tr("Failed to create target directory")});
            return FileOpResult::Error;
        }
        m_stats.filesWritten++; // Ordner erfolgreich erstellt
        updateProgress();
    }

    QDir srcDir(src);
    const QFileInfoList entries = srcDir.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries | QDir::Hidden | QDir::System);

    bool anyErrors = false;
    bool anySkipped = false;

    for (const QFileInfo &entry : entries) {
        if (!checkInterruption()) return FileOpResult::Cancelled;

        QDir dstDir(dst);
        QString childDst = dstDir.filePath(entry.fileName());
        FileOpResult r = FileOpResult::Success;

        bool isSymlink = entry.isSymbolicLink() || entry.isJunction();

        // --- NEU: SYMLINK AUF ZIEL OHNE SYMLINK-SUPPORT (DEEP COPY FALLBACK) ---
        if (isSymlink && !targetSupportsSymlinks()) {
            QString targetResolved = entry.isJunction() ? entry.junctionTarget() : entry.symLinkTarget();
            QFileInfo resolvedInfo(targetResolved);

            if (!resolvedInfo.exists()) {
                m_stats.filesError++;
                m_failedItems.append(FailedItem{ entry.filePath(), childDst, forceCopyOnly ? OperationType::Copy : m_operationType, tr("Symlink target does not exist") });
                r = FileOpResult::Error;
            } else {
                bool childForceCopy = forceCopyOnly || (m_operationType == OperationType::Move);

                if (resolvedInfo.isDir()) {
                    r = copyOrMoveDir(targetResolved, childDst, isCrossDevice, childForceCopy);
                } else {
                    r = copyOrMoveFile(targetResolved, childDst, isCrossDevice, childForceCopy);
                }

                // Bei Move: Wenn der Inhalt kopiert wurde, löschen wir NUR den Symlink selbst
                if (r == FileOpResult::Success && m_operationType == OperationType::Move && !forceCopyOnly) {
                    removeReadOnlyAttribute(entry.filePath());
                    if (!QFile::remove(entry.filePath())) {
                        m_stats.filesError++;
                        m_failedItems.append(FailedItem{ entry.filePath(), childDst, m_operationType, tr("Could not remove source symlink after move") });
                        r = FileOpResult::Error;
                    }
                }
            }
        }
        // --- DEINE ORIGINALE UNTERSCHEIDUNG (mit weitergegebenem forceCopyOnly) ---
        else if (entry.isDir() && !isSymlink) {
            r = copyOrMoveDir(entry.filePath(), childDst, isCrossDevice, forceCopyOnly);
        } else {
            r = copyOrMoveFile(entry.filePath(), childDst, isCrossDevice, forceCopyOnly);
        }

        if (r == FileOpResult::Cancelled) return FileOpResult::Cancelled;
        if (r == FileOpResult::Error)     anyErrors = true;
        if (r == FileOpResult::Skipped)   anySkipped = true;
    }

    // Bei Move: Quellordner am Ende nur löschen, wenn kein Fehler/Skip auftrat UND nicht forceCopyOnly aktiv ist
    if (!anyErrors && !anySkipped && m_operationType == OperationType::Move && !forceCopyOnly) {
        if (!srcDir.removeRecursively()) {
            anyErrors = true;
        }
    }

    if (anyErrors)   return FileOpResult::Error;
    if (anySkipped)  return FileOpResult::Skipped;

    return FileOpResult::Success;
}


FileOpResult FileOperation::deleteDirRecursively(const QString &dirPath) {
    if (!checkInterruption()) return FileOpResult::Cancelled;

    QDir dir(dirPath);
    const QFileInfoList entries = dir.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries | QDir::Hidden | QDir::System);

    bool anyErrors = false;

    for (const QFileInfo &entry : entries) {
        if (!checkInterruption()) return FileOpResult::Cancelled;

        const bool isSymLink = entry.isSymbolicLink() || entry.isJunction();

        // Rekursion NUR wenn es ein echtes Verzeichnis ist, KEIN Symlink!
        if (entry.isDir() && !isSymLink && !entry.isShortcut()) {
            FileOpResult r = deleteDirRecursively(entry.filePath());
            if (r == FileOpResult::Cancelled) return FileOpResult::Cancelled;
            if (r == FileOpResult::Error)     anyErrors = true;
        } else {
            // Symlinks (egal ob Datei oder Ordner) und normale Dateien hier löschen
            m_stats.currentName = calculateRelativeDisplayPath(entry);

            if (!isSymLink) {
                removeReadOnlyAttribute(entry.filePath());
            }

            if (QFile::remove(entry.filePath())) {
                if (!isSymLink) {
                    m_stats.bytesWritten += entry.size();
                }
                m_stats.filesWritten++;
            } else {
                m_stats.filesError++;
                anyErrors = true;
                m_failedItems.append({entry.filePath(), QString(), m_operationType, tr("File deletion failed")});
            }
            updateProgress();
        }
    }

    // Wenn der Inhalt des Ordners erfolgreich geleert wurde,
    // löschen wir jetzt den (nun leeren) Ordner selbst
    if (!anyErrors) {
        removeReadOnlyAttribute(dirPath);

        if (QDir().rmdir(dirPath)) {
            m_stats.filesWritten++; // Auch der Ordner zählt als verarbeitetes Element
            updateProgress();
            return FileOpResult::Success;
        } else {
            m_stats.filesError++;
            updateProgress();
            m_failedItems.append({dirPath, QString(), m_operationType, tr("Empty folder deletion failed")});
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

    while (true) {
        QString suffix = (counter == 1) ? tr(" (Copy)") : QString(tr(" (Copy %1)")).arg(counter);
        QString newPath = QDir(targetDir).filePath(base + suffix + ext);
        counter++;

        QFileInfo info(newPath);
        if (!info.exists() && !info.isSymbolicLink() && !info.isJunction()) {
            return newPath;
        }
    }
}

std::pair<qint64, int> FileOperation::calculateStats(const QStringList &filePaths) {
    qint64 totalBytes = 0;
    int totalFiles = 0;

    QSet<QString> visitedDirs;
    QStack<QString> dirStack;

    // Hilfs-Lambda zur einheitlichen Bewertung jedes FileInfo-Objekts
    auto processInfo = [&](const QFileInfo &info) {
#ifdef Q_OS_WIN
        // --- WINDOWS .LNK SHORTCUTS ---
        if (info.isShortcut()) {
            totalBytes += getLnkSize(info.filePath());
            totalFiles++;
            return;
        }
#endif
        // --- SYMLINKS & JUNCTIONS ---
        if (info.isSymbolicLink() || info.isJunction()) {
            // Case 1: Löschen, Papierkorb oder Link erstellen -> Immer nur das Link-Objekt selbst zählen
            if (m_operationType == OperationType::Delete ||
                m_operationType == OperationType::Recycle ||
                m_operationType == OperationType::Link)
            {
                totalFiles++;
            }
            // Case 2: Copy oder Move UND Ziel unterstützt Symlinks -> schlank als Link zählen
            else if (targetSupportsSymlinks()) {
                totalFiles++;
            }
            // Case 3: Copy oder Move UND Ziel unterstützt KEINE Symlinks -> Deep Copy (Ziel auflösen)
            else {
                QString targetPath = info.isJunction() ? info.junctionTarget() : info.symLinkTarget();
                QFileInfo targetInfo(targetPath);

                if (targetInfo.exists()) {
                    if (targetInfo.isDir()) {
                        // Verzeichnis-Symlink: Einfach auf den Stack legen.
                        // Phase 2 kümmert sich um Zyklenschutz und Entfaltung!
                        dirStack.push(targetPath);
                    } else {
                        // Datei-Symlink: Echtes Ziel muss kopiert werden -> Größe & Datei mitzählen
                        totalFiles++;
                        totalBytes += targetInfo.size();
                    }
                }
            }
        }
        // --- NORMALE ORDNER ---
        else if (info.isDir()) {
            totalFiles++;
            if (m_operationType != OperationType::Link) {
                // Einfach auf den Stack legen. Phase 2 übernimmt Dedupe/Verarbeitung.
                dirStack.push(info.absoluteFilePath());
            }
        }
        // --- NORMALE DATEIEN ---
        else {
            totalBytes += info.size();
            totalFiles++;
        }
    };

    // PHASE 1: Top-Level-Items verarbeiten (Einstiegspunkt aus 'items')
    for (const QString &path : filePaths) {
        if (!checkInterruption()) return {totalBytes, totalFiles};
        processInfo(QFileInfo(path));
    }

    // PHASE 2: Unterordner speicherschonend abarbeiten
    while (!dirStack.isEmpty()) {
        if (!checkInterruption()) return {totalBytes, totalFiles};

        QString currentDir = dirStack.pop();
        QFileInfo dirInfo(currentDir);
        QString canonical = dirInfo.canonicalFilePath();

        // ZYKLENSCHUTZ & DEDUPLIZIERUNG (EINZIGE ZENTRALE PRÜFUNG)
        // Wenn der Pfad ungültig ist oder wir den kanonischen Zielordner
        // schon einmal betreten haben (z. B. durch rekursive Symlinks), überspringen.
        if (canonical.isEmpty() || visitedDirs.contains(canonical)) {
            continue;
        }
        visitedDirs.insert(canonical);

        QDirIterator it(currentDir, QDir::Files | QDir::Dirs | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);

        while (it.hasNext()) {
            if (!checkInterruption()) return {totalBytes, totalFiles};

            it.next();
            processInfo(it.fileInfo());
        }
    }

    return {totalBytes, totalFiles};
}


bool FileOperation::copyFileInChunks(const QString &src, const QString &dst) {
    QFile srcFile(src);
    QFile dstFile(dst);

    if (!srcFile.open(QIODevice::ReadOnly)) return false;
    if (!dstFile.open(QIODevice::WriteOnly | QIODevice::Unbuffered)) return false;

    constexpr qint64 bufferSize = 16 * 1024 * 1024; // 16 MiB
    QByteArray buffer(bufferSize, 0);

    bool success = true;

    while (!srcFile.atEnd()) {
        // 1. Abbruchprüfung VOR dem nächsten Lese/Schreibzyklus
        if (!checkInterruption()) {
            success = false;
            break;
        }

        qint64 bytesRead = srcFile.read(buffer.data(), bufferSize);
        if (bytesRead <= 0) {
            if (bytesRead < 0) {
                // ECHTER LESEFEHLER! (z.B. Lesefehler auf Datenträger)
                qWarning() << "Lesefehler beim Kopieren von" << src << ":" << srcFile.errorString();
                success = false;
            }
            break; // Sauberes EOF (0) oder Lesefehler (-1) -> Schleife beenden
        }

        qint64 bytesWritten = dstFile.write(buffer.data(), bytesRead);

        // 2. Fehlerprüfung beim Schreiben (z.B. Festplatte voll)
        if (bytesWritten != bytesRead) {
            qWarning() << "Schreibfehler bei" << dst << ":" << dstFile.errorString();
            success = false;
            break;
        }

        // Qt-Puffer an Kernel übergeben
        dstFile.flush();

        // Kernel zwingen, den Chunk SOFORT physisch auf das Medium zu schreiben
#if defined(Q_OS_WIN)
        FlushFileBuffers(reinterpret_cast<HANDLE>(dstFile.handle()));
#else
        // fdatasync ist unter Linux einen Tick schneller als fsync, da es nur Daten
        // und keine irrelevanten Metadaten/Timestamps pro Chunk synchronisiert!
        ::fdatasync(dstFile.handle());
#endif

        // Erst JETZT ist der Chunk physikalisch geschrieben -> Statistik & UI aktualisieren
        m_stats.bytesWritten += bytesWritten;
        updateProgress();
    }

    if (success && checkInterruption()) {
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
    if (!success || !checkInterruption()) {
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

    if (!m_sourceDir.isEmpty()) {
        QString cleanSource = QDir::cleanPath(m_sourceDir);
        if (!cleanSource.endsWith('/') && !cleanSource.endsWith('\\')) {
            cleanSource += '/';
        }

        if (fullPath.startsWith(cleanSource, Qt::CaseInsensitive)) {
            return QDir::toNativeSeparators(fullPath.sliced(cleanSource.length()));
        }
    }

    return info.fileName();
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
        if (!checkInterruption()) {
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

    if (!checkInterruption()) {
        return ConflictResolution::Cancel;
    }

    {
        QMutexLocker locker(&m_mutex);

        // Schnelle, nicht-blockierender re-check falls doCancel() zwischen
        // obigem checkInterruption() und hier durchgelaufen ist.
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
    // QFile::exists prüft die Datei selbst (auch kaputte .lnk-Dateien),
    // während QFileInfo::exists() dem Ziel der Verknüpfung folgen würde!
    if (!QFile::exists(path)) return true;

#ifdef Q_OS_WIN
    const wchar_t *wPath = reinterpret_cast<const wchar_t *>(path.utf16());
    DWORD dwAttrs = GetFileAttributesW(wPath);

    if (dwAttrs != INVALID_FILE_ATTRIBUTES && (dwAttrs & FILE_ATTRIBUTE_READONLY)) {
        return SetFileAttributesW(wPath, dwAttrs & ~FILE_ATTRIBUTE_READONLY) != 0;
    }
    return true;
#else
    QFileInfo info(path);
    if (!info.exists()) return true;

    // Permissions auslesen, Schreibrechte für den Besitzer hinzufügen und neu setzen
    QFile::Permissions permissions = info.permissions();
    if (!(permissions & QFileDevice::WriteUser)) {
        permissions |= QFileDevice::WriteUser;
        return QFile::setPermissions(path, permissions);
    }
    return true;
#endif
}

void FileOperation::doPause() {
    m_isPaused.store(true);
}

void FileOperation::doContinue() {
    m_isPaused.store(false);
    m_pauseCond.wakeAll(); // Weckt den wartenden Thread auf
}

void FileOperation::doRetry() {
    if (m_failedItems.isEmpty()) {
        return;
    }

    // 1. Die alte Fehlerliste für den neuen Versuch umkopieren
    QList<FailedItem> itemsToRetry = std::move(m_failedItems);
    m_failedItems.clear(); // Für den neuen Versuch frisch leeren

    // 2. Status/Flags zurücksetzen
    m_isCancelled.store(false);
    m_isPaused.store(false);
    m_applyToAll = false;

    // 3. Einen gezielten Retry-Durchlauf anfordern
    // FALSCH: run(); -> Würde den GUI-Thread blockieren
    // RICHTIG: Signalisiert dem Worker-Thread, run() in SEINEM Thread auszuführen
    QMetaObject::invokeMethod(this, [this, itemsToRetry = std::move(itemsToRetry)]() {
        runRetryList(itemsToRetry);
    }, Qt::QueuedConnection);

}

void FileOperation::runRetryList(const QList<FailedItem> &itemsToRetry) {
#ifdef Q_OS_WIN
    WindowsBackgroundPriorityGuard priorityGuard;
#elif defined(Q_OS_LINUX)
    LinuxBackgroundPriorityGuard priorityGuard;
#endif

    // 1. STATISTIKEN ZURÜCKSETZEN & NEU BERECHNEN
    m_stats = CopyStats();
    m_stats.currentName = tr("Scanning files for retry...");
    m_timer.start();
    updateProgress(true);

    QStringList retryPaths;
    for (const FailedItem &item : itemsToRetry) {
        retryPaths.append(item.sourcePath);
    }

    auto [totalBytes, totalFiles] = calculateStats(retryPaths);
    m_stats.totalBytes = totalBytes;
    m_stats.totalFiles = totalFiles;

    // isSameDevice aus dem ersten Pfad ableiten (falls relevant)
    if (!itemsToRetry.isEmpty()) {
        m_stats.isSameDevice = isOnSameDevice(itemsToRetry.first().sourcePath, m_targetDir);
    }

    if (!checkInterruption()) {
        emit wasCanceled(m_stats.filesError);
        return;
    }

    m_timer.start();

    // 2. RETRY SCHLEIFE
    for (const FailedItem &item : itemsToRetry) {
        if (!checkInterruption()) break;

        FileOpResult result = FileOpResult::Success;
        QFileInfo srcInfo(item.sourcePath);
        m_stats.currentName = calculateRelativeDisplayPath(srcInfo);
        updateProgress();

        switch (item.type) {
        case OperationType::Copy:
        case OperationType::Move: {
            bool isCrossDevice = !isOnSameDevice(item.sourcePath, item.targetPath);
            bool isSymlink = srcInfo.isSymbolicLink() || srcInfo.isJunction();

            // Ziel unterstützt KEINE Symlinks -> Deep Copy Fallback beim Retry
            if (isSymlink && !targetSupportsSymlinks()) {
                QString targetResolved = srcInfo.isJunction() ? srcInfo.junctionTarget() : srcInfo.symLinkTarget();
                QFileInfo resolvedInfo(targetResolved);

                if (!resolvedInfo.exists()) {
                    m_stats.filesError++;
                    m_failedItems.append(FailedItem{item.sourcePath, item.targetPath, item.type, tr("Symlink target does not exist")});
                    result = FileOpResult::Error;
                } else {
                    bool forceCopyOnly = (item.type == OperationType::Move);

                    if (resolvedInfo.isDir()) {
                        result = copyOrMoveDir(targetResolved, item.targetPath, isCrossDevice, forceCopyOnly);
                    } else {
                        result = copyOrMoveFile(targetResolved, item.targetPath, isCrossDevice, forceCopyOnly);
                    }

                    if (result == FileOpResult::Success && item.type == OperationType::Move) {
                        removeReadOnlyAttribute(item.sourcePath);
                        if (!QFile::remove(item.sourcePath)) {
                            m_stats.filesError++;
                            m_failedItems.append(FailedItem{item.sourcePath, item.targetPath, item.type, tr("Could not remove source symlink after move")});
                            result = FileOpResult::Error;
                        }
                    }
                }
            }
            else if (srcInfo.isDir() && !isSymlink) {
                result = copyOrMoveDir(item.sourcePath, item.targetPath, isCrossDevice, false);
            } else {
                result = copyOrMoveFile(item.sourcePath, item.targetPath, isCrossDevice, false);
            }
            break;
        }
        case OperationType::Delete: {
            if (srcInfo.isDir() && !srcInfo.isSymbolicLink() && !srcInfo.isJunction()) {
                result = deleteDirRecursively(item.sourcePath);
            } else {
                removeReadOnlyAttribute(item.sourcePath);
                if (QFile::remove(item.sourcePath)) {
                    if (!srcInfo.isSymbolicLink() && !srcInfo.isJunction()) {
                        m_stats.bytesWritten += srcInfo.size();
                    }
                    m_stats.filesWritten++;
                } else {
                    m_stats.filesError++;
                    m_failedItems.append(item);
                }
            }
            break;
        }
        case OperationType::Recycle: {
            if (QFile::moveToTrash(item.sourcePath)) {
                auto dirStats = std::make_pair(srcInfo.size(), 1);
                if (srcInfo.isDir() && !srcInfo.isSymbolicLink() && !srcInfo.isJunction()) {
                    dirStats = calculateStats({item.sourcePath});
                }
                m_stats.bytesWritten += dirStats.first;
                m_stats.filesWritten += dirStats.second;
            } else {
                m_stats.filesError++;
                m_failedItems.append(item);
            }
            break;
        }
        case OperationType::Link: {
            if (QFile::link(item.sourcePath, item.targetPath)) {
                m_stats.filesWritten++;
            } else {
                m_stats.filesError++;
                m_failedItems.append(item);
            }
            break;
        }
        }

        if (result == FileOpResult::Cancelled) {
            m_isCancelled.store(true);
            break;
        }
    }

    m_stats.currentName = QString();
    updateProgress(true);

    if (!checkInterruption()) {
        emit wasCanceled(m_stats.filesError);
    } else {
        emit wasFinished(m_stats);
    }
}


bool FileOperation::checkInterruption() {
    if (m_isCancelled.load()) {
        return false;
    }

    if (m_isPaused.load()) {
        emit wasPaused();

        QMutexLocker locker(&m_pauseMutex);
        // Schlafen legen, solange Pause aktiv ist und kein Abbruch kommt
        while (m_isPaused.load() && !m_isCancelled.load()) {
            m_pauseCond.wait(&m_pauseMutex);
        }

        if (m_isCancelled.load()) {
            return false;
        }

        emit wasContinued(); // Thread läuft wieder weiter
    }

    return true;
}

bool FileOperation::checkTargetSymlinkSupport(const QString &targetDir) const {
#ifdef Q_OS_WIN
    // Unter Windows erzwingen wir immer eine "Deep Copy" (Inhalte auflösen/kopieren),
    // da Symlinks Rechte (Admin/Developer Mode) erfordern und Explorer-Standard sind.
    return false;
#else
    QDir target(targetDir);
    if (!target.exists()) {
        if (!target.mkpath(".")) {
            return false;
        }
    }

    // Eindeutige, temporäre Namen für Test-Ordner und Test-Link generieren
    quint32 randomId = QRandomGenerator::global()->generate();
    qint64 pid = QCoreApplication::applicationPid();

    QString testTargetName = QString(".tmp_symtest_dir_%1_%2").arg(pid).arg(randomId);
    QString testLinkName   = QString(".tmp_symtest_link_%1_%2").arg(pid).arg(randomId);

    QString testTargetAbs = target.filePath(testTargetName);
    QString testLinkAbs   = target.filePath(testLinkName);

    // 1. Temporären Test-Ordner im Zielverzeichnis anlegen
    if (!QDir().mkdir(testTargetAbs)) {
        return false;
    }

    // 2. Versuchen, einen echten Symlink darauf zu erstellen
    bool supportsSymlinks = QFile::link(testTargetAbs, testLinkAbs);

    // 3. Sofortiges Aufräumen der Spuren
    if (supportsSymlinks) {
        QFile::remove(testLinkAbs); // Löscht nur das Symlink-Element
    }
    QDir().rmdir(testTargetAbs); // Löscht den Test-Ordner

    return supportsSymlinks;
#endif
}

// Lazy Evaluator!
bool FileOperation::targetSupportsSymlinks() {
    if (!m_targetSupportsSymlinks.has_value()) {
        m_targetSupportsSymlinks = checkTargetSymlinkSupport(m_targetDir);
    }
    return m_targetSupportsSymlinks.value();
}
