#ifndef HELPERS_H
#define HELPERS_H

#include <QFileInfo>
#include <QPixmap>
#include <QString>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

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

struct LnkInfo {
    qint64 size = 0;
    QDateTime birthTime;
    QDateTime lastAccessTime;
    QDateTime lastWriteTime;

    bool isReadOnly = false;
    bool isHidden   = false;
    bool isSystem   = false;

    bool exists     = false;
};

bool isTextFile(const QString &filePath);
QString cleanFileName(const QString &fileName);
QString formatAdaptiveSize(quint64 bytes);
quint32 calculateCRC32(const QString &filePath);

DesktopEntry getDesktopEntryById(const QString &id);
DesktopEntry getDesktopEntry(const QFileInfo &fileInfo);
void openFileListWithHandler(const QString &handler, const QStringList &fileList);
void launchDesktopFile(const DesktopEntry &info, const QStringList &fileList = {});
void browseToFile(const QString &path, const QString &fileManager);
QString getDisplayName(const QFileInfo &fileInfo, bool showFileExtensions);
QString getDisplayName(const QString &filePath, bool isDir, bool showFileExtensions);
QPixmap generateThumbnail(const QFileInfo &fileInfo);
bool hasIconExt(const QFileInfo &fileInfo);
bool hasOnlyFiles(const QStringList &pathList);
bool isCurrentProcessElevated();
bool onSameStorageDevice(const QString &pathA, const QString &pathB);
void createInternetShortcut(const QString &urlStr, const QString &targetDir, const QString &webTitle);
QString readLnkTargetOnLinux(const QString &filePath);  // currently unused
QString getSiblingPath(const QString &path, bool previous);

#ifdef Q_OS_WIN
bool startProcessElevatedWin(const QString &programPath, const QString &arguments);
QString argumentsToWinString(const QStringList &args);
LnkInfo getLnkInfo(const QString &filePath);
qint64 getLnkSize(const QString &filePath);
QDateTime fileTimeToQDateTime(const FILETIME &ft);
#endif

bool atWordBoundary(const QString &fileName, const QString &word, Qt::CaseSensitivity cs);
uint getNameMatchQuality(const QString &itemName, const QString &path, const QString &searchString, const QStringList &searchStringSplit, Qt::CaseSensitivity caseSensitivity);
uint getRegExNameMatchQuality(const QFileInfo &fileInfo, const QRegularExpression &re);
uint getContentMatchCount(const QFileInfo &fileInfo, const QString &searchStringContent, Qt::CaseSensitivity caseSensitivity, const QSet<QString> &m_FileExtTextSet);
uint getRegExContentMatchCount(const QFileInfo &fileInfo, const QRegularExpression &re, const QSet<QString> &m_FileExtTextSet);

namespace Helpers {
    QString expandPath(QString path);
    QString getPathFromClipboard(QString text);
    void showPopup(const QString &appName, const QString &title, const QString &body);

#ifdef Q_OS_WIN
    QString getAssociatedExecutableWin32(const QString &extensionOrScheme);
    void openUrlsWithWin32(const QList<QUrl> &urls);
#endif

#ifdef Q_OS_LINUX
    void showKdePropertiesDialog(const QStringList &filePaths, QWidget *parent);
    void openUrlsWithKIO(const QList<QUrl> &urls);
#endif
}

#endif // HELPERS_H
