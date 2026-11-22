#ifndef HELPERS_H
#define HELPERS_H

#include <KService>

#include <QFileInfo>
#include <QPixmap>
#include <QString>


enum class OperationType { Copy, Move, Link, Delete, Recycle }; // needed for interop with mkTransactionHandler
enum ViewMode {List = 0, Detail = 1, Thumbnail = 2};

struct DesktopEntry {
    QString id;
    QString name;
    QString icon;
    QString exec;
    QString path;
    QString workDir;
    QString type; // "Application", "Link", etc.
    QString url;  // For Type=Link
    bool isValid = false;
    bool useTerminal = false;
};

namespace Helpers {

    QString cleanFileName(const QString &fileName);
    QString fileIdentity(const QString &path);
    bool isSameFile(const QString &pathA, const QString &pathB);
    QString formatAdaptiveSize(quint64 bytes);
    quint32 calculateCRC32(const QString &filePath);

    DesktopEntry getDesktopEntryById(const QString &id);
    DesktopEntry getDesktopEntry(const QFileInfo &fileInfo);
    bool openWithSystemDefault(const QUrl &url);
    void openFileListWithHandler(const QString &handler, const QStringList &fileList);
    void launchDesktopFile(const DesktopEntry &info, const QStringList &fileList = {});
    QList<QStringList> expandDesktopExec(const DesktopEntry &entry, const QStringList &files);
    void startFileOperation(OperationType operationType, const QList<QUrl> &urlList, const QString &targetDir, bool fromClipboard, const QString &workingDir, QObject *parent);
    void launchRenameTool(const QString &targetDir, const QStringList &pathList, const QString &renameTool, QObject *parent = nullptr);
    void browseToFile(const QString &path, const QString &fileManager);

    QPixmap generateThumbnail(const QFileInfo &fileInfo);
    bool hasIconExt(const QFileInfo &fileInfo);
    bool hasOnlyFiles(const QStringList &pathList);
    bool isCurrentProcessElevated();
    bool onSameStorageDevice(const QString &pathA, const QString &pathB);
    void createInternetShortcut(const QString &urlStr, const QString &targetDir, const QString &webTitle);
    QString getSiblingPath(const QString &path, bool previous);

    uint getNameMatchQuality(const QString &itemName, const QString &path, const QString &searchString, const QStringList &searchStringSplit, Qt::CaseSensitivity caseSensitivity);
    uint getRegExNameMatchQuality(const QFileInfo &fileInfo, const QRegularExpression &re);
    uint getContentMatchCount(const QFileInfo &fileInfo, const QString &searchStringContent, Qt::CaseSensitivity caseSensitivity, const QSet<QString> &contentSearchExts);
    uint getRegExContentMatchCount(const QFileInfo &fileInfo, const QRegularExpression &re, const QSet<QString> &contentSearchExts);

    QString toSentenceCase(const QString &input);
    QString toTitleCase(const QString &input);

    QString expandPath(QString path);
    QString getPathFromClipboard(QString text);
    void showPopup(const QString &appName, const QString &title, const QString &body);

    //bool isTextFile(const QString &filePath);

    bool showDeleteConfirmationDialog(const QStringList &pathList, bool bRecycleOnly, QWidget *parent);
    void showKdePropertiesDialog(const QStringList &filePaths, QWidget *parent);
    void openUrlsWithKIO(const QList<QUrl> &urls);

    // How the "Open" action of the apps handles a file
    enum class OpenMethod {
        DesktopEntry,   // .desktop file: the entry is launched
        Execute,        // file with an executable MIME type (program, script) and the executable bit: executed directly
        PreferredApp    // opened with the preferred application of its MIME type
    };
    OpenMethod openMethod(const QFileInfo &fileInfo);
    bool allExecutable(const QStringList &paths);
    KService::Ptr openActionService(const QStringList &paths);

    QString findTerminalEmulator();
    QStringList terminalArguments(const QString &terminal, const QString &program, const QStringList &args, const QString &workDir, bool hold);
    bool executeInTerminal(const QString &path);
    bool openFileWithPreferredApp(const QString &path, int rank);
    KService::List commonServicesForPaths(const QStringList &paths, int *mimeTypeCount = nullptr);
}

#endif // HELPERS_H
