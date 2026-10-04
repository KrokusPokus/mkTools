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
    QString drivePath;
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
    bool isDrive = false;
    bool isExecutable;
    bool isHidden;
    bool isNameCollision = false;
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
    bool matchCase = false;
};

// Remove
struct RemoveRule {
    bool enabled = false;
    int target = 0;
    int firstN = 0;
    int lastN = 0;
    int fromN = 0;
    int toN = 0;
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

// Padding
struct PaddingRule {
    bool enabled = false;
    int target = 0;
    bool addLeadEnabled = false;
    QString addLeadChar;
    int addLeadCount = 0;
    bool addLeadNewEnabled = false;
    int addLeadNewStart = 0;
    int addLeadNewStep = 0;
};

// Case
struct CaseRule {
    bool enabled = false;
    int target = 0;
    int mode = 0; // 0: Same, 1: Lower, 2: Upper, 3: Title, 4: Fixed, 5: Extra, 6: Remove
};

// Filters
struct FilterRule {
    bool showFiles = false;
    bool showFolders = false;
    bool showRecursive = false;
    QString filter;
    bool matchCase = false;

    // Überladung für Vergleiche auf Änderung
    bool operator==(const FilterRule &other) const = default; // C++20
    bool operator!=(const FilterRule &other) const { return !(*this == other); }
};

struct RenameRules {
    RegExRule rgx;          // RegEx
    ReplaceRule rpl;        // Replace
    RemoveRule rmv;         // Remove
    MoveCopyRule mcp;       // Move/Copy
    AddRule add;            // Add
    AddNumberingRule num;
    AddDateRule dat;        // Add Date
    AddFolderNameRule afn;  // Add Folder Name
    PaddingRule pad;        // Number Padding
    CaseRule cas;           // Extension (11)
    FilterRule flt;     // Selections (12)
};


class CustomTableModel : public QAbstractTableModel {
    Q_OBJECT

signals:
    void filesDropped(const QList<QUrl> &urls, const QString &targetDirectory, Qt::DropAction action);
    void searchProgress(uint itemsFound, uint nameMatched, uint contentMatched);
    void searchFinished(uint itemsFound, uint nameMatched, uint contentMatched, bool searchInterrupted);

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
        ColumnCount // Ein oft genutzter Trick: Steht immer am Ende und gibt automatisch die Gesamtzahl der Spalten an (hier 8)
    };

    enum ModelRoles {
        IsCutRole = Qt::UserRole + 1,
        IsDirectoryRole = Qt::UserRole + 2,
        IsExecutableRole = Qt::UserRole + 3,
        IsHiddenRole = Qt::UserRole + 4,
        UseRedTextRole = Qt::UserRole + 5,
        ListViewSizeHintRole = Qt::UserRole + 6,
        DisplayNameRole = Qt::UserRole + 7,
        FullFileInfoRole = Qt::UserRole + 8
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
    Qt::DropActions supportedDropActions() const override;                  // Gibt an, welche Aktionen (Kopieren/Verschieben) erlaubt sind
    QStringList mimeTypes() const override;                                 // Sagt dem System, dass wir mit Datei-Pfaden arbeiten
    QMimeData* mimeData(const QModelIndexList &indexes) const override;     // Packt die ausgewählten Dateien beim Draggen in das Clipboard/Mime-Objekt
    bool dropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column, const QModelIndex &parent) override;           // Verarbeitet das Loslassen (Drop) der Dateien
    bool canDropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column, const QModelIndex &parent) const override;  // Verarbeitet das Mauszeiger-Feedback während dem Ziehen

    // Funktion, um später deine eingelesenen Daten zu übergeben
    void setFiles(const std::vector<CustomFileInfo> &files);
    void clear();
    void abort();

    void populateModel_mkBatchRename(const QString &dirPath, const QStringList &externalPathList);
    void populateModel_mkFileSearch(const QString &searchDir, const QString &searchStringFilename, const QString &searchStringContent, bool bRegExFilename, bool bRegExContent, bool bFilenameCaseSensitive, bool bContentCaseSensitive, Qt::CheckState cbDirState, const QSet<QString> &FileExtTextSet);
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

    const QString &currentDirectoryPath() const { return m_currentDirectoryPath; }

    void setModelViewMode(ViewMode nIndex) {
        m_viewMode = nIndex;
    }

    void setRenameRows(const QSet<int> &rowSet, const QList<int> &rowList);
    void setRenameRules(const RenameRules &rules);
    void applyBatchRename();

private:
    bool renameFileInternal(int row, const QString &rawNewName);
    QPixmap generateDummyThumb(const QString &dummyName) const;
    CustomFileInfo createCustomFileInfo(const QFileInfo &fileInfo, int anchorPathLength = 0) const;

    std::vector<CustomFileInfo> m_files;
    QString m_currentDirectoryPath;
    ViewMode m_viewMode;
    int m_columnCount;
    std::atomic<bool> m_abortSearch{false};

    QFileIconProvider m_iconProvider;
    mutable QHash<QString, QIcon> m_iconCache;                            // Icon-Cache für Suffixe (z.B. "txt", "pdf")
    mutable QHash<QString, QIcon> m_thumbnailCache;

    mutable QHash<QString, IconCacheEntry> m_individualIconCache;         // Icon-Cache für individuelle Pfade (z.B. exe-Dateien)
    mutable QHash<QString, ThumbnailCacheEntry> m_individualThumbnailCache;
    mutable QHash<QString, CrcCacheEntry> m_CrcCache;

    QStringList m_renameExtPathList;
    QSet<int> m_renameRowsSet;
    QList<int> m_renameRowsList;
    RenameRules m_rules;
    QRegularExpression m_RegexRulesCompiled;
    void recalculateNewNames();
    QString computeNewName(const CustomFileInfo &item, int seqIndex) const;

    QString applyRegExRule(const QString &input, const RegExRule &rule) const;
    QString applyReplaceRule(const QString &input, const ReplaceRule &rule) const;
    QString applyRemoveRule(const QString &input, const RemoveRule &rule) const;
    QString applyMoveCopyRule(const QString &input, const MoveCopyRule &rule) const;
    QString applyAddRule(const QString &input, const AddRule &rule) const;
    QString applyAddNumberingRule(const QString &input, const AddNumberingRule &rule, int seqIndex) const;
    QString applyAddDateRule(const QString &input, const AddDateRule &rule) const;
    QString applyAddFolderNameRule(const QString &input, const AddFolderNameRule &rule, const QString &path) const;
    QString applyPaddingRule(const QString &input, const PaddingRule &rule) const;
    QString applyCaseRule(const QString &input, const CaseRule &rule) const;

    SettingsManager *m_settings;
    QFontMetrics m_fontMetrics;
};



#endif // CUSTOMTABLEMODEL_H
