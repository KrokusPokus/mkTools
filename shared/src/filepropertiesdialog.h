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

class FilePropertiesDialog : public QDialog {
    Q_OBJECT

public:
    explicit FilePropertiesDialog(const QStringList &filePaths, QWidget *parent = nullptr);
    ~FilePropertiesDialog();

private:
    void setupUi(const QFileInfo &fileInfo);
    void setupUiMultiMode();
    void updateUiAsyncStart();
    QString getFileType(const QFileInfo &info);
    quint64 calculateFolderSize(const QString &path, int &fileCount, int &folderCount, QSet<QString> &visitedDirs, const std::atomic<bool> &abortFlag);
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

    std::atomic<bool> m_abort{false};

    // For calculating size concurrently
    struct ProgressResult {
        quint64 size = 0;
        int files = 0;
        int folders = 0;
    };

    QFutureWatcher<ProgressResult> *m_watcher = nullptr;
    void updateUiAsync(ProgressResult result);

protected:
    void done(int r) override;
};

#endif // FILEPROPERTIESDIALOG_H
