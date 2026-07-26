#ifndef FILEOPERATION_H
#define FILEOPERATION_H

#include "helpers.h"

#include <QElapsedTimer>
#include <QList>
#include <QMutex>
#include <QObject>
#include <QSemaphore>
#include <QStorageInfo>
#include <QString>
#include <QUrl>
#include <QWaitCondition>
#include <Qt>

#include <atomic>
#include <optional>

struct CopyStats {
    qint64 totalBytes = 0;
    qint64 bytesWritten = 0;
    int totalFiles = 0;
    int filesWritten = 0;
    int filesSkipped = 0;
    int filesError = 0;
    qint64 elapsedMs = 0;
    QString currentName;       // %relativer_Pfad%
    QString currentSourceDir;  // %source_Ordner%
    QString currentTargetDir;  // %target_Ordner%
    bool isSameDevice = false; // Für "Same/Different Drive"
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

enum class FileOpResult { Success, Skipped, Error, Cancelled};

enum class ConflictResolution { Overwrite, Skip, Cancel };

class FileOperation : public QObject {
    Q_OBJECT
public:
    explicit FileOperation(OperationType opType,
                           QList<QUrl> urls,
                           QString targetDir = QString(),
                           QObject *parent = nullptr);

    QList<FailedItem> failedItems() const { return m_failedItems; }

signals:
    void progress(const CopyStats &stats);
    void conflictDetected(const Conflict &conflict);  // blockierend via Qt::BlockingQueuedConnection
    void wasContinued();
    void wasPaused();
    void wasFinished(int filesError);
    void wasCanceled(int filesError);

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
    std::pair<qint64, int> calculateStats(const QStringList &filePaths);
    bool copyFileInChunks(const QString &src, const QString &dst);
    void updateProgress(bool force = false);
    std::optional<quint32> calculateCRC32(const QString &filePath);
    ConflictResolution askUserForResolution(const Conflict &conflict);
    bool removeReadOnlyAttribute(const QString &path);
    void runRetryList(const QList<FailedItem> &itemsToRetry);
    bool checkTargetSymlinkSupport(const QString &targetDir) const;
    bool targetSupportsSymlinks(); // Lazy Evaluator

    CopyStats m_stats;
    QElapsedTimer m_timer;
    QElapsedTimer m_progressEmitTimer;

    QList<QUrl> m_urls;
    QString m_sourceDir;
    QString m_targetDir;
    OperationType m_operationType;
    QList<FailedItem> m_failedItems;
    mutable std::optional<bool> m_targetSupportsSymlinks;

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
        Idle,           // Worker tut andere Dinge
        WaitingForUser, // Worker blockiert und wartet auf Antwort
        SignalSent      // GUI oder Cancel hat die Semaphore bereits geöffnet
    };

    std::atomic<ConflictState> m_conflictState{ConflictState::Idle};
    std::atomic<bool> m_isCancelled{false};
};

#endif // FILEOPERATION_H
