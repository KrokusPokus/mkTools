#ifndef FILEOPERATION_H
#define FILEOPERATION_H

#include "helpers.h"

#include <QElapsedTimer>
#include <QList>
#include <QMutex>
#include <QObject>
#include <QSemaphore>
#include <QSet>
#include <QStorageInfo>
#include <QString>
#include <QUrl>
#include <QWaitCondition>
#include <Qt>

#include <atomic>
#include <optional>

// KDE KIO framework (Linux-specific)
#include <KIO/CopyJob>
#include <KIO/JobUiDelegate>
#include <KJob>

struct CopyStats {
    qint64 totalBytes = 0;
    qint64 bytesWritten = 0;
    int totalFiles = 0;
    int filesWritten = 0;
    int filesSkipped = 0;
    int filesError = 0;
    int filesDuplicate = 0;
    qint64 elapsedMs = 0;
    QString currentName;       // %relative_path%
    QString currentSourceDir;  // %source_folder%
    QString currentTargetDir;  // %target_folder%
    bool isSameDevice = false; // For "Same/Different Drive"
};

struct Conflict {
    QString sourcePath;
    QString targetPath;;
};

struct FailedItem {
    QString sourcePath;
    QString targetPath;
    OperationType type;
    QString errorReason;
};

Q_DECLARE_METATYPE(FailedItem)
Q_DECLARE_METATYPE(QList<FailedItem>)

enum class FileOpResult { Success, Skipped, Error, Cancelled};

enum class ConflictResolution { Overwrite, Skip, Cancel };

class FileOperation : public QObject {
    Q_OBJECT
public:
    explicit FileOperation(OperationType opType,
                           QList<QUrl> urls,
                           QString targetDir = QString(),
                           QObject *parent = nullptr);
    void startKioAsync();

signals:
    void progress(const CopyStats &stats);
    void conflictDetected(const Conflict &conflict);  // blocking via Qt::BlockingQueuedConnection
    void wasContinued();
    void wasPaused();
    void wasFinished(const CopyStats &stats, const QList<FailedItem> &failedItems);
    void wasCanceled(int filesError, const QList<FailedItem> &failedItems);

public slots:
    void run();
    void doContinue();
    void doPause();
    void doRetry();
    void doCancel();
    void resolveConflict(ConflictResolution resolution, bool applyToAll);

private:
    FileOpResult copyOrMoveFile(const QString &src, const QString &dst, bool isCrossDevice, bool forceCopyOnly);
    FileOpResult copyOrMoveDir(const QString &src, const QString &dst, bool isCrossDevice, bool forceCopyOnly);
    FileOpResult deleteDirRecursively(const QString &dirPath);
    QString calculateRelativeDisplayPath(const QFileInfo &info);
    bool isOnSameDevice(const QString &src, const QString &dst) const;
    QString generateUniqueCopyName(const QFileInfo &srcInfo, const QString &targetDir);
    QString uniqueSiblingPath(const QString &path, const QString &tag) const;
    bool swapIntoPlace(const QString &replacement, const QString &target);
    std::pair<qint64, int> calculateStats(const QStringList &filePaths);
    bool copyFileInChunks(const QString &src, const QString &dst);
    void updateProgress(bool force = false);
    std::optional<bool> filesAreIdentical_interruptable(const QString &fileA, const QString &fileB);
    ConflictResolution askUserForResolution(const Conflict &conflict);
    bool removeReadOnlyAttribute(const QString &path);
    void runRetryList(const QList<FailedItem> &itemsToRetry);
    bool checkTargetSymlinkSupport(const QString &targetDir) const;
    bool targetSupportsSymlinks(); // Lazy Evaluator

    KIO::CopyJob *m_currentKioJob = nullptr;
    QStringList m_kioCreatedTargets; // Target files/links verifiably created by the KIO job (for cleanup on abort)
    bool m_isKioMode = false;        // Operation runs via KIO (GUI thread) instead of the local worker thread
    QString m_kioCurrentSource;      // Item KIO is currently transferring (for correct error assignment)
    QString m_kioCurrentTarget;

    CopyStats m_stats;
    QElapsedTimer m_timer;
    QElapsedTimer m_progressEmitTimer;

    QList<QUrl> m_urls;
    QString m_sourceDir;
    QString m_targetDir;
    OperationType m_operationType;
    QList<FailedItem> m_failedItems;
    mutable std::optional<bool> m_targetSupportsSymlinks;
    QSet<QString> m_activeDirIds; // Identities of the folders on the current copyOrMoveDir recursion path (loop protection)

    bool m_applyToAll = false;
    ConflictResolution m_pendingResolution;
    ConflictResolution m_applyToAllResolution = ConflictResolution::Skip;
    QSemaphore m_semaphore;
    QMutex m_mutex;

    std::atomic<bool> m_isPaused{false};
    QMutex m_pauseMutex;
    QWaitCondition m_pauseCond;
    bool checkInterruption();

    enum class ConflictState {
        Idle,           // The worker does other things
        WaitingForUser, // Worker blocks and waits for an answer
        SignalSent      // GUI or cancel has already opened the semaphore
    };

    std::atomic<ConflictState> m_conflictState{ConflictState::Idle};
    std::atomic<bool> m_isCancelled{false};
};

#endif // FILEOPERATION_H
