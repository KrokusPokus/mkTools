#ifndef FILEPROPERTIESDIALOG_H
#define FILEPROPERTIESDIALOG_H

#include <QCheckBox>
#include <QDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QLabel>
#include <QLineEdit>
#include <QFormLayout>
#include <QPushButton>
#include <QtConcurrent>
#include <QVBoxLayout>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <shlobj.h>     // needed for getWindowsShortcutDetails() and setWindowsShortcutDetails()
#endif

struct ProgressResult {
    // physical values (symlinks NOT resolved)
    quint64 directSize = 0;
    quint64 directFiles = 0;
    quint64 directFolders = 0;

    // logical values (symlinks resolved)
    quint64 followedSize = 0;
    quint64 followedFiles = 0;
    quint64 followedFolders = 0;
};

struct AtomicProgressResult {
    std::atomic<quint64> directFiles{0};
    std::atomic<quint64> directFolders{0};
    std::atomic<quint64> directSize{0};
    std::atomic<quint64> followedFiles{0};
    std::atomic<quint64> followedFolders{0};
    std::atomic<quint64> followedSize{0};

    // Hilfsmethode, um ein konsistentes, nicht-atomares Foto für das UI zu machen
    void snapshot(ProgressResult &out) const {
        out.directFiles = directFiles.load(std::memory_order_relaxed);
        out.directFolders = directFolders.load(std::memory_order_relaxed);
        out.directSize = directSize.load(std::memory_order_relaxed);
        out.followedFiles = followedFiles.load(std::memory_order_relaxed);
        out.followedFolders = followedFolders.load(std::memory_order_relaxed);
        out.followedSize = followedSize.load(std::memory_order_relaxed);
    }

    void reset() {
        directFiles = 0;
        directFolders = 0;
        directSize = 0;
        followedFiles = 0;
        followedFolders = 0;
        followedSize = 0;
    }
};

class FilePropertiesDialog : public QDialog {
    Q_OBJECT

public:
    explicit FilePropertiesDialog(const QStringList &filePaths, QWidget *parent = nullptr);
    ~FilePropertiesDialog();

private:
    static void calculateStats(const QStringList &filePaths, AtomicProgressResult &res, const std::atomic<bool> &abortFlag);
    void setupUi(const QFileInfo &fileInfo);
    void setupUiMultiMode();
    void initiateStatsCalculation();
    QString getFileType(const QFileInfo &info);
    void onOkPressed();
    bool canModifyPermissions(const QFileInfo &fileInfo);
    bool pathIsDrive(const QString &absoluteFilePath);
    bool isNetworkDrive(const QStorageInfo &storage);

#ifdef Q_OS_WIN
    struct WinShortcutDetails {
        QString targetPath;
        QString arguments;
        QString workingDirectory;
        QString description;
    };

    DWORD getWindowsFileAttributes(const QString &filePath);
    bool getWindowsShortcutDetails(const QString &lnkFilePath, WinShortcutDetails &details);
    bool setWindowsShortcutDetails(const QString &lnkFilePath, const WinShortcutDetails &details);
#endif

    QStringList m_filePaths;
    bool m_isMultiMode;
    QLocale m_locale;

    QLabel *m_iconLabel = nullptr;
    QLineEdit *m_nameEdit = nullptr;
    QLabel *m_typeLabel1 = nullptr;
    QLabel *m_typeLabel2 = nullptr;
    QLabel *m_pathEdit = nullptr;
    QLabel *m_sizeLabel = nullptr;
    QLabel *m_sizeLabel2 = nullptr;
    QLabel *m_sizeLabel3 = nullptr;
    QLabel *m_containsLabel = nullptr;
    QLabel *m_createdLabel = nullptr;
    QLabel *m_lastReadLabel = nullptr;
    QLabel *m_modifiedLabel = nullptr;
    QLabel *m_linkTargetLabel = nullptr;
    QLabel *m_linkTargetLabelRaw = nullptr;

    QLineEdit *m_linkTargetEdit = nullptr;
    QLineEdit *m_linkArgumentsEdit = nullptr;
    QLineEdit *m_linkWorkingDirectoryEdit = nullptr;

#ifdef Q_OS_WIN
    QCheckBox *m_readOnlyCB = nullptr;
    QCheckBox *m_hiddenCB = nullptr;
    QCheckBox *m_systemCB = nullptr;
#elif defined(Q_OS_LINUX)
    QLabel *m_ownerLabel = nullptr;
    QLabel *m_groupLabel = nullptr;
    QLabel *m_permLabel = nullptr;
    QCheckBox *m_permCBs[3][3]{};   // The '{}' initializes all 9 pointers to nullptr
#endif

    AtomicProgressResult m_progress;
    QTimer *m_updateTimer{nullptr};
    std::atomic<bool> m_abort{false};
    QFutureWatcher<void> m_watcher;
    void updateGuiLabelText(const ProgressResult &result);

protected:
    void done(int r) override;
};

#endif // FILEPROPERTIESDIALOG_H
