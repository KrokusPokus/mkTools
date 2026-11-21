#include "fileoperation.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QStack>
#include <QThread>

#include <zlib.h>

#include <cerrno>
#include <sys/stat.h> // stat(), mkfifo(), S_ISFIFO ...
#include <cstdio>     // ::rename()

#include <sys/resource.h> // For getpriority/setpriority (CPU)
#include <sys/syscall.h>
#include <unistd.h>
#include <kio/jobuidelegatefactory.h>

// Since glibc has no headers for this, we define the kernel constants ourselves
#ifndef IOPRIO_WHO_PROCESS
#define IOPRIO_WHO_PROCESS 1
#endif

#ifndef IOPRIO_CLASS_BE
#define IOPRIO_CLASS_BE 2
#endif

#ifndef IOPRIO_PRIO_VALUE
#define IOPRIO_PRIO_VALUE(class, data) (((class) << 13) | (data))
#endif

namespace {

// Special files (Unix only): FIFO, socket, character or block device. They all report size 0 and must
// NEVER be read like a file: open() on a FIFO blocks forever, a device like "zero" delivers
// endless data (the target file grows until the disk is full).
enum class SpecialFileKind { None, Fifo, Other };

SpecialFileKind specialFileKind(const QString &path, unsigned int *permissions = nullptr) {
    struct stat st;
    if (::stat(QFile::encodeName(path).constData(), &st) != 0) return SpecialFileKind::None;
    if (S_ISREG(st.st_mode) || S_ISDIR(st.st_mode)) return SpecialFileKind::None;
    if (permissions) *permissions = st.st_mode & 07777;
    return S_ISFIFO(st.st_mode) ? SpecialFileKind::Fifo : SpecialFileKind::Other;
}

// Returns true if 'path' (does not have to exist) equals 'dir' or lies anywhere below it.
// To do so, it walks up from 'path' and compares every existing ancestor via Helpers::isSameFile().
bool isSameOrInsideDir(const QString &dir, const QString &path) {
    QString current = QDir::cleanPath(QFileInfo(path).absoluteFilePath());

    while (!current.isEmpty()) {
        if (QFileInfo::exists(current) && Helpers::isSameFile(current, dir)) {
            return true;
        }
        const QString parent = QFileInfo(current).path();
        if (parent == current) break; // Reached the root
        current = parent;
    }
    return false;
}

} // namespace

struct LinuxBackgroundPriorityGuard {
    LinuxBackgroundPriorityGuard() {
        m_oldIoPrio = syscall(SYS_ioprio_get, IOPRIO_WHO_PROCESS, 0);

        // Throttle I/O to best effort (class 2) with the lowest priority (level 7)
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

FileOperation::FileOperation(OperationType opType, QList<QUrl> urls, QString targetDir, QObject *parent)
    : QObject(parent)
    , m_urls(std::move(urls))
    , m_targetDir(std::move(targetDir))
    , m_operationType(opType)
    , m_pendingResolution(ConflictResolution::Skip)
{}

void FileOperation::run() {
    LinuxBackgroundPriorityGuard priorityGuard;

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

    m_stats.currentSourceDir = m_sourceDir;
    m_stats.currentTargetDir = m_targetDir;
    m_timer.start();
    updateProgress();

    // PRE-SCAN PASS
    auto [totalBytes, totalFiles] = calculateStats(items);

    m_stats.totalBytes = totalBytes;
    m_stats.totalFiles = totalFiles;

    if (!checkInterruption()) {
        emit wasCanceled(m_stats.filesError, m_failedItems);
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
            // Identity comparison instead of string comparison: recognizes the same folder even via
            // symlinks, bind mounts or differing upper/lower case.
            if (Helpers::isSameFile(srcInfo.absolutePath(), m_targetDir)) {
                if (m_operationType == OperationType::Move) {
                    // Moving into its own folder is a no-op -> skip
                    auto skipStats = std::make_pair(srcInfo.size(), 1);
                    if (srcInfo.isDir() && !srcInfo.isSymbolicLink() && !srcInfo.isJunction()) {
                        skipStats = calculateStats({srcPath});
                    }
                    m_stats.filesSkipped += skipStats.second;
                    m_stats.bytesWritten += skipStats.first;
                    updateProgress();
                    continue;
                }
                dstPath = generateUniqueCopyName(srcInfo, m_targetDir);
            } else {
                dstPath = QDir(m_targetDir).filePath(srcInfo.fileName());
            }

            bool isSymlink = srcInfo.isSymbolicLink() || srcInfo.isJunction();

            // CASE 1: Target does NOT support symlinks -> deep copy fallback
            if (isSymlink && !targetSupportsSymlinks()) {
                QString targetResolved = srcInfo.isJunction() ? srcInfo.junctionTarget() : srcInfo.symLinkTarget();
                QFileInfo resolvedInfo(targetResolved);

                if (!resolvedInfo.exists()) {
                    m_stats.filesError++;
                    m_failedItems.append(FailedItem{srcPath, dstPath, m_operationType, tr("Symlink target does not exist")});
                    result = FileOpResult::Error;
                } else {
                    // For a move, we force a copy of the content so that the source target of the link is not deleted
                    bool forceCopyOnly = (m_operationType == OperationType::Move);

                    if (resolvedInfo.isDir()) {
                        result = copyOrMoveDir(targetResolved, dstPath, isCrossDevice, forceCopyOnly);
                    } else {
                        result = copyOrMoveFile(targetResolved, dstPath, isCrossDevice, forceCopyOnly);
                    }

                    // After the content has been moved successfully, we delete ONLY the link itself
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
            // CASE 2: Target supports symlinks OR normal file/folder
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
                    // Since moveToTrash is extremely fast (OS interface),
                    // we can add the overall dir stats here

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
            QString dstPath = QDir(m_targetDir).filePath(baseName);
            QFileInfo linkDstInfo(dstPath);

            // Conflict check with the new name
            if (linkDstInfo.exists() || linkDstInfo.isSymbolicLink()) {
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
        emit wasCanceled(m_stats.filesError, m_failedItems);
    } else {
        emit wasFinished(m_stats, m_failedItems);
    }

    // <- 'priorityGuard' is destroyed here and the thread mode is reset automatically
}

void FileOperation::doCancel() {
    m_isCancelled.store(true);

    KJob* jobToKill = nullptr;
    {
        // Lock the mutex ONLY for reading the pointer
        QMutexLocker locker(&m_mutex);
        jobToKill = m_currentKioJob;
    } // <-- The mutex is released again RIGHT HERE!

    if (jobToKill) {
        // Now safely triggers the result signal synchronously,
        // which can lock m_mutex again in the lambda without a deadlock.
        jobToKill->kill(KJob::EmitResult);
    }

    // If the thread is sleeping in the pause condition -> wake it up!
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

    // Effective move status: if forceCopyOnly == true (e.g. for a deep copy of a symlink),
    // we treat the operation internally strictly as a copy, so that the source file is NOT deleted!
    const bool isMove = (m_operationType == OperationType::Move) && !forceCopyOnly;

    const bool isSymlinkSource = srcInfo.isSymbolicLink() || srcInfo.isJunction();
    const bool isSymlinkTarget = dstInfo.isSymbolicLink() || dstInfo.isJunction();

    qint64 fileSizeSource = isSymlinkSource ? 0 : srcInfo.size(); // Symlinks don't take up space in the global byte counter

    // Detect special files (symlinks themselves are not - they are treated as links)
    unsigned int specialPermissions = 0;
    const SpecialFileKind specialKind = isSymlinkSource ? SpecialFileKind::None : specialFileKind(src, &specialPermissions);
    const bool isSpecialSource = (specialKind != SpecialFileKind::None);
    const bool canMoveSpecialByRename = isMove && !isCrossDevice;

    // Devices and sockets cannot be copied in a meaningful way (recreating a device only works as root, a socket
    // is worthless without its process). Only moving by renaming on the same drive is possible.
    if (specialKind == SpecialFileKind::Other && !canMoveSpecialByRename) {
        m_stats.filesError++;
        updateProgress();
        m_failedItems.append({src, dst, forceCopyOnly ? OperationType::Copy : m_operationType, tr("Special file (device or socket) cannot be copied, skipped")});
        return FileOpResult::Error;
    }

    // PROTECTION: Source and target are physically the same file (e.g. via a symlink path, bind mount,
    // upper/lower case or a source symlink pointing to the target).
    // Without this protection the comparison would yield "identical" and a move would delete the only copy,
    // or an overwrite would destroy the target (= the source or the link target).
    // If the target itself is a symlink, only the link is replaced -> not critical, so it is excluded.
    if (!isSymlinkTarget && dstInfo.exists() && Helpers::isSameFile(src, dst)) {
        m_stats.filesError++;
        updateProgress();
        m_failedItems.append({src, dst, forceCopyOnly ? OperationType::Copy : m_operationType, tr("Source and target are the same file")});
        return FileOpResult::Error;
    }

    // true = target exists and is to be replaced. The old target is kept until the operation
    // completes successfully and only then swapped out via swapIntoPlace().
    bool replaceExisting = false;

    // There is a conflict if the target exists OR a (possibly broken) symlink is there
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
                // Do NOT delete the folder beforehand: it is only replaced after copying succeeded (see swapIntoPlace)
                replaceExisting = true;
                break;
            }
        }
        else {
            // Shortcut "both 0 bytes" and content comparison ONLY if both sides are regular files:
            // special files always report 0 bytes (but are not "identical"), and a comparison would open a
            // FIFO at the target and thereby block forever. Otherwise -> normal conflict dialog.
            if (!isSymlinkSource && !isSymlinkTarget && srcInfo.isFile() && dstInfo.isFile() && !isSpecialSource) {
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

                // Only compare content for files up to 128 MiB which have the same size.
                // Byte-for-byte comparison instead of CRC32: same I/O cost (both files are read completely
                // either way), but exact - a CRC32 collision must never lead to deleting a move source.
                if (fileSizeSource == fileSizeTarget && fileSizeSource <= 134217728) {
                    auto identical = filesAreIdentical_interruptable(src, dst);
                    if (!identical) {
                        if (!checkInterruption()) return FileOpResult::Cancelled;
                        m_stats.filesError++;
                        updateProgress();
                        m_failedItems.append({src, dst, forceCopyOnly ? OperationType::Copy : m_operationType, tr("Content comparison of source and target failed")});
                        return FileOpResult::Error;
                    }

                    // If the target file has the same content as the source file, just skip.
                    // No point wasting the user's time and attention on a unneccessary MessageBox.
                    if (*identical) {
                        if (isMove) {
                            // MOVE-MODUS: Source needs to be deleted since identical target exists
                            if (QFile::remove(src)) {
                                qDebug() << "Move-source with identical content deleted:" << src;

                                m_stats.filesDuplicate++;
                                m_stats.filesWritten++; // Counts as a successfully processed file
                                m_stats.bytesWritten += fileSizeSource; // Fill up the progress bar
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
                // Do NOT delete the target beforehand: it is only replaced once the new version has been written completely
                replaceExisting = true;
                break;
            }
        }
    }

    bool ok = false;
    bool movedViaRename = false;

    QString failReason;

    if (isSpecialSource) {
        // Special file: never via QFile::rename (silently falls back to copying = reading on EXDEV)
        // and never via copyFileInChunks.
        if (canMoveSpecialByRename && !replaceExisting) {
            ok = (::rename(QFile::encodeName(src).constData(), QFile::encodeName(dst).constData()) == 0);
            movedViaRename = ok;
        }

        if (!ok && specialKind == SpecialFileKind::Fifo) {
            // Recreate the FIFO at the target with the same permissions (like "cp -a"); when overwriting, next to it first
            const QString writePath = replaceExisting ? uniqueSiblingPath(dst, QStringLiteral(".mkpart")) : dst;
            ok = (::mkfifo(QFile::encodeName(writePath).constData(), static_cast<mode_t>(specialPermissions)) == 0);
            if (!ok) {
                failReason = tr("Could not create FIFO at target");
            } else if (replaceExisting && !swapIntoPlace(writePath, dst)) {
                QFile::remove(writePath);
                ok = false;
            }
        } else if (!ok) {
            failReason = tr("Special file (device or socket) cannot be copied, skipped");
        }
    } else if (isMove && !isCrossDevice) {
        // Move on the same drive by simply renaming
        // (when overwriting: the old target is only removed after the rename succeeded)
        ok = replaceExisting ? swapIntoPlace(src, dst) : QFile::rename(src, dst);
        if (ok) {
            movedViaRename = true;
        }
    }

    if (!ok && !isSpecialSource) { // OperationType::Copy OR cross-device move OR forceCopyOnly OR fallback if renaming failed
        // When overwriting, write to a temporary file next to the target first.
        // If copying fails (disk full, read error, abort), the old target stays untouched.
        const QString writePath = replaceExisting ? uniqueSiblingPath(dst, QStringLiteral(".mkpart")) : dst;

        if (isSymlinkSource) {
            ok = QFile::link(srcInfo.symLinkTarget(), writePath);
        } else {
            ok = copyFileInChunks(src, writePath);
        }

        if (ok && replaceExisting && !swapIntoPlace(writePath, dst)) {
            QFile::remove(writePath);
            if (!isSymlinkSource) {
                // copyFileInChunks already counted the file as written -> revert
                m_stats.filesWritten--;
                m_stats.bytesWritten -= fileSizeSource;
            }
            ok = false;
        }
    }

    bool sourceDeleteFailed = false;

    if (ok && isMove && !movedViaRename) {
        if (!QFile::remove(src)) { // Delete the source ONLY for a move and if it has not already been moved by rename()
            // If deleting the source fails, the move operation failed
            qDebug() << "Verschieben unvollständig: Quelldatei konnte nicht gelöscht werden:" << src;
            sourceDeleteFailed = true;
        }
    }

    if (ok && !sourceDeleteFailed) {
        // Note: copyFileInChunks already increases filesWritten/bytesWritten internally!
        // We only increase them here if it was done via rename() or link().
        if (movedViaRename || isSymlinkSource || isSpecialSource) {
            m_stats.bytesWritten += fileSizeSource;
            m_stats.filesWritten++;
        }
    } else {
        if (!checkInterruption()) return FileOpResult::Cancelled;
        m_stats.filesError++;
        updateProgress();

        if (!ok) {
            m_failedItems.append({src, dst, forceCopyOnly ? OperationType::Copy : m_operationType, failReason.isEmpty() ? tr("CopyOrMove failed") : failReason});
        }

        if (sourceDeleteFailed) {
            m_failedItems.append({src, dst, m_operationType, tr("Source deletion after successful move failed")});
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

    // PROTECTION: Target is the source folder itself or lies inside it (e.g. copying /A to /A/B,
    // via paste, drop onto the background or via symlink paths).
    // Without this protection: endless recursion (disk fills up) or data loss for a merge move.
    if (isSameOrInsideDir(src, dst)) {
        m_stats.filesError++;
        updateProgress();
        m_failedItems.append({src, dst, forceCopyOnly ? OperationType::Copy : m_operationType, tr("Cannot copy or move a folder into itself")});
        return FileOpResult::Error;
    }

    // PROTECTION: Symlink loop during a deep copy (target without symlink support).
    // If e.g. /A contains a symlink pointing back to /A (or an ancestor), the recursion would
    // enter the same folder again endlessly. We therefore remember all folders on the CURRENT
    // recursion path. The same folder in separate branches (two links to one target) is still allowed.
    const QString srcDirId = Helpers::fileIdentity(src);
    if (!srcDirId.isEmpty() && m_activeDirIds.contains(srcDirId)) {
        m_stats.filesError++;
        updateProgress();
        m_failedItems.append({src, dst, forceCopyOnly ? OperationType::Copy : m_operationType, tr("Symlink loop detected, folder skipped")});
        return FileOpResult::Error;
    }

    if (!srcDirId.isEmpty()) {
        m_activeDirIds.insert(srcDirId);
    }
    // Remove it from the recursion path again when leaving the function (no matter via which return)
    struct ActiveDirGuard {
        QSet<QString> &set;
        QString id;
        ~ActiveDirGuard() { if (!id.isEmpty()) set.remove(id); }
    } activeDirGuard{m_activeDirIds, srcDirId};

    // When moving a folder on the same drive, we can skip the whole recursion!
    // But ONLY if the target does not exist yet and we are NOT in forceCopyOnly mode!
    if (m_operationType == OperationType::Move && !forceCopyOnly && !isCrossDevice && !dstInfo.exists()) {
        // We have to get the stats BEFORE the rename, since the source path is gone afterwards.
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
        // If target is file OR a symlink/junction: delete and replace with actual directory
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
                // Delete the blocking file so that the folder can be created afterwards
                // (For a symlink, QFile::remove deletes ONLY the link, not the content of the folder it points to.)
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

                m_stats.filesWritten++; // Folder created successfully
                updateProgress();
                break;
            }
        } else {
            // Conflict: target folder already exists → ask
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
                m_stats.filesWritten++; // Folder merged successfully
                updateProgress();
                break; // Merge, continue with the recursion
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
        m_stats.filesWritten++; // Folder created successfully
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

        // --- NEW: SYMLINK ON A TARGET WITHOUT SYMLINK SUPPORT (DEEP COPY FALLBACK) ---
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

                // For a move: if the content was copied, we delete ONLY the symlink itself
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
        // --- YOUR ORIGINAL DISTINCTION (with forceCopyOnly passed on) ---
        else if (entry.isDir() && !isSymlink) {
            r = copyOrMoveDir(entry.filePath(), childDst, isCrossDevice, forceCopyOnly);
        } else {
            r = copyOrMoveFile(entry.filePath(), childDst, isCrossDevice, forceCopyOnly);
        }

        if (r == FileOpResult::Cancelled) return FileOpResult::Cancelled;
        if (r == FileOpResult::Error)     anyErrors = true;
        if (r == FileOpResult::Skipped)   anySkipped = true;
    }

    // For a move: only delete the source folder at the end if no error/skip occurred AND forceCopyOnly is not active
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

        // Recurse ONLY if it is a real directory, NOT a symlink!
        if (entry.isDir() && !isSymLink) {
            FileOpResult r = deleteDirRecursively(entry.filePath());
            if (r == FileOpResult::Cancelled) return FileOpResult::Cancelled;
            if (r == FileOpResult::Error)     anyErrors = true;
        } else {
            // Delete symlinks (no matter if file or folder) and normal files here
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

    // If the content of the folder was emptied successfully,
    // we now delete the (now empty) folder itself
    if (!anyErrors) {
        removeReadOnlyAttribute(dirPath);

        if (QDir().rmdir(dirPath)) {
            m_stats.filesWritten++; // The folder also counts as a processed item
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

    // CAS operation: only if the status REALLY was WaitingForUser
    // does it become SignalSent and we enter the if.
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

// Returns a not yet existing path next to 'path', e.g. "file.txt.mkpart-1a2b3c4d".
QString FileOperation::uniqueSiblingPath(const QString &path, const QString &tag) const {
    while (true) {
        const QString candidate = QStringLiteral("%1%2-%3").arg(path, tag).arg(QRandomGenerator::global()->generate(), 8, 16, QChar('0'));
        const QFileInfo info(candidate);
        if (!info.exists() && !info.isSymbolicLink()) {
            return candidate;
        }
    }
}

// Replaces 'target' with 'replacement' (both in the same folder or on the same drive).
// The old target is first renamed aside and only deleted AFTER the swap succeeded.
// If the swap fails, the old target is restored. 'target' may be a file,
// a symlink or a folder.
bool FileOperation::swapIntoPlace(const QString &replacement, const QString &target) {
    const QFileInfo targetInfo(target);
    if (!targetInfo.exists() && !targetInfo.isSymbolicLink()) {
        return QFile::rename(replacement, target);
    }

    const QString backupPath = uniqueSiblingPath(target, QStringLiteral(".mkbak"));
    if (!QFile::rename(target, backupPath)) {
        qWarning() << "swapIntoPlace(): Ziel konnte nicht beiseite gelegt werden:" << target;
        return false;
    }

    if (!QFile::rename(replacement, target)) {
        qWarning() << "swapIntoPlace(): Ersatz konnte nicht an Zielposition verschoben werden:" << replacement << "->" << target;
        if (!QFile::rename(backupPath, target)) {
            m_stats.filesError++;
            m_failedItems.append({backupPath, target, m_operationType, tr("Could not restore original target from backup")});
        }
        return false;
    }

    // Swap successful -> remove the old target for good
    const QFileInfo backupInfo(backupPath);
    bool removed = false;
    if (backupInfo.isDir() && !backupInfo.isSymbolicLink() && !backupInfo.isJunction()) {
        removed = QDir(backupPath).removeRecursively();
    } else {
        removeReadOnlyAttribute(backupPath);
        removed = QFile::remove(backupPath);
    }

    if (!removed) {
        m_stats.filesError++;
        m_failedItems.append({backupPath, target, m_operationType, tr("Could not remove backup of replaced target")});
    }
    return true;
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

    // Helper lambda to evaluate every FileInfo object in the same way
    auto processInfo = [&](const QFileInfo &info) {
        // --- SYMLINKS & JUNCTIONS ---
        if (info.isSymbolicLink() || info.isJunction()) {
            // Case 1: Delete, trash or create link -> always count only the link object itself
            if (m_operationType == OperationType::Delete ||
                m_operationType == OperationType::Recycle ||
                m_operationType == OperationType::Link)
            {
                totalFiles++;
            }
            // Case 2: Copy or move AND target supports symlinks -> count lean as a link
            else if (targetSupportsSymlinks()) {
                totalFiles++;
            }
            // Case 3: Copy or move AND target does NOT support symlinks -> deep copy (resolve the target)
            else {
                QString targetPath = info.isJunction() ? info.junctionTarget() : info.symLinkTarget();
                QFileInfo targetInfo(targetPath);

                if (targetInfo.exists()) {
                    if (targetInfo.isDir()) {
                        // Directory symlink: just push it onto the stack.
                        // Phase 2 takes care of cycle protection and unfolding!
                        dirStack.push(targetPath);
                    } else {
                        // File symlink: the real target has to be copied -> count size & file
                        totalFiles++;
                        totalBytes += targetInfo.size();
                    }
                }
            }
        }
        // --- NORMAL FOLDERS ---
        else if (info.isDir()) {
            totalFiles++;
            if (m_operationType != OperationType::Link) {
                // Just push it onto the stack. Phase 2 takes care of dedupe/processing.
                dirStack.push(info.absoluteFilePath());
            }
        }
        // --- NORMAL FILES ---
        else {
            totalBytes += info.size();
            totalFiles++;
        }
    };

    // PHASE 1: Process the top-level items (entry point from 'items')
    for (const QString &path : filePaths) {
        if (!checkInterruption()) return {totalBytes, totalFiles};
        processInfo(QFileInfo(path));
    }

    // PHASE 2: Process subfolders in a memory-saving way
    while (!dirStack.isEmpty()) {
        if (!checkInterruption()) return {totalBytes, totalFiles};

        QString currentDir = dirStack.pop();
        QFileInfo dirInfo(currentDir);
        QString canonical = dirInfo.canonicalFilePath();

        // CYCLE PROTECTION & DEDUPLICATION (THE ONLY CENTRAL CHECK)
        // If the path is invalid or we have already entered the canonical target folder
        // before (e.g. through recursive symlinks), skip it.
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
        // 1. Abort check BEFORE the next read/write cycle
        if (!checkInterruption()) {
            success = false;
            break;
        }

        qint64 bytesRead = srcFile.read(buffer.data(), bufferSize);
        if (bytesRead <= 0) {
            if (bytesRead < 0) {
                // REAL READ ERROR! (e.g. read error on the storage device)
                qWarning() << "Lesefehler beim Kopieren von" << src << ":" << srcFile.errorString();
                success = false;
            }
            break; // Clean EOF (0) or read error (-1) -> end the loop
        }

        qint64 bytesWritten = dstFile.write(buffer.data(), bytesRead);

        // 2. Error check while writing (e.g. disk full)
        if (bytesWritten != bytesRead) {
            qWarning() << "Schreibfehler bei" << dst << ":" << dstFile.errorString();
            success = false;
            break;
        }

        // Hand the Qt buffer over to the kernel
        dstFile.flush();

        // Force the kernel to write the chunk to the medium physically RIGHT NOW
        // On Linux, fdatasync is a tad faster than fsync, since it only synchronizes data
        // and no irrelevant metadata/timestamps per chunk!
        // Write errors (EIO, ENOSPC, EDQUOT, USB stick unplugged, network error) often only show up
        // HERE -> then the copy has failed and the source must not be deleted.
        int syncResult;
        do {
            syncResult = ::fdatasync(dstFile.handle());
        } while (syncResult == -1 && errno == EINTR);

        // EINVAL/EROFS: target does not support synchronization -> keep copying without sync as before
        if (syncResult == -1 && errno != EINVAL && errno != EROFS) {
            qWarning() << "Synchronisationsfehler (fdatasync) bei" << dst << ":" << qt_error_string(errno);
            success = false;
            break;
        }

        // Only NOW is the chunk physically written -> update statistics & UI
        m_stats.bytesWritten += bytesWritten;
        updateProgress();
    }

    if (success && checkInterruption()) {
        QFileInfo srcInfo(src);
        dstFile.flush();

        dstFile.setPermissions(srcInfo.permissions());

        dstFile.setFileTime(srcInfo.fileTime(QFileDevice::FileBirthTime), QFileDevice::FileBirthTime);
        dstFile.setFileTime(srcInfo.fileTime(QFileDevice::FileModificationTime), QFileDevice::FileModificationTime);
        dstFile.setFileTime(srcInfo.fileTime(QFileDevice::FileAccessTime), QFileDevice::FileAccessTime);
    }

    // 3. IMPORTANT: Close explicitly first, so that the OS releases the lock!
    srcFile.close();
    dstFile.close();

    // close() can still report a write error (especially on NFS/CIFS). After QFileDevice::close(),
    // error() is NoError exactly when flushing and closing succeeded.
    if (success && dstFile.error() != QFileDevice::NoError) {
        qWarning() << "Fehler beim Schließen von" << dst << ":" << dstFile.errorString();
        success = false;
    }

    // 4. CLEANUP: If aborted or an error occurred, delete the fragment
    if (!success || !checkInterruption()) {
        QFile::remove(dst); // Deletes the incomplete file in the target directory
        return false;
    }

    m_stats.filesWritten++;
    return true;
}

void FileOperation::updateProgress(bool force) {
    m_stats.elapsedMs = m_timer.elapsed();

    // Send the signal ONLY if 40ms have passed OR we force a final update
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
            return fullPath.sliced(cleanSource.length());
        }
    }

    return info.fileName();
}

// Compares two files byte by byte.
// Returns: true = identical, false = different, std::nullopt = read error or abort.
std::optional<bool> FileOperation::filesAreIdentical_interruptable(const QString &fileA, const QString &fileB) {
    QFile a(fileA);
    QFile b(fileB);
    if (!a.open(QIODevice::ReadOnly)) {
        qWarning() << "Vergleich fehlgeschlagen: Datei konnte nicht geöffnet werden:" << fileA;
        return std::nullopt;
    }
    if (!b.open(QIODevice::ReadOnly)) {
        qWarning() << "Vergleich fehlgeschlagen: Datei konnte nicht geöffnet werden:" << fileB;
        return std::nullopt;
    }

    if (a.size() != b.size()) {
        return false;
    }

    // Read in chunks to be memory efficient
    constexpr qint64 bufferSize = 1024 * 1024; // 1 MiB buffer
    QByteArray bufA(bufferSize, Qt::Uninitialized);
    QByteArray bufB(bufferSize, Qt::Uninitialized);

    while (true) {
        if (!checkInterruption()) {
            return std::nullopt;
        }

        const qint64 readA = a.read(bufA.data(), bufferSize);
        const qint64 readB = b.read(bufB.data(), bufferSize);

        if (readA < 0 || readB < 0) {
            return std::nullopt; // Real read error
        }
        if (readA != readB) {
            return false; // File changed while reading or similar
        }
        if (readA == 0) {
            return true; // Both at the end at the same time -> identical
        }
        if (memcmp(bufA.constData(), bufB.constData(), static_cast<size_t>(readA)) != 0) {
            return false;
        }
    }
}

ConflictResolution FileOperation::askUserForResolution(const Conflict &conflict) {
    updateProgress(true);

    if (!checkInterruption()) {
        return ConflictResolution::Cancel;
    }

    // PROTECTION: This function blocks until the GUI thread answers the conflict dialog.
    // If it runs in the GUI thread itself, the dialog can never appear -> deadlock (frozen window).
    // Instead of freezing: report loudly and skip safely (skip changes nothing at the target).
    if (QThread::currentThread() == QCoreApplication::instance()->thread()) {
        qCritical() << "askUserForResolution() im GUI-Thread aufgerufen - Konflikt wird übersprungen:" << conflict.sourcePath << "->" << conflict.targetPath;
        Q_ASSERT_X(false, "FileOperation::askUserForResolution", "must not be called from the GUI thread");
        return ConflictResolution::Skip;
    }

    {
        QMutexLocker locker(&m_mutex);

        // Fast, non-blocking re-check in case doCancel() ran between
        // the checkInterruption() above and here.
        if (m_isCancelled.load()) {
            return ConflictResolution::Cancel;
        }

        m_pendingResolution = ConflictResolution::Skip;
        m_conflictState.store(ConflictState::WaitingForUser);
    }

    emit conflictDetected(conflict);    // The GUI thread handles this via QueuedConnection
    m_semaphore.acquire();              // waits for the GUI

    {
        QMutexLocker locker(&m_mutex);
        m_conflictState.store(ConflictState::Idle);
    }

    // The GUI thread writes m_pendingResolution via resolveConflict()
    // No mutex needed: m_semaphore.acquire() acts as a memory barrier
    // that guarantees that resolveConflict() has completed
    // before we read m_pendingResolution.

    return m_pendingResolution;
}

bool FileOperation::removeReadOnlyAttribute(const QString &path) {
    if (!QFile::exists(path)) return true;

    QFileInfo info(path);
    if (!info.exists()) return true;

    // Read the permissions, add write permission for the owner and set them again
    QFile::Permissions permissions = info.permissions();
    if (!(permissions & QFileDevice::WriteUser)) {
        permissions |= QFileDevice::WriteUser;
        return QFile::setPermissions(path, permissions);
    }
    return true;
}

void FileOperation::doPause() {
    m_isPaused.store(true);

    if (m_currentKioJob) {
        // Pauses the asynchronous KIO job directly
        if (m_currentKioJob->doSuspend()) {
            emit wasPaused();
        }
        return;
    }

    // For non-KIO (worker thread), checkInterruption() sends
    // the wasPaused() signal as soon as the thread goes to sleep.
}

void FileOperation::doContinue() {
    m_isPaused.store(false);

    if (m_currentKioJob) {
        // Resumes the KIO job
        if (m_currentKioJob->doResume()) {
            emit wasContinued();
        }
        return;
    }

    // For non-KIO: wake the worker thread from m_pauseCond.wait()
    m_pauseCond.wakeAll();
}

void FileOperation::doRetry() {
    if (m_failedItems.isEmpty()) {
        return;
    }

    // 1. Copy the old error list over for the new attempt
    QList<FailedItem> itemsToRetry = std::move(m_failedItems);
    m_failedItems.clear(); // Clear it for the new attempt

    // 2. Reset status/flags
    m_isCancelled.store(false);
    m_isPaused.store(false);
    m_applyToAll = false;

    // Do NOT repeat KIO operations (non-local URLs) via the local retry code: there, URL strings
    // would be treated like local paths. Instead, restart the KIO job with the original list;
    // KIO handles files that were already transferred itself via its conflict dialog (e.g. "Skip all").
    if (m_isKioMode) {
        QMetaObject::invokeMethod(this, [this]() {
            startKioAsync();
        }, Qt::QueuedConnection);
        return;
    }

    // 3. Request a targeted retry pass
    // WRONG: run(); -> would block the GUI thread
    // RIGHT: signals the worker thread to execute run() in ITS thread
    QMetaObject::invokeMethod(this, [this, itemsToRetry = std::move(itemsToRetry)]() {
        runRetryList(itemsToRetry);
    }, Qt::QueuedConnection);

}

void FileOperation::runRetryList(const QList<FailedItem> &itemsToRetry) {
    LinuxBackgroundPriorityGuard priorityGuard;

    // 1. RESET & RECALCULATE STATISTICS
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

    // Derive isSameDevice from the first path (if relevant)
    if (!itemsToRetry.isEmpty()) {
        m_stats.isSameDevice = isOnSameDevice(itemsToRetry.first().sourcePath, m_targetDir);
    }

    if (!checkInterruption()) {
        emit wasCanceled(m_stats.filesError, m_failedItems);
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

            // Target does NOT support symlinks -> deep copy fallback on retry
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
        emit wasCanceled(m_stats.filesError, m_failedItems);
    } else {
        emit wasFinished(m_stats, m_failedItems);
    }
}


bool FileOperation::checkInterruption() {
    if (m_isCancelled.load()) {
        return false;
    }

    if (m_isPaused.load()) {
        emit wasPaused();

        QMutexLocker locker(&m_pauseMutex);
        // Go to sleep while pause is active and no abort comes in
        while (m_isPaused.load() && !m_isCancelled.load()) {
            m_pauseCond.wait(&m_pauseMutex);
        }

        if (m_isCancelled.load()) {
            return false;
        }

        emit wasContinued(); // Thread continues running
    }

    return true;
}

bool FileOperation::checkTargetSymlinkSupport(const QString &targetDir) const {
    QDir target(targetDir);
    if (!target.exists()) {
        if (!target.mkpath(".")) {
            return false;
        }
    }

    // Generate unique, temporary names for the test folder and test link
    quint32 randomId = QRandomGenerator::global()->generate();
    qint64 pid = QCoreApplication::applicationPid();

    QString testTargetName = QString(".tmp_symtest_dir_%1_%2").arg(pid).arg(randomId);
    QString testLinkName   = QString(".tmp_symtest_link_%1_%2").arg(pid).arg(randomId);

    QString testTargetAbs = target.filePath(testTargetName);
    QString testLinkAbs   = target.filePath(testLinkName);

    // 1. Create a temporary test folder in the target directory
    if (!QDir().mkdir(testTargetAbs)) {
        return false;
    }

    // 2. Try to create a real symlink to it
    bool supportsSymlinks = QFile::link(testTargetAbs, testLinkAbs);

    // 3. Clean up the traces right away
    if (supportsSymlinks) {
        QFile::remove(testLinkAbs); // Deletes only the symlink item
    }
    QDir().rmdir(testTargetAbs); // Deletes the test folder

    return supportsSymlinks;
}

// Lazy Evaluator!
bool FileOperation::targetSupportsSymlinks() {
    if (!m_targetSupportsSymlinks.has_value()) {
        m_targetSupportsSymlinks = checkTargetSymlinkSupport(m_targetDir);
    }
    return m_targetSupportsSymlinks.value();
}

void FileOperation::startKioAsync() {
    if (m_isCancelled.load()) {
        emit wasCanceled(0, m_failedItems);
        return;
    }

    QUrl destUrl = QUrl::fromLocalFile(m_targetDir);
    KIO::CopyJob *job = nullptr;

    if (m_operationType == OperationType::Copy) job = KIO::copy(m_urls, destUrl);
    else if (m_operationType == OperationType::Move) job = KIO::move(m_urls, destUrl);
    else if (m_operationType == OperationType::Link) job = KIO::link(m_urls, destUrl);

    if (!job) {
        emit wasFinished(m_stats, m_failedItems);
        return;
    }

    job->setWriteIntoExistingDirectories(true);

    m_isKioMode = true;
    m_kioCurrentSource.clear();
    m_kioCurrentTarget.clear();

    // Remember which item KIO is currently transferring - KJob::result() does not tell in case of an error
    auto trackCurrent = [this](const QUrl &from, const QUrl &to) {
        m_kioCurrentSource = from.toDisplayString(QUrl::PreferLocalFile);
        m_kioCurrentTarget = to.toDisplayString(QUrl::PreferLocalFile);
    };
    connect(job, &KIO::CopyJob::copying, this, [trackCurrent](KIO::Job *, const QUrl &from, const QUrl &to) { trackCurrent(from, to); });
    connect(job, &KIO::CopyJob::moving,  this, [trackCurrent](KIO::Job *, const QUrl &from, const QUrl &to) { trackCurrent(from, to); });
    connect(job, &KIO::CopyJob::linking, this, [this](KIO::Job *, const QString &target, const QUrl &to) {
        m_kioCurrentSource = target;
        m_kioCurrentTarget = to.toDisplayString(QUrl::PreferLocalFile);
    });
    // KIO first creates all target folders and copies the files only afterwards. If creating fails,
    // there was no copying() signal yet -> remember the affected target folder (KIO names no source then).
    connect(job, &KIO::CopyJob::creatingDir, this, [this](KIO::Job *, const QUrl &dir) {
        m_kioCurrentSource.clear();
        m_kioCurrentTarget = dir.toDisplayString(QUrl::PreferLocalFile);
    });

    m_currentKioJob = job;
    job->setUiDelegate(KIO::createDefaultJobUiDelegate(KJobUiDelegate::AutoHandlingEnabled, nullptr));

    m_stats = CopyStats();
    m_stats.currentTargetDir = m_targetDir;
    m_stats.totalFiles = m_urls.size();
    m_timer.start();

    connect(job, &KJob::description, this, [this](KJob *, const QString &, const QPair<QString, QString> &field1, const QPair<QString, QString> &field2) {
        m_stats.currentName = field1.second;    // field1.second contains the path of the source file
        updateProgress();
    });

    connect(job, &KJob::totalSize, this, [this](KJob *, qulonglong size) {
        m_stats.totalBytes = size;
        updateProgress();
    });

    connect(job, &KJob::processedSize, this, [this](KJob *, qulonglong size) {
        m_stats.bytesWritten = size;
        updateProgress();
    });

    // Remember which targets this job has verifiably created itself.
    // Only these may be removed again on abort - never files with the same name that existed
    // before (skipped conflicts) or files that were already moved completely.
    m_kioCreatedTargets.clear();

    connect(job, &KIO::CopyJob::copyingDone, this, [this](KIO::Job *, const QUrl &, const QUrl &to, const QDateTime &, bool directory, bool) {
        if (!directory && to.isLocalFile()) {
            m_kioCreatedTargets.append(to.toLocalFile());
        }
    });

    connect(job, &KIO::CopyJob::copyingLinkDone, this, [this](KIO::Job *, const QUrl &, const QString &, const QUrl &to) {
        if (to.isLocalFile()) {
            m_kioCreatedTargets.append(to.toLocalFile());
        }
    });

    connect(job, &KJob::result, this, [this](KJob *kjob) {
        bool cancelled = m_isCancelled.load() || (kjob && kjob->error() == KJob::KilledJobError);

        if (cancelled) {
            // 1. Remove incomplete .part fragments (they are created exclusively by KIO itself)
            for (const QUrl &srcUrl : std::as_const(m_urls)) {
                const QString partFilePath = QDir(m_targetDir).filePath(srcUrl.fileName()) + ".part";
                if (QFile::exists(partFilePath)) {
                    QFile::remove(partFilePath);
                }
            }

            // 2. For copy/link: only roll back the targets completed by THIS job.
            //    NEVER for a move: there the source is already deleted - the target is the only copy!
            if (m_operationType != OperationType::Move) {
                for (const QString &createdPath : std::as_const(m_kioCreatedTargets)) {
                    QFile::remove(createdPath); // For symlinks only the link is removed
                }
            }
            m_kioCreatedTargets.clear();
        } else if (kjob && kjob->error()) {
            m_stats.filesError++;
            // Assign the error to the item KIO processed last. If the error occurs before the
            // first transfer started (e.g. connection already gone at the start), there is no such item:
            // then name a single source directly, or the target folder if there are several sources.
            QString failedSource = m_kioCurrentSource;
            QString failedTarget = m_kioCurrentTarget;
            if (failedSource.isEmpty() && !failedTarget.isEmpty()) {
                failedSource = failedTarget; // Error while creating a target folder: name the folder itself
            } else if (failedSource.isEmpty()) {
                failedSource = (m_urls.size() == 1) ? m_urls.first().toDisplayString(QUrl::PreferLocalFile)
                                                    : m_targetDir;
                failedTarget = m_targetDir;
            }
            m_failedItems.append({failedSource, failedTarget, m_operationType, kjob->errorString()});
        }

        m_currentKioJob = nullptr;

        if (m_isCancelled.load()) {
            emit wasCanceled(m_stats.filesError, m_failedItems);
        } else {
            emit wasFinished(m_stats, m_failedItems);
        }
    });
}
