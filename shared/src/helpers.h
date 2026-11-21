#ifndef HELPERS_H
#define HELPERS_H

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
    QString url;  // Für Type=Link
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
    uint getContentMatchCount(const QFileInfo &fileInfo, const QString &searchStringContent, Qt::CaseSensitivity caseSensitivity, const QSet<QString> &m_FileExtTextSet);
    uint getRegExContentMatchCount(const QFileInfo &fileInfo, const QRegularExpression &re, const QSet<QString> &m_FileExtTextSet);

    QString toSentenceCase(const QString &input);
    QString toTitleCase(const QString &input);

    QString expandPath(QString path);
    QString getPathFromClipboard(QString text);
    void showPopup(const QString &appName, const QString &title, const QString &body);

    //bool isTextFile(const QString &filePath);
    //QString getDisplayName(const QFileInfo &fileInfo, bool showFileExtensions);
    //QString getDisplayName(const QString &filePath, bool isDir, bool showFileExtensions);
    //QString readLnkTargetOnLinux(const QString &filePath);

    void showKdePropertiesDialog(const QStringList &filePaths, QWidget *parent);
    void openUrlsWithKIO(const QList<QUrl> &urls);
    bool openFileWithPreferredApp(const QString &path, int rank);
}

#endif // HELPERS_H
