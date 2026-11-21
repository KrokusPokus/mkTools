#ifndef CUSTOMTABLEMODEL_H
#define CUSTOMTABLEMODEL_H

#include "helpers.h"
#include "settingsmanager.h"

#include <QAbstractTableModel>
#include <QDateTime>
#include <QFileIconProvider>
#include <QFontMetrics>
#include <QHash>
#include <QIcon>
#include <QRegularExpression>
#include <QUrl>
#include <vector>

struct CustomFileInfo {
    QString name;
    QString displayName;
    QString newName;
    QString path;
    QString filePath;
    qint64 size;
    QDateTime date;
    QString type;
    QString crc;
    mutable QSize sizeHint;
    int anchorPathLength;
    int iconIndex;
    int nameMatchQuality;
    int contentMatchCount;
    bool isCut = false;
    bool isDir;
    bool isSymbolicLink;
    bool isExecutable;
    bool isHidden;
    bool isNameCollision = false;
    QString nameCollisionReason; // Reason for isNameCollision (tooltip in the preview)
};

Q_DECLARE_METATYPE(CustomFileInfo)

struct IconCacheEntry {
    QIcon icon;
    QDateTime lastModified;
};

struct ThumbnailCacheEntry {
    QPixmap thumbnail;
    QDateTime lastModified;
};

struct CrcCacheEntry {
    QString crc;
    QDateTime lastModified;
};

// RegEx
struct RegExRule {
    bool enabled = false;
    int target = 0;
    QString match;
    QString replace;
};

// Replace
struct ReplaceRule {
    bool enabled = false;
    int target = 0;
    QString match;
    QString replace;
    bool matchCase = true;
    bool regex = false;
};

// Remove
struct RemoveRule {
    bool enabled = false;
    int target = 0;
    int firstN = 0;
    int lastN = 0;
    int fromN = 0;
    int fromNcount = 0;
};

// Move/Copy
struct MoveCopyRule {
    bool enabled = false;
    int target = 0;
    int fromMode = 0; // 0: None, 1: Copy first n, 2: Copy last n, 3: Move first n, 4: Move last n
    int fromPos = 0;
    int toMode = 0; // 0: None, 1: To start, 2: To end, 3: To pos.
    int toPos = 0;
    QString separator;
};

// Add
struct AddRule {
    bool enabled = false;
    int target = 0;
    QString prefix;
    QString insertText;
    int insertPos;
    QString suffix;
};

// Add Numbering
struct AddNumberingRule {
    bool enabled = false;
    int target = 0;
    int mode = 0; // 0: None, 1: Prefix, 2: Suffix
    int start = 1;
    int step = 1;
    QString separator;
};

// Add Date
struct AddDateRule {
    bool enabled = false;
    int target = 0;
};

// Add Folder Name
struct AddFolderNameRule {
    bool enabled = false;
    int target = 0;
    int mode = 0; // 0: None, 1: Prefix, 2: Suffix
    QString separator;
    int levels = 0;
};

// Number Manipulation
struct NumberManipulationRule {
    bool enabled = false;
    int targetGroup = 0;
    bool addLeadEnabled = false;
    int addLeadCount = 0;
    bool newEnabled = false;
    int newStart = 0;
    int newStep = 0;
};

// Case
struct CaseRule {
    bool enabled = false;
    int target = 0;
    int mode = 0; // 0: Same, 1: Lower-Case, 2: Upper-Case, 3: Title-Case, 4: Sentence
};

// Filters
struct FilterRule {
    bool showFiles = false;
    bool showFolders = false;
    bool showRecursive = false;
    QString filter;
    bool matchCase = false;

    // Overload for change comparisons
    bool operator==(const FilterRule &other) const = default; // C++20
    bool operator!=(const FilterRule &other) const { return !(*this == other); }
};

struct RenameRules {
    int target = 0;
    ReplaceRule rpl;        // Replace
    RemoveRule rmv;         // Remove
    MoveCopyRule mcp;       // Move/Copy
    AddRule add;            // Add
    AddNumberingRule num;
    AddDateRule dat;        // Add Date
    AddFolderNameRule afn;  // Add Folder Name
    NumberManipulationRule man; // Number Manipulation
    CaseRule cas;           // Extension (11)
    FilterRule flt;         // Selections (12)
};


class CustomTableModel : public QAbstractTableModel {
    Q_OBJECT

signals:
    void filesDropped(const QList<QUrl> &urls, const QString &targetDirectory, Qt::DropAction action);
    void searchProgress(uint itemsFound, uint nameMatched, uint contentMatched);
    void searchFinished(uint itemsFound, uint nameMatched, uint contentMatched, bool searchInterrupted);
    void renameConflictsChanged(int conflictCount);

public:
    enum Column {
        eColName = 0,
        eColNewName = 1,
        eColPath = 2,
        eColSize = 3,
        eColDate = 4,
        eColType = 5,
        eColQuality = 6,
        eColCount = 7,
        eColCRC = 8,
        ColumnCount // A commonly used trick: always comes last and automatically gives the total number of columns (8 here)
    };

    enum ModelRoles {
        IsCutRole = Qt::UserRole + 1,
        IsDirectoryRole = Qt::UserRole + 2,
        IsExecutableRole = Qt::UserRole + 3,
        IsHiddenRole = Qt::UserRole + 4,
        UseRedTextRole = Qt::UserRole + 5,
        UseRedBackgroundRole = Qt::UserRole + 6,
        ListViewSizeHintRole = Qt::UserRole + 7,
        DisplayNameRole = Qt::UserRole + 8,
        FullFileInfoRole = Qt::UserRole + 9
    };

    explicit CustomTableModel(SettingsManager *settings, int columnCount = 8, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;

    // Drag&Drop related
    Qt::DropActions supportedDragActions() const override;
    Qt::DropActions supportedDropActions() const override;                  // Specifies which actions (copy/move) are allowed
    QStringList mimeTypes() const override;                                 // Tells the system that we work with file paths
    QMimeData* mimeData(const QModelIndexList &indexes) const override;     // Packs the selected files into the clipboard/mime object when dragging
    bool dropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column, const QModelIndex &parent) override;           // Handles the release (drop) of the files
    bool canDropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column, const QModelIndex &parent) const override;  // Handles the mouse cursor feedback while dragging

    // Function to hand over your loaded data later
    void setFiles(const std::vector<CustomFileInfo> &files);
    void clear();
    void abort();

    void populateModel_mkBatchRename(const QString &dirPath, const QStringList &externalPathList);
    void populateModel_mkFileSearch(const QString &searchDir, const QString &searchStringFilename, const QString &searchStringContent, bool bRegExFilename, bool bRegExContent, bool bFilenameCaseSensitive, bool bContentCaseSensitive, Qt::CheckState cbDirState, const QSet<QString> &contentSearchExts);
    void populateModel_mkLauncher(const QStringList &searchFolders, const QString &searchString, QStringList recentOpenList);
    void populateModel_mkFolderWidget(const QString &dirPath);

    QString filePath(const QModelIndex &index) const;

    bool isPathIconUpToDate(const QString &path, const QDateTime &currentDate) const;
    void addPathIcon(const QString &path, const QIcon &icon, const QDateTime &lastChanged, int row);

    bool isThumbnailUpToDate(const QString &path, const QDateTime &currentDate) const;
    void addPathThumbnail(const QString &path, const QPixmap &thumb, const QDateTime &lastChanged, int row);

    bool isCrcUpToDate(const QString &path, const QDateTime &currentDate) const;
    void addCRC(const QString &path, const QString &crc, const QDateTime &lastChanged, int row);

    void setCutMarkers(const QStringList &absolutePaths);
    void clearCutMarkers();
    bool isDirectory(int row) const;
    void removeFilePaths(const QSet<QString> &paths);

    void setShowFileExtensions(bool showFileExtensions) { m_settings->showFileExtensions = showFileExtensions; };

    const QString &currentDirectoryPath() const { return m_currentDirectoryPath; }

    void setModelViewMode(ViewMode nIndex) {
        m_viewMode = nIndex;
    }

    void setRenameRows(const QSet<int> &rowSet, const QList<int> &rowList);
    void setRenameRules(const RenameRules &rules);
    bool applyBatchRename(QString *errorMessage = nullptr);
    int renameConflictCount() const { return m_renameConflictCount; }
    bool hasRenameRows() const { return !m_renameRowsList.isEmpty(); }

private:
    bool renameFileInternal(int row, const QString &rawNewName);
    void applyRenamedFileInfo(int row, const QString &newName, const QString &newPath);
    QPixmap generateDummyThumb(const QString &dummyName) const;
    static QIcon symlinkIcon();
    CustomFileInfo createCustomFileInfo(const QFileInfo &fileInfo, int anchorPathLength = 0) const;

    std::vector<CustomFileInfo> m_files;
    QString m_currentDirectoryPath;
    ViewMode m_viewMode;
    int m_columnCount;
    int m_renameConflictCount = 0;
    std::atomic<bool> m_abortSearch{false};

    QFileIconProvider m_iconProvider;
    mutable QHash<QString, QIcon> m_iconCache;                            // Icon cache for suffixes (e.g. "txt", "pdf")
    mutable QHash<QString, QIcon> m_thumbnailCache;

    mutable QHash<QString, IconCacheEntry> m_individualIconCache;         // Icon cache for individual paths (e.g. executables)
    mutable QHash<QString, ThumbnailCacheEntry> m_individualThumbnailCache;
    mutable QHash<QString, CrcCacheEntry> m_CrcCache;

    QStringList m_renameExtPathList;
    QSet<int> m_renameRowsSet;
    QList<int> m_renameRowsList;
    RenameRules m_rules;
    QRegularExpression m_RegexRulesCompiled;
    void recalculateNewNames();
    QString computeNewName(const CustomFileInfo &item, int seqIndex) const;

    QString applyReplaceRule(const QString &input, const ReplaceRule &rule) const;
    QString applyRemoveRule(const QString &input, const RemoveRule &rule) const;
    QString applyMoveCopyRule(const QString &input, const MoveCopyRule &rule) const;
    QString applyAddRule(const QString &input, const AddRule &rule) const;
    QString applyAddNumberingRule(const QString &input, const AddNumberingRule &rule, int seqIndex) const;
    QString applyAddDateRule(const QString &input, const AddDateRule &rule) const;
    QString applyAddFolderNameRule(const QString &input, const AddFolderNameRule &rule, const QString &path) const;
    QString applyNumberManipulationRule(const QString &input, const NumberManipulationRule &rule, int seqIndex) const;
    QString applyCaseRule(const QString &input, const CaseRule &rule) const;

    SettingsManager *m_settings;
    QFontMetrics m_fontMetrics;
};



#endif // CUSTOMTABLEMODEL_H
