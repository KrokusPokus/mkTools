#include "customtablemodel.h"
#include "helpers.h"

#include <QApplication>
#include <QDirIterator>
#include <QRandomGenerator>
#include <QElapsedTimer>
#include <QMimeData>
#include <QPainter>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStyle>

#include <algorithm>        // needed for std::reverse();

CustomTableModel::CustomTableModel(SettingsManager *settings, int columnCount, QObject *parent) : QAbstractTableModel(parent), m_settings(settings), m_columnCount(columnCount), m_fontMetrics(QApplication::font()) {}

int CustomTableModel::rowCount(const QModelIndex &parent) const {
    if (parent.isValid()) return 0;
    return static_cast<int>(m_files.size());
}

int CustomTableModel::columnCount(const QModelIndex &parent) const {
    if (parent.isValid()) return 0;
    return m_columnCount;
}

QVariant CustomTableModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid()) return QVariant();

    int row = index.row();
    int col = index.column();

    if (row < 0 || row >= static_cast<int>(m_files.size())) {
        return QVariant();
    }

    const CustomFileInfo &file = m_files[row];

    if (role == Qt::DisplayRole) {
        switch (col) {
            case eColName: return file.displayName;
            case eColNewName: {
                if (m_renameRowsSet.contains(row)) {
                    return file.newName;
                }
                return file.displayName;
                }
            case eColPath: {
                    if (file.anchorPathLength > 0) {
                        if (file.anchorPathLength <= file.path.length()) {
                            return file.path.sliced(file.anchorPathLength);
                        }
                        else {
                            return QVariant();
                        }
			        }
            		return file.path;
            	}
            case eColSize: {
                    if (file.isDir) {
                        return QVariant();
                    }
                    return Helpers::formatAdaptiveSize(file.size);
                }
            case eColDate: return file.date.toString("yyyy-MM-dd  HH:mm:ss");
            case eColType: {
                    if (file.isSymbolicLink) {
                        if (file.type.isEmpty()) {
                            return "SymLink";
                        } else {
                            return file.type + " (SymLink)";
                        }
                    }
                }
                return file.type;
            case eColQuality: return file.nameMatchQuality;
            case eColCount: return file.contentMatchCount;
            case eColCRC: return file.crc;
            default: return QVariant();
        }
    }
    else if (role == Qt::EditRole) {
        switch (col) {
            case eColName: return file.name;
            case eColNewName: {
                if (m_renameRowsSet.contains(row)) {
                    return file.newName;
                }
                return file.name;
                }
            case eColPath: return file.path;
            case eColSize: return file.size;
            case eColDate: return file.date;
            case eColType: return file.type;
            case eColQuality: return file.nameMatchQuality;
            case eColCount: return file.contentMatchCount;
            case eColCRC: return file.crc;
            default:
                return data(index, Qt::DisplayRole);
        }
    }
    else if (role == Qt::DecorationRole) {
        if (col != eColName) return QVariant(); // Show icons only in the name column

        QString absolutePath = file.filePath;
        if (m_viewMode == ViewMode::Thumbnail) {
            auto itThumb = m_individualThumbnailCache.find(absolutePath);
            if (itThumb != m_individualThumbnailCache.end()) {
                return itThumb.value().thumbnail; // Returns the finished 96x96 image
            }

            if (file.isDir) {
                auto it = m_thumbnailCache.find("//dir//");
                if (it == m_thumbnailCache.end()) {
                    it = m_thumbnailCache.insert("//dir//", generateDummyThumb("//dir//"));
                }
                return it.value();
            } else {
                int lastDot = file.name.lastIndexOf('.');
                QString suffix = (lastDot > 0) ? file.name.sliced(lastDot + 1).toLower() : "";

                auto it = m_thumbnailCache.find(suffix);
                if (it == m_thumbnailCache.end()) {
                    it = m_thumbnailCache.insert(suffix, generateDummyThumb("any_filename." + suffix));
                }
                return it.value();
            }
        }
        else {
            // Symlinks take precedence over file/folder and over individual icons:
            // a fixed icon, created once and stored in the general cache.
            if (file.isSymbolicLink) {
                auto it = m_iconCache.find("//symlink//");
                if (it == m_iconCache.end()) {
                    it = m_iconCache.insert("//symlink//", symlinkIcon());
                }
                return it.value();
            }

            auto itPath = m_individualIconCache.find(absolutePath);
            if (itPath != m_individualIconCache.end()) {
                return itPath.value().icon;
            }

            if (file.isDir) {
                auto it = m_iconCache.find("//dir//");
                if (it == m_iconCache.end()) {
                    it = m_iconCache.insert("//dir//", m_iconProvider.icon(QFileIconProvider::Folder));
                }
                return it.value();
            } else {
                int lastDot = file.name.lastIndexOf('.');
                QString suffix = (lastDot > 0) ? file.name.sliced(lastDot + 1).toLower() : "";

                auto it = m_iconCache.find(suffix);
                if (it == m_iconCache.end()) {
                    QFileInfo dummyInfo("any_filename." + suffix);
                    it = m_iconCache.insert(suffix, m_iconProvider.icon(dummyInfo));
                }
                return it.value();
            }
        }
    }
    else if (role == Qt::TextAlignmentRole) {
        switch (col) {
            case eColSize:
                return QVariant(Qt::AlignRight | Qt::AlignVCenter);
            case eColQuality:
            case eColCount:
            case eColCRC:
                return QVariant(Qt::AlignCenter | Qt::AlignVCenter);
            case eColName:
            case eColNewName:
            case eColPath:
            case eColDate:
            case eColType:
                return QVariant(Qt::AlignLeft | Qt::AlignVCenter);
            default:
                return QVariant(Qt::AlignLeft | Qt::AlignVCenter);
        }
    }
    else if (role == CustomTableModel::UseRedTextRole) {
        switch (col) {
            case eColName:
                return file.isExecutable && !file.isDir && m_settings && m_settings->executableFilesRed;
            default:
                return QVariant(false);
        }
    }
    else if (role == CustomTableModel::UseRedBackgroundRole) {
        switch (col) {
            case eColNewName:
                return file.isNameCollision;
            default:
                return QVariant(false);
        }
    }
    else if (role == Qt::ToolTipRole && col == eColNewName && !file.nameCollisionReason.isEmpty()) {
        return file.nameCollisionReason;
    }
    else if (role == CustomTableModel::IsCutRole) {
        return QVariant(file.isCut);
    }
    else if (role == CustomTableModel::IsDirectoryRole) {
        return QVariant(file.isDir);
    }
    else if (role == CustomTableModel::IsExecutableRole) {
        return QVariant(file.isExecutable);
    }
    else if (role == CustomTableModel::IsHiddenRole) {
        return QVariant(file.isHidden);
    }
    else if (role == CustomTableModel::ListViewSizeHintRole) {
        if (m_viewMode == ViewMode::List && !file.sizeHint.isValid()) {
            int totalWidth = m_fontMetrics.horizontalAdvance(file.displayName) + 36; // 16 px icon size + 20 px padding
            QSize calculatedSize = QSize(totalWidth, 18);  // row height 18
            m_files[row].sizeHint = calculatedSize;
            return calculatedSize;
        }
        return file.sizeHint;
    }
    else if (role == CustomTableModel::DisplayNameRole) {
        return QVariant(file.displayName);
    }
    else if (role == CustomTableModel::FullFileInfoRole) {
        return QVariant::fromValue(file);
    }

    return QVariant();
}

QVariant CustomTableModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
        switch (section) {
        case eColName:
            return tr("Name");
        case eColNewName:
            return tr("New Name");
        case eColPath:
            return tr("Path");
        case eColSize:
            return tr("Size");
        case eColDate:
            return tr("Changed");
        case eColType:
            return tr("Type");
        case eColQuality:
            return tr("Rating");
        case eColCount:
            return tr("Count");
        case eColCRC:
            return tr("CRC");
        default:
            return QVariant();
        }
    }

    if (orientation == Qt::Horizontal && role == Qt::TextAlignmentRole) {
        switch (section) {
        case eColSize:
            return QVariant(Qt::AlignRight | Qt::AlignVCenter);
        case eColQuality:
        case eColCount:
        case eColCRC:
            return QVariant(Qt::AlignCenter | Qt::AlignVCenter);
        case eColName:
        case eColNewName:
        case eColPath:
        case eColDate:
        case eColType:
            return QVariant(Qt::AlignLeft | Qt::AlignVCenter);
        default:
            return QVariant(Qt::AlignLeft | Qt::AlignVCenter);
        }
    }

    // For the vertical headers (row numbers 1, 2, 3...) we use Qt's default behavior
    return QAbstractTableModel::headerData(section, orientation, role);
}

Qt::ItemFlags CustomTableModel::flags(const QModelIndex &index) const {
    if (!index.isValid()) {                             // No index -> no item -> background!
        if (!m_currentDirectoryPath.isEmpty()) {
        	return Qt::NoItemFlags | Qt::ItemIsDropEnabled;  // Background needs to have Qt::ItemIsDropEnabled to become a valid drop target.
    	}
        return Qt::NoItemFlags;
    }

    // Get the default flags (Selectable, Enabled)
    Qt::ItemFlags cellFlags = QAbstractTableModel::flags(index);

    if (index.column() == 0) {
        cellFlags |= Qt::ItemIsEditable | Qt::ItemIsDragEnabled;

        // Only folders may serve as drop targets
        int row = index.row();
        if (row >= 0 && row < static_cast<int>(m_files.size())) {
            if (m_files[row].isDir) {
                cellFlags |= Qt::ItemIsDropEnabled;
            }
        }
    }

    return cellFlags;
}

bool CustomTableModel::setData(const QModelIndex &index, const QVariant &value, int role) {
    if (!index.isValid() || role != Qt::EditRole || index.column() != eColName) {
        return false;
    }

    if (!renameFileInternal(index.row(), value.toString())) {
        return false;
    }

    // Notify the view about the single row
    emit dataChanged(index, index, {Qt::DisplayRole, Qt::EditRole});

    return true;
}

bool CustomTableModel::renameFileInternal(int row, const QString &rawNewName) {
    if (row < 0 || row >= static_cast<int>(m_files.size())) {
        return false;
    }

    QString newName = Helpers::cleanFileName(rawNewName.trimmed());

    // Stop if the name is empty or has not changed at all
    if (newName.isEmpty() || newName == m_files[row].name) {
        return false;
    }

    // 1. Rename the file on disk
    QString oldPath = m_files[row].filePath;
    QString newPath = QDir::cleanPath(m_files[row].path + "/" + newName);

    if (!QFile::rename(oldPath, newPath)) {
        return false;
    }

    // 2. Update the struct in memory
    applyRenamedFileInfo(row, newName, newPath);
    return true;
}

// Updates the metadata of a row after the file was renamed on disk
void CustomTableModel::applyRenamedFileInfo(int row, const QString &newName, const QString &newPath) {
    CustomFileInfo &info = m_files[row];
    info.name = newName;
    info.displayName = newName;
    info.filePath = newPath;

    QString ext;
    if (!info.isDir) {
        int lastDot = newName.lastIndexOf('.');
        if (lastDot > 0) {
            ext = info.name.sliced(lastDot + 1).toLower(); // in case the type has changed

            if (!m_settings->showFileExtensions && m_settings->hideExts.contains(ext)) {
                info.displayName = newName.sliced(0, lastDot);
            }
        }
    }

    info.type = ext;
}

void CustomTableModel::sort(int column, Qt::SortOrder order) {
    // We're not using any of it. The proxy model will take over this job.
    Q_UNUSED(column);
    Q_UNUSED(order);
}

// Set the data again and signal the view that it has to repaint
void CustomTableModel::setFiles(const std::vector<CustomFileInfo> &files) {
    beginResetModel();
    m_files = files;
    endResetModel();
}

void CustomTableModel::clear() {
    beginResetModel();
    m_files.clear();
    endResetModel();
}

void CustomTableModel::abort() {
    m_abortSearch.store(true);
}

QString CustomTableModel::filePath(const QModelIndex &index) const {
    if (!index.isValid() || index.model() != this) {
        return QString();
    }

    int row = index.row();
	
    if (row < 0 || row >= static_cast<int>(m_files.size())) {
        return QString();
    }

    return m_files[row].filePath;
}

// Fixed icon for symlinks (files and folders alike).
// The only place where the icon source is defined.
QIcon CustomTableModel::symlinkIcon() {
    // PROVISIONAL: theme emblem (Linux), fallback to the Qt style icon.
    QIcon icon = QIcon::fromTheme("emblem-symbolic-link");
    if (icon.isNull()) {
        icon = QApplication::style()->standardIcon(QStyle::SP_FileLinkIcon);
    }
    return icon;
}

bool CustomTableModel::isPathIconUpToDate(const QString &path, const QDateTime &currentDate) const {
    auto it = m_individualIconCache.find(path);
    if (it == m_individualIconCache.end()) {
        return false;
    }
    // If the date in the cache is invalid or matches exactly -> up to date / fits already!
    return !it.value().lastModified.isValid() || it.value().lastModified == currentDate;
}

void CustomTableModel::addPathIcon(const QString &path, const QIcon &icon, const QDateTime &lastModified, int row) {
    m_individualIconCache.insert(path, {icon, lastModified});

    // Immediately notify the view that exactly THIS row (column 0) has to be repainted
    QModelIndex idx = createIndex(row, 0);
    emit dataChanged(idx, idx, {Qt::DecorationRole});
}

bool CustomTableModel::isThumbnailUpToDate(const QString &path, const QDateTime &currentDate) const {
    auto it = m_individualThumbnailCache.find(path);
    if (it == m_individualThumbnailCache.end()) {
        return false;
    }
    // If the date in the cache is invalid or matches exactly -> up to date / fits already!
    return !it.value().lastModified.isValid() || it.value().lastModified == currentDate;
}

void CustomTableModel::addPathThumbnail(const QString &path, const QPixmap &thumb, const QDateTime &lastModified, int row) {
    m_individualThumbnailCache.insert(path, {thumb, lastModified});

    int targetRow = row;

    if (targetRow == -1) {
        for (int i = 0; i < m_files.size(); ++i) {
            QString fullFilePath = m_files[i].filePath;
            if (fullFilePath == path) {
                targetRow = i;
                break;
            }
        }
    }

    if (targetRow != -1 && targetRow < this->rowCount()) {
        QModelIndex idx = createIndex(targetRow, 0);
        emit dataChanged(idx, idx, {Qt::DecorationRole});
    }
}

bool CustomTableModel::isCrcUpToDate(const QString &path, const QDateTime &currentDate) const {
    auto it = m_CrcCache.find(path);
    if (it == m_CrcCache.end()) {
        return false;
    }
    // If the date in the cache is invalid or matches exactly -> up to date / fits already!
    return !it.value().lastModified.isValid() || it.value().lastModified == currentDate;
}

void CustomTableModel::addCRC(const QString &path, const QString &crc, const QDateTime &lastModified, int row) {
    m_CrcCache.insert(path, {crc, lastModified});

    int targetRow = row;

    if (targetRow == -1) {
        for (int i = 0; i < m_files.size(); ++i) {
            QString fullFilePath = m_files[i].filePath;
            if (fullFilePath == path) {
                targetRow = i;
                break;
            }
        }
    }

    if (targetRow != -1 && targetRow < static_cast<int>(m_files.size())) {
        if (m_files[targetRow].crc != crc) {
            m_files[targetRow].crc = crc;

            QModelIndex idx = createIndex(targetRow, CustomTableModel::eColCRC);
            emit dataChanged(idx, idx, {Qt::DisplayRole});
        }
    }
}

void CustomTableModel::setCutMarkers(const QStringList &absolutePaths) {
    // We convert the list into a QSet for fast lookups
    QSet<QString> cutSet(absolutePaths.begin(), absolutePaths.end());

    for (size_t i = 0; i < m_files.size(); ++i) {
        QString fullPath = m_files[i].filePath;
        bool newCutState = cutSet.contains(fullPath);

        if (m_files[i].isCut != newCutState) {
            m_files[i].isCut = newCutState;

            QModelIndex startIdx = createIndex(static_cast<int>(i), 0);
            QModelIndex endIdx = createIndex(static_cast<int>(i), this->columnCount() - 1);
            emit dataChanged(startIdx, endIdx, {CustomTableModel::IsCutRole, Qt::DecorationRole});
        }
    }
}

void CustomTableModel::clearCutMarkers() {
    for (size_t i = 0; i < m_files.size(); ++i) {
        if (m_files[i].isCut) {
            m_files[i].isCut = false;

            QModelIndex startIdx = createIndex(static_cast<int>(i), 0);
            QModelIndex endIdx = createIndex(static_cast<int>(i), this->columnCount() - 1);
            emit dataChanged(startIdx, endIdx, {CustomTableModel::IsCutRole, Qt::DecorationRole});
        }
    }
}

void CustomTableModel::setRenameRows(const QSet<int> &rowSet, const QList<int> &rowList) {
    if (m_renameRowsList == rowList)
        return;

    m_renameRowsList = rowList;
    m_renameRowsSet = rowSet;

    if (m_files.empty() || rowCount() == 0)
        return;

    recalculateNewNames();

    emit dataChanged(createIndex(0, eColNewName), createIndex(rowCount() - 1, eColNewName), {Qt::DisplayRole});
}

namespace {
// Comparison key for paths (the file system is case-sensitive on Linux)
QString renameKey(const QString &path) {
    return QDir::cleanPath(path);
}

// Not yet existing temporary name next to 'path' (for resolving rename cycles)
QString uniqueTempRenamePath(const QString &path) {
    while (true) {
        const QString candidate = QStringLiteral("%1.mkren-%2").arg(path).arg(QRandomGenerator::global()->generate(), 8, 16, QChar('0'));
        const QFileInfo info(candidate);
        if (!info.exists() && !info.isSymLink()) return candidate;
    }
}
} // namespace

void CustomTableModel::recalculateNewNames() {
    // 1. Compute the new names - already sanitized, so that preview, conflict check and execution
    //    are guaranteed to see the same name (previously, sanitizing only happened when renaming).
    int seqIndex = 0;
    for (int row : std::as_const(m_renameRowsList)) {
        if (row >= 0 && row < static_cast<int>(m_files.size())) {
            auto &file = m_files[row];
            file.newName = Helpers::cleanFileName(computeNewName(file, seqIndex).trimmed());
            file.isNameCollision = false;
            file.nameCollisionReason.clear();
            seqIndex++;
        }
    }

    // 2. Which sources free up their name, and how often is each target claimed?
    QSet<QString> movingSources;
    QHash<QString, int> targetUseCount;
    for (int row : std::as_const(m_renameRowsList)) {
        if (row >= 0 && row < static_cast<int>(m_files.size())) {
            const auto &file = m_files[row];
            if (file.newName == file.name) continue; // unchanged -> keeps occupying its name
            movingSources.insert(renameKey(file.filePath));
            targetUseCount[renameKey(QDir(file.path).filePath(file.newName))]++;
        }
    }

    // 3. Determine conflicts
    int conflicts = 0;
    for (int row : std::as_const(m_renameRowsList)) {
        if (row < 0 || row >= static_cast<int>(m_files.size())) continue;
        auto &file = m_files[row];
        if (file.newName == file.name) continue;

        const QString target = QDir::cleanPath(QDir(file.path).filePath(file.newName));
        const QString targetKey = renameKey(target);
        QString reason;

        if (file.newName.isEmpty() || file.newName == "." || file.newName == "..") {
            reason = tr("Invalid file name");
        } else if (targetUseCount.value(targetKey) > 1) {
            reason = tr("Several files would get this name");
        } else {
            // Target exists on disk (including filtered-out files!) and is not freed by another
            // rename in the batch. Exception: a pure upper/lower case change on a
            // case-insensitive file system (the target physically is the file itself).
            const QFileInfo targetInfo(target);
            const bool occupied = targetInfo.exists() || targetInfo.isSymLink();
            const bool caseOnlyRename = target.compare(file.filePath, Qt::CaseInsensitive) == 0 && Helpers::isSameFile(file.filePath, target);
            if (occupied && !movingSources.contains(targetKey) && !caseOnlyRename) {
                reason = tr("A file with this name already exists");
            }
        }

        if (!reason.isEmpty()) {
            file.isNameCollision = true;
            file.nameCollisionReason = reason;
            conflicts++;
        }
    }

    m_renameConflictCount = conflicts;
    emit renameConflictsChanged(conflicts);
}

void CustomTableModel::setRenameRules(const RenameRules &rules) {
    if (rules.rpl.regex == true && (rules.rpl.match != m_rules.rpl.match || rules.rpl.regex != m_rules.rpl.regex || rules.rpl.matchCase != m_rules.rpl.matchCase)) {
        QRegularExpression::PatternOptions regexOptions = QRegularExpression::NoPatternOption;
        if (rules.rpl.matchCase == false) {
            regexOptions |= QRegularExpression::CaseInsensitiveOption;
        }
        m_RegexRulesCompiled = QRegularExpression(rules.rpl.match, regexOptions);
    }

    if (rules.flt != m_rules.flt) {
        m_rules = rules;
        populateModel_mkBatchRename(m_currentDirectoryPath, m_renameExtPathList);
        return;
    }

    m_rules = rules;

    if (m_files.empty() || rowCount() == 0)
        return;

    recalculateNewNames();

    emit dataChanged(createIndex(0, eColNewName), createIndex(rowCount() - 1, eColNewName), {Qt::DisplayRole});
}

QString CustomTableModel::computeNewName(const CustomFileInfo &item, int seqIndex) const {
    QString fileName = item.name;
    QString fileExt;

    QString workingCopy;
    if (m_rules.target == 2) {
        workingCopy = item.name;
    } else {
        int lastDot = item.name.lastIndexOf('.');
        if (lastDot > 0) {
            fileName = item.name.sliced(0, lastDot);
            fileExt  = item.name.sliced(lastDot + 1);
        }

        if (m_rules.target == 0) {
            workingCopy = fileName;
        } else if (m_rules.target == 1) {
            workingCopy = fileExt;
        }
    }

    //---------------------------------------------------------------------
    workingCopy = applyReplaceRule(workingCopy, m_rules.rpl);
    workingCopy = applyRemoveRule(workingCopy, m_rules.rmv);
    workingCopy = applyAddRule(workingCopy, m_rules.add);
    workingCopy = applyAddFolderNameRule(workingCopy, m_rules.afn, item.path);
    workingCopy = applyAddNumberingRule(workingCopy, m_rules.num, seqIndex);
    workingCopy = applyNumberManipulationRule(workingCopy, m_rules.man, seqIndex);
    workingCopy = applyCaseRule(workingCopy, m_rules.cas);
    //---------------------------------------------------------------------

    if (m_rules.target == 2) {
        return workingCopy;
    }

    if (m_rules.target == 0) {
        fileName = workingCopy;
    } else if (m_rules.target == 1) {
        fileExt = workingCopy;
    }


    if (!fileExt.isEmpty()) {
        return fileName + '.' + fileExt;
    }

    return fileName;
}

// Performs the batch rename as "all or nothing":
//  - If there are conflicts (see recalculateNewNames), it does not even start.
//  - Order by dependencies: whoever needs a target that is still occupied by another source
//    waits until that one has been renamed. Chains like 1->2, 2->3 thus work without intermediate names.
//  - Real cycles (a<->b, a->b->c->a) are resolved by moving one entry to a temporary name for a moment.
//  - If a rename fails, all previous ones are reverted in reverse order.
bool CustomTableModel::applyBatchRename(QString *errorMessage) {
    if (m_files.empty() || m_renameRowsList.isEmpty())
        return true;

    // Check again with the current disk state (something may have changed since the preview)
    recalculateNewNames();
    if (m_renameConflictCount > 0) {
        if (errorMessage) *errorMessage = tr("Renaming was not started because of %n name conflict(s).", nullptr, m_renameConflictCount);
        return false;
    }

    struct Op { int row; QString from; QString to; QString finalName; bool done = false; };
    std::vector<Op> ops;
    for (int row : std::as_const(m_renameRowsList)) {
        if (row < 0 || row >= static_cast<int>(m_files.size())) continue;
        const auto &file = m_files[row];
        if (file.newName == file.name) continue;
        ops.push_back({row, file.filePath, QDir::cleanPath(QDir(file.path).filePath(file.newName)), file.newName});
    }
    if (ops.empty()) return true;

    // Dependency graph: pendingSource[key(source)] = op; waiterFor[key(source)] = op whose target is this source
    QHash<QString, int> pendingSource;
    QHash<QString, int> waiterFor;
    QList<int> ready;
    for (int i = 0; i < static_cast<int>(ops.size()); ++i) {
        pendingSource.insert(renameKey(ops[i].from), i);
    }
    for (int i = 0; i < static_cast<int>(ops.size()); ++i) {
        const QString targetKey = renameKey(ops[i].to);
        auto it = pendingSource.constFind(targetKey);
        if (it != pendingSource.constEnd() && it.value() != i) {
            waiterFor.insert(targetKey, i);
        } else {
            ready.append(i);
        }
    }

    auto releaseSource = [&](const QString &sourcePath) {
        const QString key = renameKey(sourcePath);
        pendingSource.remove(key);
        auto w = waiterFor.find(key);
        if (w != waiterFor.end()) {
            ready.append(w.value());
            waiterFor.erase(w);
        }
    };

    QList<QPair<QString, QString>> journal; // Renames performed (from, to) for reverting
    QString failedFrom, failedTo, failedReason;
    int remaining = static_cast<int>(ops.size());

    while (remaining > 0) {
        if (ready.isEmpty()) {
            // Cycle: let a waiting entry whose source someone is waiting for move aside temporarily
            int c = -1;
            for (int i = 0; i < static_cast<int>(ops.size()); ++i) {
                if (!ops[i].done && waiterFor.contains(renameKey(ops[i].from))) { c = i; break; }
            }
            if (c == -1) { // Must not happen - abort to be safe instead of looping endlessly
                failedReason = tr("Internal error while ordering the renames");
                break;
            }

            const QString tempPath = uniqueTempRenamePath(ops[c].from);
            QFile f(ops[c].from);
            if (!f.rename(tempPath)) {
                failedFrom = ops[c].from; failedTo = tempPath; failedReason = f.errorString();
                break;
            }
            journal.append({ops[c].from, tempPath});
            const QString oldFrom = ops[c].from;
            ops[c].from = tempPath;
            releaseSource(oldFrom);
            continue;
        }

        const int i = ready.takeFirst();
        QFile f(ops[i].from);
        if (!f.rename(ops[i].to)) {
            failedFrom = ops[i].from; failedTo = ops[i].to; failedReason = f.errorString();
            break;
        }
        journal.append({ops[i].from, ops[i].to});
        ops[i].done = true;
        remaining--;
        releaseSource(ops[i].from);
    }

    if (remaining > 0) {
        // ERROR -> revert everything in reverse order
        QStringList notReverted;
        for (int k = static_cast<int>(journal.size()) - 1; k >= 0; --k) {
            if (!QFile::rename(journal[k].second, journal[k].first)) {
                notReverted << journal[k].second;
            }
        }

        if (errorMessage) {
            QString msg;
            if (!failedFrom.isEmpty()) {
                msg = tr("Renaming \"%1\" to \"%2\" failed: %3").arg(failedFrom, QFileInfo(failedTo).fileName(), failedReason);
            } else {
                msg = failedReason;
            }
            if (notReverted.isEmpty()) {
                msg += "\n\n" + tr("No files were renamed, all changes have been reverted.");
            } else {
                msg += "\n\n" + tr("The following files could not be reverted and still have their new or temporary name:") + "\n" + notReverted.join('\n');
            }
            *errorMessage = msg;
        }
        return false;
    }

    // SUCCESS -> update the metadata of all rows, one single dataChanged signal
    int minRow = -1, maxRow = -1;
    for (const Op &op : ops) {
        applyRenamedFileInfo(op.row, op.finalName, op.to);
        if (minRow == -1 || op.row < minRow) minRow = op.row;
        if (op.row > maxRow) maxRow = op.row;
    }
    emit dataChanged(index(minRow, 0), index(maxRow, columnCount() - 1));
    return true;
}

QString CustomTableModel::applyReplaceRule(const QString &input, const ReplaceRule &rule) const {
    // QString rule.match
    // QString rule.replace
    // bool rule.matchCase
    // bool rule.regex
    if (rule.match.isEmpty()) {
        return input;
    }

    QString workingCopy;
    workingCopy = input;

    if (!workingCopy.isEmpty()) {
        //---------------------------------------------------------------------
        if (rule.regex == false) {
            const Qt::CaseSensitivity cs = rule.matchCase ? Qt::CaseSensitive : Qt::CaseInsensitive;
            workingCopy.replace(rule.match, rule.replace, cs);
        } else {
            if (!m_RegexRulesCompiled.isValid()) {
                return input;
            }
            workingCopy.replace(m_RegexRulesCompiled, rule.replace);
        }
        //---------------------------------------------------------------------
    }

    return workingCopy;
}

QString CustomTableModel::applyRemoveRule(const QString &input, const RemoveRule &rule) const {
    // int rule.firstN
    // int rule.lastN
    // int rule.fromN
    // int rule.fromNcount

    QString workingCopy;
    workingCopy = input;

    if (!workingCopy.isEmpty()) {
        //---------------------------------------------------------------------
        if (rule.firstN > 0) {
            qsizetype count = std::min(static_cast<qsizetype>(rule.firstN), workingCopy.length());
            workingCopy.remove(0, count);
        }

        if (rule.fromNcount > 0) {
            qsizetype startIndex = rule.fromN - 1;

            if (startIndex < workingCopy.length()) {
                workingCopy.remove(startIndex, rule.fromNcount);
            }
        }

        if (rule.lastN > 0 && !workingCopy.isEmpty()) {
            qsizetype count = std::min(static_cast<qsizetype>(rule.lastN), workingCopy.length());
            workingCopy.chop(count);
        }
        //---------------------------------------------------------------------
    }

    return workingCopy;
}

QString CustomTableModel::applyMoveCopyRule(const QString &input, const MoveCopyRule &rule) const {
    if (rule.fromMode == 0 || rule.toMode == 0 || rule.fromPos <= 0 || input.isEmpty()) {
        return input;
    }

    // int rule.fromMode     // 0: None, 1: Copy first n, 2: Copy last n, 3: Move first n, 4: Move last n
    // int rule.fromPos
    // int rule.toMode       // 0: None, 1: To start, 2: To end, 3: To pos.
    // int rule.toPos
    // QString rule.separator

    QString workingCopy;
    workingCopy = input;

    if (!workingCopy.isEmpty()) {
        //---------------------------------------------------------------------
        QString remaining = workingCopy;

        // Limit the number of characters to copy/move to the file name length
        int count = std::min(rule.fromPos, static_cast<int>(remaining.size()));
        if (count <= 0) {
            return input;
        }

        QString extracted;

        // 1. Extract the section (and remove it from 'remaining' for "Move")
        switch (rule.fromMode) {
            case 1: // Copy first n
                extracted = remaining.first(count);
                break;
            case 2: // Copy last n
                extracted = remaining.last(count);
                break;
            case 3: // Move first n
                extracted = remaining.first(count);
                remaining.remove(0, count);
                break;
            case 4: // Move last n
                extracted = remaining.last(count);
                remaining.remove(remaining.size() - count, count);
                break;
            default:
                return input;
        }

        if (extracted.isEmpty()) {
            return input;
        }

        // 2. Insert the extracted text into the file name at the target position
        switch (rule.toMode) {
            case 1: // To start
                if (!remaining.isEmpty() && !rule.separator.isEmpty()) {
                    workingCopy = extracted + rule.separator + remaining;
                } else {
                    workingCopy = extracted + remaining;
                }
                break;

            case 2: // To end
                if (!remaining.isEmpty() && !rule.separator.isEmpty()) {
                    workingCopy = remaining + rule.separator + extracted;
                } else {
                    workingCopy = remaining + extracted;
                }
                break;

            case 3: { // To pos.
                int targetPos = std::clamp(rule.toPos - 1, 0, static_cast<int>(remaining.size()));

                QString textToInsert = extracted;
                if (!rule.separator.isEmpty()) {
                    textToInsert += rule.separator;
                }

                remaining.insert(targetPos, textToInsert);
                workingCopy = remaining;
                break;
            }

            default:
                return input;
        }
        //---------------------------------------------------------------------
    }

    return workingCopy;
}

QString CustomTableModel::applyAddRule(const QString &input, const AddRule &rule) const {
    QString workingCopy;
    workingCopy = input;

    //---------------------------------------------------------------------
    if (!rule.prefix.isEmpty()) {
        workingCopy.prepend(rule.prefix);
    }

    if (!rule.insertText.isEmpty() && rule.insertPos > 0) {
        qsizetype targetIndex = rule.insertPos - 1;

        targetIndex = std::clamp(targetIndex, static_cast<qsizetype>(0), workingCopy.length());

        workingCopy.insert(targetIndex, rule.insertText);
    }

    if (!rule.suffix.isEmpty()) {
        workingCopy.append(rule.suffix);
    }
    //---------------------------------------------------------------------

    return workingCopy;
}

QString CustomTableModel::applyAddNumberingRule(const QString &input, const AddNumberingRule &rule, int seqIndex) const {
    if (rule.mode == 0) {
        return input;
    }

    // int rule.mode = 0;    // 0: None, 1: Prefix, 2: Suffix
    // int rule.start = 1;
    // int rule.step = 1;
    // QString separator;

    QString workingCopy;
    workingCopy = input;

    //---------------------------------------------------------------------

    int currentNumber = rule.start + (seqIndex * rule.step);
    QString numberStr = QString::number(currentNumber);

    // Todo: Implement padding? Probably not needed, since can be solved by chained number manipulation step
    // QString numberStr = QString("%1").arg(currentNumber, rule.padding, 10, QChar('0'));

    if (rule.mode == 1) {
        workingCopy.prepend(numberStr + rule.separator);
    } else if (rule.mode == 2) {
        workingCopy.append(rule.separator + numberStr);
    }

    //---------------------------------------------------------------------

    return workingCopy;
}

QString CustomTableModel::applyAddDateRule(const QString &input, const AddDateRule &rule) const {
    QString workingCopy;
    workingCopy = input;

    //---------------------------------------------------------------------

    //---------------------------------------------------------------------

    return workingCopy;
}

QString CustomTableModel::applyAddFolderNameRule(const QString &input, const AddFolderNameRule &rule, const QString &path) const {
    if (rule.mode == 0 || rule.levels <= 0 || path.isEmpty()) {
        return input;
    }

    // int rule.mode            // 0: None, 1: Prefix, 2: Suffix
    // QString rule.separator
    // int rule.levels

    // Unify and clean up path separators
    QString cleanPath = QDir::cleanPath(path);
    QStringList parts = cleanPath.split('/', Qt::SkipEmptyParts);

    if (parts.isEmpty()) {
        return input;
    }

    // Determine the number of desired folder levels (limited to the available folders)
    int levelsToTake = std::min(rule.levels, static_cast<int>(parts.size()));
    QStringList selectedFolders = parts.mid(parts.size() - levelsToTake, levelsToTake);

    // Join the folder names with the separator
    QString folderString = selectedFolders.join(rule.separator);
    if (folderString.isEmpty()) {
        return input;
    }

    QString workingCopy;
    workingCopy = input;

    // ---------------------------------------------------------------------
    // 1: Prefix (prepend)
    if (rule.mode == 1) {
        if (!workingCopy.isEmpty() && !rule.separator.isEmpty()) {
            workingCopy = folderString + rule.separator + workingCopy;
        } else {
            workingCopy = folderString + workingCopy;
        }
    }
    // 2: Suffix (append)
    else if (rule.mode == 2) {
        if (!workingCopy.isEmpty() && !rule.separator.isEmpty()) {
            workingCopy = workingCopy + rule.separator + folderString;
        } else {
            workingCopy = workingCopy + folderString;
        }
    }
    //---------------------------------------------------------------------

    return workingCopy;
}


QString CustomTableModel::applyNumberManipulationRule(const QString &input, const NumberManipulationRule &rule, int seqIndex) const {
    if (!rule.newEnabled && !rule.addLeadEnabled) {
        return input;
    }

    // int rule.targetGroup
    // bool rule.addLeadEnabled
    // int rule.addLeadCount
    // bool rule.newEnabled
    // int rule.newStart
    // int rule.newStep

    QString workingCopy = input;

    if (!workingCopy.isEmpty()) {
        static const QRegularExpression digitRegex(QStringLiteral("\\d+"));
        QRegularExpressionMatchIterator it = digitRegex.globalMatch(workingCopy);

        int currentGroup = 1;
        QRegularExpressionMatch match;

        while (it.hasNext()) {
            QRegularExpressionMatch m = it.next();
            if (currentGroup == rule.targetGroup) {
                match = m;
                break;
            }
            currentGroup++;
        }

        if (match.hasMatch()) {
            QString numStr = match.captured(0);

            if (rule.newEnabled) {
                int newNumber = rule.newStart + (seqIndex * rule.newStep);
                numStr = QString::number(newNumber);
            }

            // 2. Apply leading zeros / adjust the length (if enabled)
            if (rule.addLeadEnabled && rule.addLeadCount > 0) {
                if (numStr.length() < rule.addLeadCount) {
                    numStr = numStr.rightJustified(rule.addLeadCount, QChar('0'));
                } else if (numStr.length() > rule.addLeadCount) {
                    // Remove leading zeros as long as the target length is exceeded
                    while (numStr.length() > rule.addLeadCount && numStr.startsWith('0')) {
                        numStr.remove(0, 1);
                    }
                }
            }

            // Replace the original digit sequence in the file name
            workingCopy.replace(match.capturedStart(), match.capturedLength(), numStr);
        }
    }

    return workingCopy;
}


QString CustomTableModel::applyCaseRule(const QString &input, const CaseRule &rule) const {
    if (rule.mode == 0) {
        return input;
    }

    // int rule.mode            // 0: Same, 1: Lower-Case, 2: Upper-Case, 3: Title-Case, 4: Sentence

    QString workingCopy;
    workingCopy = input;

    if (!workingCopy.isEmpty()) {
        //---------------------------------------------------------------------
        switch (rule.mode) {
            case 1:
                workingCopy = workingCopy.toLower();
                break;
            case 2:
                workingCopy = workingCopy.toUpper();
                break;
            case 3:
                workingCopy = Helpers::toTitleCase(workingCopy);
                break;
            case 4:
                workingCopy = Helpers::toSentenceCase(workingCopy);
                break;
            default:
                break;
        }
        //---------------------------------------------------------------------
    }

    return workingCopy;
}

bool CustomTableModel::isDirectory(int row) const {
    if (row < 0 || row >= static_cast<int>(m_files.size())) {
        return false;
    }

    return m_files[row].isDir;
}

Qt::DropActions CustomTableModel::supportedDragActions() const {
    return Qt::CopyAction | Qt::MoveAction | Qt::LinkAction;
}

Qt::DropActions CustomTableModel::supportedDropActions() const {
    return Qt::CopyAction | Qt::MoveAction | Qt::LinkAction;
}

QStringList CustomTableModel::mimeTypes() const {
    QStringList types;
    types << "text/uri-list"; // The default MIME type for files in the OS
    return types;
}

QMimeData* CustomTableModel::mimeData(const QModelIndexList &indexes) const {
    QMimeData *mimeData = new QMimeData();

    QList<QUrl> urls;
    QSet<int> processedRows; // Prevents duplicates when multiple columns are selected

    for (const QModelIndex &index : indexes) {
        if (index.isValid()) {
            processedRows.insert(index.row());
        }
    }

    for (int row : processedRows) {
        if (row >= 0 && row < static_cast<int>(m_files.size())) {
            QString absolutePath = m_files[row].filePath;
            urls.append(QUrl::fromLocalFile(absolutePath));
        }
    }

    mimeData->setUrls(urls);

    return mimeData;
}

bool CustomTableModel::canDropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column, const QModelIndex &parent) const {
    // Basic check: if there are no URLs in the drag, reject right away
    if (!data || !data->hasUrls()) {
        return false;
    }

    // Do not allow dropping "between" items!
    // If row != -1, the mouse is hovering over the edges of an item (above/below).
    // With 'false', Qt hides the insertion line and switches to "OnItem" mode right away.
    if (row != -1) {
        return false;
    }

    // CASE 1: The mouse hovers over the empty background (parent is invalid)
    if (!parent.isValid()) {
        return true;
    }

    // CASE 2: The mouse hovers over a specific item (e.g. a folder)
    int parentRow = parent.row();
    if (parentRow >= 0 && parentRow < static_cast<int>(m_files.size())) {

        // We only check all this if the target item is a folder
        if (m_files[parentRow].isDir) {
            // This is the absolute path of the folder the mouse is currently hovering over:
            QString targetFolderPath = m_files[parentRow].filePath;

            // Loop over all files/folders attached to the mouse cursor
            QList<QUrl> urls = data->urls();
            for (const QUrl &url : std::as_const(urls)) {
                QString draggedPath = url.toLocalFile();

                // PROTECTION 1: Dropping a folder onto itself (identical path)
                // Prevents e.g. dropping /Projekte/A onto /Projekte/A in instance 2
                if (draggedPath == targetFolderPath) {
                    return false; // Block right away! Shows the "not allowed" sign.
                }

                // PROTECTION 2: Dropping a folder into one of its own subfolders
                // Prevents the physically impossible move of /A into /A/B/C
                if (targetFolderPath.startsWith(draggedPath + "/")) {
                    return false; // Block as well!
                }
            }
        }
    }
    return QAbstractTableModel::canDropMimeData(data, action, row, column, parent);
}

bool CustomTableModel::dropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column, const QModelIndex &parent) {
    Q_UNUSED(row);
    Q_UNUSED(column);

    if (action == Qt::IgnoreAction) return true;
    if (!data || !data->hasUrls()) return false;

    // Default target: the currently opened directory
    QString targetDir;
	if (!m_currentDirectoryPath.isEmpty()) {
		targetDir = m_currentDirectoryPath;
	}
	
    // If the drop happened on a valid item and it is a folder,
    // we move/copy the files INTO this subfolder!
    if (parent.isValid()) {
        int parentRow = parent.row();
        if (parentRow >= 0 && parentRow < static_cast<int>(m_files.size()) && m_files[parentRow].isDir) {
            targetDir = m_files[parentRow].filePath;
        }
    }

    if (targetDir.isEmpty()) return true;

    QList<QUrl> urlList = data->urls();
    if (!urlList.first().isLocalFile()) {
        for (const QUrl &url : data->urls()) {
            if (!url.isLocalFile()) {
                QString webUrlStr = url.toString();

                QString webTitle = "";

                if (data->hasFormat("text/x-moz-url-desc")) {
                    QByteArray rawDesc = data->data("text/x-moz-url-desc");

                    // Firefox delivers this as UTF-16 (2 bytes per character).
                    // We explicitly convert the raw data from UTF-16 into a QString:
                    webTitle = QString::fromUtf16(
                        reinterpret_cast<const char16_t*>(rawDesc.constData()),
                        rawDesc.size() / 2
                        );

                    // Since such strings often end with a null terminator '\0',
                    // we cut it off together with any whitespace.
                    int nullPos = webTitle.indexOf(QChar('\0'));
                    if (nullPos != -1) {
                        webTitle = webTitle.left(nullPos);
                    }
                    webTitle = webTitle.trimmed();
                }

                // Fallback in case it was not a Firefox/Mozilla browser or the field is missing
                if (webTitle.isEmpty() && data->hasText()) {
                    // If it is a URL, we extract the host name, otherwise we take the text
                    QUrl url(data->text());
                    webTitle = url.isValid() && !url.isLocalFile() ? url.host() : data->text();
                }

                Helpers::createInternetShortcut(webUrlStr, targetDir, webTitle);
            }
        }

        return true;
    }

    emit filesDropped(data->urls(), targetDir, action);

    return true;
}

QPixmap CustomTableModel::generateDummyThumb(const QString &dummyName) const {
    QIcon trueIcon;

    if (dummyName == "//dir//") {
        trueIcon = m_iconProvider.icon(QFileIconProvider::Folder);
    } else {
        QFileInfo dummyInfo(dummyName);
        trueIcon = m_iconProvider.icon(dummyInfo);
    }

    QPixmap icon48 = trueIcon.pixmap(48, 48);
    QPixmap canvas(96, 96);
    canvas.fill(Qt::transparent);

    {
        QPainter painter(&canvas);
        int x = (canvas.width() - icon48.width()) / 2;
        int y = (canvas.height() - icon48.height()) / 2;
        painter.drawPixmap(x, y, icon48);
        painter.setPen(QColor(255, 255, 255, 27));

        // IMPORTANT QT QUIRK:
        // For historical reasons, with a 1px pen drawRect(x, y, w, h) in Qt draws
        // a rectangle that is w+1 pixels wide and h+1 pixels high.
        // For the border to lie exactly on pixels 0 to 95, we have to subtract 1.
        painter.drawRect(0, 0, canvas.width() - 1, canvas.height() - 1);

        // The painter is closed and saved automatically at the end of the {} scope
    }

    return canvas;
}

void CustomTableModel::populateModel_mkBatchRename(const QString &dirPath, const QStringList &externalPathList) {
    m_renameExtPathList = externalPathList;
    m_abortSearch.store(false);
    std::vector<CustomFileInfo> newFiles;
    m_currentDirectoryPath = dirPath;
    m_renameRowsList.clear();
    m_renameRowsSet.clear();
    if (m_renameConflictCount != 0) {
        m_renameConflictCount = 0;
        emit renameConflictsChanged(0);
    }

    QString searchStringFilename = m_rules.flt.filter;
    QStringList searchStringFilenameSplit = m_rules.flt.filter.split(' ', Qt::SkipEmptyParts);
    bool bSearchStringFilenameEmpty = m_rules.flt.filter.trimmed().isEmpty();
    Qt::CaseSensitivity caseSensitivityFilename = m_rules.flt.matchCase ? Qt::CaseSensitive : Qt::CaseInsensitive;

    if (!dirPath.isEmpty()) {
        int iAnchorPathLength = dirPath.length();
        if (!dirPath.endsWith(QDir::separator())) {
            iAnchorPathLength++;
        }

        QDir::Filters dirFilter = QDir::Hidden | QDir::System | QDir::NoDotAndDotDot;

        if (m_rules.flt.showFiles) {
            dirFilter = dirFilter | QDir::Files;
        }

        if (m_rules.flt.showFolders) {
            dirFilter = dirFilter | QDir::Dirs;
        }

        QDirIterator::IteratorFlag itFlags = QDirIterator::NoIteratorFlags;
        if (m_rules.flt.showRecursive) {
            itFlags = QDirIterator::Subdirectories;
        }

        QDirIterator it(dirPath, dirFilter, itFlags);
        while (it.hasNext()) {
            it.next();

            QCoreApplication::processEvents();

            if (m_abortSearch.load()) {
                break;
            }

            QFileInfo fileInfo = it.fileInfo();

            if (!bSearchStringFilenameEmpty) {
                if (Helpers::getNameMatchQuality(fileInfo.fileName(), fileInfo.path(), searchStringFilename, searchStringFilenameSplit, caseSensitivityFilename) == 0) {
                    continue;
                }
            }
            CustomFileInfo info = createCustomFileInfo(fileInfo, iAnchorPathLength);

            newFiles.push_back(info);
        }
    } else if (!externalPathList.isEmpty()) {
        for (const QString &filePath : externalPathList) {

            QCoreApplication::processEvents();

            if (m_abortSearch.load()) {
                break;
            }

            QFileInfo fileInfo(filePath);

            if (!fileInfo.exists()) {
                continue;
            }

            // Filtering by files and folders
            if (fileInfo.isFile() && !m_rules.flt.showFiles) {
                continue;
            }
            if (fileInfo.isDir() && !m_rules.flt.showFolders) {
                continue;
            }

            // Filtering by file name/search term
            if (!bSearchStringFilenameEmpty) {
                if (Helpers::getNameMatchQuality(fileInfo.fileName(), fileInfo.path(), searchStringFilename, searchStringFilenameSplit, caseSensitivityFilename) == 0) {
                    continue;
                }
            }

            // iAnchorPathLength is 0, since there is no common source directory (dirPath)
            CustomFileInfo info = createCustomFileInfo(fileInfo, 0);

            newFiles.push_back(info);
        }
    }

    recalculateNewNames();

    beginResetModel();
    m_files = std::move(newFiles);
    endResetModel();
}

void CustomTableModel::populateModel_mkFileSearch(const QString &searchDir, const QString &searchStringFilename, const QString &searchStringContent, bool bRegExFilename, bool bRegExContent, bool bFilenameCaseSensitive, bool bContentCaseSensitive, Qt::CheckState cbDirState, const QSet<QString> &contentSearchExts) {
    m_abortSearch.store(false);
    std::vector<CustomFileInfo> newFiles;
    //m_currentDirectoryPath = searchDir; // commented out, since it would allow dropping on background (after first search)

    uint iItemsFound = 0;
    uint iNameMatched = 0;
    uint iContentMatched = 0;
    bool bSearchInterrupted = false;
    bool bSearchStringFilenameEmpty = searchStringFilename.trimmed().isEmpty();
    bool bsearchStringContentEmpty = searchStringContent.trimmed().isEmpty();

    int iAnchorPathLength = searchDir.length();
    if (!searchDir.endsWith(QDir::separator())) {
        iAnchorPathLength++;
    }

    QDir::Filters searchFlags = QDir::Hidden | QDir::NoDotAndDotDot | QDir::System;

    if (cbDirState == Qt::Unchecked) {
        searchFlags = searchFlags | QDir::Files;
    } else if (cbDirState == Qt::PartiallyChecked) {
        searchFlags = searchFlags | QDir::Files | QDir::Dirs;
    } else /* if (m_cbDirState == Qt::Checked) */ {
        searchFlags = searchFlags | QDir::Dirs;
    }

    QRegularExpression::PatternOptions reOptionsFilename = QRegularExpression::NoPatternOption;
    Qt::CaseSensitivity caseSensitivityFilename = Qt::CaseSensitive;
    if (!bFilenameCaseSensitive) {
        reOptionsFilename |= QRegularExpression::CaseInsensitiveOption;
        caseSensitivityFilename = Qt::CaseInsensitive;
    }

    QRegularExpression qreFileName(searchStringFilename, reOptionsFilename);
    if (!qreFileName.isValid() && bRegExFilename) {
        // The user typed an invalid Regex (e.g., unmatched brackets)
        qDebug() << "Invalid name field Regex:" << qreFileName.errorString();
        return;
    }

    QRegularExpression::PatternOptions reOptionsContent = QRegularExpression::NoPatternOption;
    Qt::CaseSensitivity caseSensitivityContent = Qt::CaseSensitive;
    if (!bContentCaseSensitive) {
        reOptionsContent |= QRegularExpression::CaseInsensitiveOption;
        caseSensitivityContent = Qt::CaseInsensitive;
    }

    QRegularExpression qreContent(searchStringContent, reOptionsContent);
    if (!qreContent.isValid() && bRegExContent) {
        // The user typed an invalid Regex (e.g., unmatched brackets)
        qDebug() << "Invalid content field Regex:" << qreContent.errorString();
        return;
    }

    QStringList searchStringFilenameSplit = searchStringFilename.split(' ', Qt::SkipEmptyParts);

    int nameMatchQuality = -1;
    int contentMatchCount = -1;

    // Timer for periodic updates (1000 ms)
    QElapsedTimer progressTimer;
    progressTimer.start();

    QDirIterator it(searchDir, searchFlags, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();

        QCoreApplication::processEvents();

        if (m_abortSearch.load()) {
            bSearchInterrupted = true;
            break;
        }

        iItemsFound++;

        QFileInfo fileInfo = it.fileInfo();

        if (!bSearchStringFilenameEmpty) {
            if (bRegExFilename) {
                nameMatchQuality = Helpers::getRegExNameMatchQuality(fileInfo, qreFileName);
            } else {
                nameMatchQuality = Helpers::getNameMatchQuality(fileInfo.fileName(), fileInfo.path(), searchStringFilename, searchStringFilenameSplit, caseSensitivityFilename);
            }

            if (nameMatchQuality == 0) {
                // When discarding, check before the continue whether 1000 ms have passed
                if (progressTimer.elapsed() >= 1000) {
                    emit searchProgress(iItemsFound, iNameMatched, iContentMatched);
                    progressTimer.restart();
                }
                continue;
            }
        }

        if (!bsearchStringContentEmpty) {
            if (bRegExContent) {
                contentMatchCount = Helpers::getRegExContentMatchCount(fileInfo, qreContent, contentSearchExts);
            } else {
                contentMatchCount = Helpers::getContentMatchCount(fileInfo, searchStringContent, caseSensitivityContent, contentSearchExts);
            }

            if (contentMatchCount == 0) {
                // When discarding, check before the continue whether 1000 ms have passed
                if (progressTimer.elapsed() >= 1000) {
                    emit searchProgress(iItemsFound, iNameMatched, iContentMatched);
                    progressTimer.restart();
                }
                continue;
            }

            iContentMatched += contentMatchCount;
        }

        iNameMatched++;

        CustomFileInfo info = createCustomFileInfo(fileInfo, iAnchorPathLength);
        info.nameMatchQuality = nameMatchQuality;
        info.contentMatchCount = contentMatchCount;

        newFiles.push_back(info);

        // Check whether 1000 ms have passed since the last signal
        if (progressTimer.elapsed() >= 1000) {
            emit searchProgress(iItemsFound, iNameMatched, iContentMatched);
            progressTimer.restart();
        }
    }

    beginResetModel();
    m_files = std::move(newFiles);
    endResetModel();

    emit searchFinished(iItemsFound, iNameMatched, iContentMatched, bSearchInterrupted);
}

void CustomTableModel::populateModel_mkLauncher(const QStringList &searchFolders, const QString &searchString, QStringList recentOpenList) {
    m_abortSearch.store(false);
    std::vector<CustomFileInfo> newFiles;

    std::reverse(recentOpenList.begin(), recentOpenList.end());
    QStringList searchStringSplit = searchString.split(' ', Qt::SkipEmptyParts);

    uint iItemsFound = 0;
    uint iNameMatched = 0;
    uint iContentMatched = 0;
    bool bSearchInterrupted = false;

    int nameMatchQuality = -1;

    static const QString currentDesktop = qEnvironmentVariable("XDG_CURRENT_DESKTOP").toUpper();
    static const QStringList appDirs = QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation);
    // Note to self: appDirs contains the path in the order from user specific to system defaults
    // We read user specific first and remember which ones we already got in "seenIds" to prevent overwriting with system defaults
    QSet<QString> seenIds;

    const QString localeName1 = QLocale::system().name();
    const QString localeName2 = localeName1.left(2);
    static const QString localKeyLong = QString("Name[%1]=").arg(localeName1); // e.g. Name[de_DE]
    static const QString localKeyShort = QString("Name[%1]=").arg(localeName2); // e.g. Name[de]

    for (const QString &dirPath : appDirs) {
        QDirIterator iter(dirPath, {"*.desktop"}, QDir::NoDotAndDotDot | QDir::Files);   // Parameters: path, name filter, filter flags

        while (iter.hasNext()) {
            iter.next();

            if (m_abortSearch.load()) {
                bSearchInterrupted = true;
                break;
            }

            QString fileName = iter.fileName(); // The ID (e.g. "firefox.desktop")

            if (!seenIds.contains(fileName)) {
                seenIds.insert(fileName);

                QString filePath = iter.filePath();
                QFile file(filePath);
                if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                    QTextStream in(&file);
                    in.setEncoding(QStringConverter::Utf8);

                    QString iniName, iniNameLocalised, iniGenericName, iniKeywords;
                    bool bShowApplication = true;
                    bool bForceHidden = false;
                    uint iSystemSettings = 0;
                    bool bInMainSection = false;

                    while (!in.atEnd()) {
                        QString line = in.readLine().trimmed();

                        if (line.startsWith('[') && line.endsWith(']')) {
                            if (bInMainSection) break; // we're already inside the main "[Desktop Entry]" and hit another section -> break

                            bInMainSection = (line == "[Desktop Entry]");
                            continue;
                        }

                        if (!bInMainSection || line.startsWith('#')) continue;

                        if (line.startsWith("Name=")) iniName = line.mid(5);
                        else if (line.startsWith(localKeyLong)) iniNameLocalised = line.mid(localKeyLong.size());
                        else if (line.startsWith(localKeyShort) && iniNameLocalised.isEmpty()) iniNameLocalised = line.mid(localKeyShort.size());
                        else if (line.startsWith("GenericName=")) iniGenericName = line.mid(12);
                        else if (line.startsWith("NoDisplay=true")) bShowApplication = false;
                        else if (line.startsWith("Exec=systemsettings")) {
                            iSystemSettings++;
                            if (line == "Exec=systemsettings") {
                                iSystemSettings++;
                            }
                        } else if (line.startsWith("OnlyShowIn=")) {
                            QStringList onlyShowIn = line.mid(11).split(';', Qt::SkipEmptyParts);
                            if (!onlyShowIn.isEmpty()) {
                                if (!onlyShowIn.contains(currentDesktop)) {
                                    bForceHidden = true;
                                }
                            }
                        } else if (line.startsWith("NotShowIn=")) {
                            QStringList notShowIn = line.mid(10).split(';', Qt::SkipEmptyParts);
                            qDebug() << fileName << "notShowIn:" << notShowIn << "currentDesktop:" << currentDesktop;
                            if (!notShowIn.isEmpty()) {
                                if (notShowIn.contains(currentDesktop)) {
                                    bForceHidden = true;
                                }
                            }
                        }
                    }

                    if ((bShowApplication || iSystemSettings) && !bForceHidden) {
                        QString alternativeName = iniNameLocalised.isEmpty() ? iniName
                                                                             : iniNameLocalised + " " + iniName;

                        if (!iniGenericName.isEmpty()) {
                            alternativeName += " " + iniGenericName;
                        }

                        QFileInfo fileInfo = iter.fileInfo();

                        nameMatchQuality = Helpers::getNameMatchQuality(alternativeName, fileInfo.path(), searchString, searchStringSplit, Qt::CaseInsensitive);
                        if (nameMatchQuality == 0) {
                            continue;
                        } else {
                            int penalty = recentOpenList.indexOf(fileInfo.filePath());
                            nameMatchQuality = ((nameMatchQuality * 100) - 1) - penalty;
                        }

                        iNameMatched++;

                        CustomFileInfo info = createCustomFileInfo(fileInfo);

                        QString displayName = iniNameLocalised.isEmpty() ? iniName : iniNameLocalised;
                        if (iSystemSettings == 1) {
                            info.displayName = tr("System Settings:") + ' ' + displayName;
                        } else {
                            // If this is *not* systemsettings or is *exactly* systemsettings, don't prepend the term!
                            info.displayName = displayName;
                        }

                        info.nameMatchQuality = nameMatchQuality;

                        newFiles.push_back(info);
                    }
                }
            }
        }

        if (bSearchInterrupted == true) {
            break;
        }
    }

    if (bSearchInterrupted == false) {
        // Search through list of user-defined folders
        for (const QString &searchDir : std::as_const(searchFolders)) {
            QDirIterator iter(searchDir, QDir::NoDotAndDotDot | QDir::System | QDir::Files, QDirIterator::Subdirectories);
            while (iter.hasNext()) {
                iter.next();

                if (iItemsFound % 200 == 0) { QCoreApplication::processEvents(); } // needed so we notice changed input box text

                if (m_abortSearch.load()) {
                    bSearchInterrupted = true;
                    break;
                }

                iItemsFound++;

                QFileInfo fileInfo = iter.fileInfo();

                nameMatchQuality = Helpers::getNameMatchQuality(fileInfo.fileName(), fileInfo.path(), searchString, searchStringSplit, Qt::CaseInsensitive);
                if (nameMatchQuality == 0) {
                    continue;
                } else {
                    int penalty = recentOpenList.indexOf(fileInfo.filePath());
                    nameMatchQuality = ((nameMatchQuality * 100) - 1) - penalty;
                }

                iNameMatched++;

                CustomFileInfo info = createCustomFileInfo(fileInfo);
                info.nameMatchQuality = nameMatchQuality;

                newFiles.push_back(info);
            }
        }
    }

    if (bSearchInterrupted == false) {
        beginResetModel();
        m_files = std::move(newFiles);
        endResetModel();
    }

    emit searchFinished(iItemsFound, iNameMatched, iContentMatched, bSearchInterrupted);
}

void CustomTableModel::populateModel_mkFolderWidget(const QString &dirPath) {
    m_abortSearch.store(false);
    std::vector<CustomFileInfo> newFiles;
    m_currentDirectoryPath = dirPath;

    QDir::Filters filters = QDir::Files | QDir::Dirs | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot;
    QDirIterator it(dirPath, filters, QDirIterator::NoIteratorFlags);
    while (it.hasNext()) {
        it.next();
        CustomFileInfo info = createCustomFileInfo(it.fileInfo());

        newFiles.push_back(info);
    }

    beginResetModel();
    m_files = std::move(newFiles);
    endResetModel();
}

void CustomTableModel::removeFilePaths(const QSet<QString> &paths) {
    if (paths.isEmpty()) return;

    // We walk backwards through our internal data
    for (int i = m_files.size() - 1; i >= 0; --i) {
        const CustomFileInfo &file = m_files[i];
        QString currentPath = file.filePath;

        if (paths.contains(currentPath)) {
            // Tell Qt that we are about to delete exactly this row
            beginRemoveRows(QModelIndex(), i, i);

            m_files.erase(m_files.begin() + i);

            // Optional: also remove it from the CRC cache, since the file is gone
            m_CrcCache.remove(currentPath);

            endRemoveRows(); // The view now updates itself cleanly and automatically
        }
    }
}

CustomFileInfo CustomTableModel::createCustomFileInfo(const QFileInfo &fileInfo, int anchorPathLength) const {
    CustomFileInfo info;
    info.name = fileInfo.fileName();
    info.isDir = fileInfo.isDir();
    info.isSymbolicLink = fileInfo.isSymbolicLink();

    info.isHidden = fileInfo.isHidden();
    info.size = info.isDir ? 0 : fileInfo.size();
    info.date = fileInfo.lastModified();

    info.iconIndex = 0;
    info.displayName = info.name;

    QString ext;
    if (!info.isDir) {
        int lastDot = info.name.lastIndexOf('.');
        if (lastDot > 0) {
            ext = info.name.sliced(lastDot + 1).toLower();

            if (!m_settings->showFileExtensions && m_settings->hideExts.contains(ext)) {
                info.displayName = info.name.sliced(0, lastDot);
            }
        }
    }

    info.type = ext;

    info.isExecutable = fileInfo.isExecutable();
    info.path = fileInfo.absolutePath();
    info.filePath = fileInfo.absoluteFilePath();
    info.anchorPathLength = anchorPathLength;
    return info;
}
