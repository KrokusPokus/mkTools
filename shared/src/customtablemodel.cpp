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
#include <QStorageInfo>
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
                            return QDir::toNativeSeparators(file.path.sliced(file.anchorPathLength));
                        }
                        else {
                            return QVariant();
                        }
			        }
            		return QDir::toNativeSeparators(file.path);
            	}
            case eColSize: {
                    if (file.isDir && m_currentDirectoryPath != "drives://") {
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
        if (col != eColName) return QVariant(); // Icons nur in der Namensspalte anzeigen

        QString absolutePath = file.filePath;
#ifdef Q_OS_WIN
        if (!file.drivePath.isEmpty()) {
            absolutePath = file.drivePath;
        }
#endif
        if (m_viewMode == ViewMode::Thumbnail) {
            auto itThumb = m_individualThumbnailCache.find(absolutePath);
            if (itThumb != m_individualThumbnailCache.end()) {
                return itThumb.value().thumbnail; // Liefert das fertige 96x96 Bild
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
            // SymLinks haben Vorrang vor Datei/Ordner und vor individuellen Icons:
            // ein festes Icon, einmalig erzeugt und im allgemeinen Cache abgelegt.
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

    // Für die vertikalen Header (Zeilennummern 1, 2, 3...) nutzen wir das Standardverhalten von Qt
    return QAbstractTableModel::headerData(section, orientation, role);
}

Qt::ItemFlags CustomTableModel::flags(const QModelIndex &index) const {
    if (!index.isValid()) {                             // No index -> no item -> background!
        if (!m_currentDirectoryPath.isEmpty() && m_currentDirectoryPath != "drives://") {
        	return Qt::NoItemFlags | Qt::ItemIsDropEnabled;  // Background needs to have Qt::ItemIsDropEnabled to become a valid drop target.
    	}
        return Qt::NoItemFlags;
    }

    // Die Standard-Flags holen (Selectable, Enabled)
    Qt::ItemFlags cellFlags = QAbstractTableModel::flags(index);

    if (index.column() == 0) {
        cellFlags |= Qt::ItemIsEditable | Qt::ItemIsDragEnabled;

        // Nur Ordner dürfen als Drop-Ziel dienen
        int row = index.row();
        if (row >= 0 && row < static_cast<int>(m_files.size())) {
            if (m_files[row].isDrive) {
                cellFlags &= ~Qt::ItemIsEditable;
            }
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

    // View für die einzelne Zeile benachrichtigen
    emit dataChanged(index, index, {Qt::DisplayRole, Qt::EditRole});

    return true;
}

bool CustomTableModel::renameFileInternal(int row, const QString &rawNewName) {
    if (row < 0 || row >= static_cast<int>(m_files.size())) {
        return false;
    }

    QString newName = Helpers::cleanFileName(rawNewName.trimmed());

    // Abbrechen, wenn der Name leer ist oder sich gar nicht geändert hat
    if (newName.isEmpty() || newName == m_files[row].name) {
        return false;
    }

    // 1. Datei auf der Festplatte umbenennen
    QString oldPath = m_files[row].filePath;
    QString newPath = QDir::cleanPath(m_files[row].path + "/" + newName);

    if (!QFile::rename(oldPath, newPath)) {
        return false;
    }

    // 2. Struct im Speicher aktualisieren
    applyRenamedFileInfo(row, newName, newPath);
    return true;
}

// Aktualisiert die Metadaten einer Zeile, nachdem die Datei auf der Platte umbenannt wurde
void CustomTableModel::applyRenamedFileInfo(int row, const QString &newName, const QString &newPath) {
    CustomFileInfo &info = m_files[row];
    info.name = newName;
    info.displayName = newName;
    info.filePath = newPath;

    QString ext;
    if (!info.isDir) {
        int lastDot = newName.lastIndexOf('.');
        if (lastDot > 0) {
            ext = info.name.sliced(lastDot + 1).toLower(); // falls sich der Typ geändert hat

            if (!m_settings->showFileExtensions && m_settings->knownExts.contains(ext)) {
                info.displayName = newName.sliced(0, lastDot);
            }
        }
    }

    info.type = ext;

#ifdef Q_OS_WIN
    info.isExecutable = (ext == "exe" || ext == "scr");
#endif
}

void CustomTableModel::sort(int column, Qt::SortOrder order) {
    // We're not using any of it. The Proxy-Modell will take over this job.
    Q_UNUSED(column);
    Q_UNUSED(order);
}

// Daten neu setzen und der View signalisieren, dass sie sich neu zeichnen muss
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

    const CustomFileInfo &file = m_files[row];

#ifdef Q_OS_WIN
    if (m_currentDirectoryPath == "drives://") {
        return file.drivePath;
    } else
#endif
    {
        return file.filePath;
    }
}

// Festes Icon für SymLinks (Dateien und Ordner gleichermaßen).
// Einzige Stelle, an der die Icon-Quelle festgelegt wird.
QIcon CustomTableModel::symlinkIcon() {
    // PROVISORISCH: Theme-Emblem (Linux), Fallback auf das Qt-Style-Icon (u.a. Windows).
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
    // Wenn das Datum im Cache ungültig ist (Laufwerk-Dummy) oder exakt übereinstimmt -> Aktuell bzw. passt schon!
    return !it.value().lastModified.isValid() || it.value().lastModified == currentDate;
}

void CustomTableModel::addPathIcon(const QString &path, const QIcon &icon, const QDateTime &lastModified, int row) {
    m_individualIconCache.insert(path, {icon, lastModified});

    // Sofort die View benachrichtigen, dass genau DIESE Zeile (Spalte 0) neu gezeichnet werden muss
    QModelIndex idx = createIndex(row, 0);
    emit dataChanged(idx, idx, {Qt::DecorationRole});
}

bool CustomTableModel::isThumbnailUpToDate(const QString &path, const QDateTime &currentDate) const {
    auto it = m_individualThumbnailCache.find(path);
    if (it == m_individualThumbnailCache.end()) {
        return false;
    }
    // Wenn das Datum im Cache ungültig ist (Laufwerk-Dummy) oder exakt übereinstimmt -> Aktuell bzw. passt schon!
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
    // Wenn das Datum im Cache ungültig ist (Laufwerk-Dummy) oder exakt übereinstimmt -> Aktuell bzw. passt schon!
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
    // Wir wandeln die Liste zur performanten Suche in ein QSet um
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
// Vergleichsschlüssel für Pfade: unter Windows ist das Dateisystem case-insensitiv
QString renameKey(const QString &path) {
#ifdef Q_OS_WIN
    return QDir::cleanPath(path).toLower();
#else
    return QDir::cleanPath(path);
#endif
}

// Noch nicht existierender temporärer Name neben 'path' (zum Auflösen von Umbenennungs-Zyklen)
QString uniqueTempRenamePath(const QString &path) {
    while (true) {
        const QString candidate = QStringLiteral("%1.mkren-%2").arg(path).arg(QRandomGenerator::global()->generate(), 8, 16, QChar('0'));
        const QFileInfo info(candidate);
        if (!info.exists() && !info.isSymLink()) return candidate;
    }
}
} // namespace

void CustomTableModel::recalculateNewNames() {
    // 1. Neue Namen berechnen - bereits bereinigt, damit Vorschau, Konfliktprüfung und Ausführung
    //    garantiert denselben Namen sehen (vorher wurde erst beim Umbenennen bereinigt).
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

    // 2. Welche Quellen geben ihren Namen frei, und wie oft wird jedes Ziel beansprucht?
    QSet<QString> movingSources;
    QHash<QString, int> targetUseCount;
    for (int row : std::as_const(m_renameRowsList)) {
        if (row >= 0 && row < static_cast<int>(m_files.size())) {
            const auto &file = m_files[row];
            if (file.newName == file.name) continue; // unverändert -> belegt seinen Namen weiterhin
            movingSources.insert(renameKey(file.filePath));
            targetUseCount[renameKey(QDir(file.path).filePath(file.newName))]++;
        }
    }

    // 3. Konflikte bestimmen
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
            // Ziel existiert auf der Platte (auch herausgefilterte Dateien!) und wird nicht durch eine
            // andere Umbenennung im Stapel frei. Ausnahme: reine Groß-/Kleinschreibungs-Änderung auf einem
            // case-insensitiven Dateisystem (Ziel ist physisch die Datei selbst).
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
    //workingCopy = applyMoveCopyRule(workingCopy, m_rules.mcp);
    workingCopy = applyAddRule(workingCopy, m_rules.add);
    //workingCopy = applyAddDateRule(workingCopy, m_rules.dat);
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

// Führt die Stapel-Umbenennung als "alles oder nichts" aus:
//  - Bei Konflikten (siehe recalculateNewNames) wird gar nicht erst begonnen.
//  - Reihenfolge nach Abhängigkeiten: Wer ein Ziel braucht, das noch von einer anderen Quelle belegt ist,
//    wartet, bis diese umbenannt wurde. Ketten wie 1->2, 2->3 funktionieren so ohne Zwischennamen.
//  - Echte Zyklen (a<->b, a->b->c->a) werden aufgelöst, indem ein Eintrag kurz auf einen temporären Namen ausweicht.
//  - Schlägt eine Umbenennung fehl, werden alle bisherigen in umgekehrter Reihenfolge zurückgedreht.
bool CustomTableModel::applyBatchRename(QString *errorMessage) {
    if (m_files.empty() || m_renameRowsList.isEmpty())
        return true;

    // Mit aktuellem Plattenzustand neu prüfen (seit der Vorschau kann sich etwas geändert haben)
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

    // Abhängigkeitsgraph: pendingSource[key(Quelle)] = Op; waiterFor[key(Quelle)] = Op, dessen Ziel diese Quelle ist
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

    QList<QPair<QString, QString>> journal; // Durchgeführte Umbenennungen (von, nach) für das Zurückdrehen
    QString failedFrom, failedTo, failedReason;
    int remaining = static_cast<int>(ops.size());

    while (remaining > 0) {
        if (ready.isEmpty()) {
            // Zyklus: Einen wartenden Eintrag, auf dessen Quelle jemand wartet, temporär ausweichen lassen
            int c = -1;
            for (int i = 0; i < static_cast<int>(ops.size()); ++i) {
                if (!ops[i].done && waiterFor.contains(renameKey(ops[i].from))) { c = i; break; }
            }
            if (c == -1) { // Darf nicht vorkommen - sicherheitshalber abbrechen statt Endlosschleife
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
        // FEHLER -> alles in umgekehrter Reihenfolge zurückdrehen
        QStringList notReverted;
        for (int k = static_cast<int>(journal.size()) - 1; k >= 0; --k) {
            if (!QFile::rename(journal[k].second, journal[k].first)) {
                notReverted << QDir::toNativeSeparators(journal[k].second);
            }
        }

        if (errorMessage) {
            QString msg;
            if (!failedFrom.isEmpty()) {
                msg = tr("Renaming \"%1\" to \"%2\" failed: %3").arg(QDir::toNativeSeparators(failedFrom), QFileInfo(failedTo).fileName(), failedReason);
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

    // ERFOLG -> Metadaten aller Zeilen aktualisieren, ein einziges dataChanged-Signal
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
    // int rule.toN

    QString workingCopy;
    workingCopy = input;

    if (!workingCopy.isEmpty()) {
        //---------------------------------------------------------------------
        if (rule.firstN > 0) {
            qsizetype count = std::min(static_cast<qsizetype>(rule.firstN), workingCopy.length());
            workingCopy.remove(0, count);
        }

        if (rule.fromN > 0 && rule.toN >= rule.fromN) {
            qsizetype startIndex = rule.fromN - 1;

            if (startIndex < workingCopy.length()) {
                qsizetype count = rule.toN - rule.fromN + 1;
                workingCopy.remove(startIndex, count);
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

        // Anzahl der zu kopierenden/verschiebenden Zeichen auf die Dateinamen-Länge begrenzen
        int count = std::min(rule.fromPos, static_cast<int>(remaining.size()));
        if (count <= 0) {
            return input;
        }

        QString extracted;

        // 1. Sektion extrahieren (und bei "Move" aus 'remaining' entfernen)
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

        // 2. Extrahierten Text an der Zielposition in den Dateinamen einfügen
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

    // Pfad-Separatoren vereinheitlichen und säubern
    QString cleanPath = QDir::cleanPath(QDir::fromNativeSeparators(path));
    QStringList parts = cleanPath.split('/', Qt::SkipEmptyParts);

    if (parts.isEmpty()) {
        return input;
    }

    // Anzahl der gewünschten Ordner-Ebenen ermitteln (begrenzt auf verfügbare Ordner)
    int levelsToTake = std::min(rule.levels, static_cast<int>(parts.size()));
    QStringList selectedFolders = parts.mid(parts.size() - levelsToTake, levelsToTake);

    // Ordnernamen mit dem Trennzeichen zusammenfügen
    QString folderString = selectedFolders.join(rule.separator);
    if (folderString.isEmpty()) {
        return input;
    }

    QString workingCopy;
    workingCopy = input;

    //---------------------------------------------------------------------
    // 1: Prefix (Voranstellen)
    if (rule.mode == 1) {
        if (!workingCopy.isEmpty() && !rule.separator.isEmpty()) {
            workingCopy = folderString + rule.separator + workingCopy;
        } else {
            workingCopy = folderString + workingCopy;
        }
    }
    // 2: Suffix (Anhängen)
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

            // 2. Führende Nullen anwenden / Länge anpassen (falls aktiviert)
            if (rule.addLeadEnabled && rule.addLeadCount > 0) {
                if (numStr.length() < rule.addLeadCount) {
                    numStr = numStr.rightJustified(rule.addLeadCount, QChar('0'));
                } else if (numStr.length() > rule.addLeadCount) {
                    // Führende Nullen entfernen, solange die Solllänge überschritten ist
                    while (numStr.length() > rule.addLeadCount && numStr.startsWith('0')) {
                        numStr.remove(0, 1);
                    }
                }
            }

            // Ersetze die ursprüngliche Ziffernfolge im Dateinamen
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
    types << "text/uri-list"; // Der Standard-MIME-Type für Dateien im OS
    return types;
}

QMimeData* CustomTableModel::mimeData(const QModelIndexList &indexes) const {
    QMimeData *mimeData = new QMimeData();

    QList<QUrl> urls;
    QSet<int> processedRows; // Verhindert Duplikate bei Mehrfachauswahl von Spalten

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
    // Basis-Check: Wenn keine URLs im Drag stecken, direkt verbieten
    if (!data || !data->hasUrls()) {
        return false;
    }

    // Drop "zwischen" Items verbieten!
    // Wenn row != -1 ist, schwebt die Maus an den Rändern eines Items (oberhalb/unterhalb).
    // Durch das 'false' blendet Qt die Einfüge-Linie aus und wechselt sofort in den "OnItem"-Modus.
    if (row != -1) {
        return false;
    }

    // FALL 1: Die Maus schwebt über dem leeren Hintergrund (parent ist ungültig)
    if (!parent.isValid()) {
        return true;
    }

    // FALL 2: Maus schwebt über einem konkreten Item (z. B. einem Ordner)
    int parentRow = parent.row();
    if (parentRow >= 0 && parentRow < static_cast<int>(m_files.size())) {

        // Wir prüfen das Ganze nur, wenn das Ziel-Item ein Ordner ist
        if (m_files[parentRow].isDir) {
            // Das ist der absolute Pfad des Ordners, über dem die Maus gerade schwebt:
            QString targetFolderPath = m_files[parentRow].filePath;

            // Schleife über alle Dateien/Ordner, am Mauszeiger hängen
            QList<QUrl> urls = data->urls();
            for (const QUrl &url : std::as_const(urls)) {
                QString draggedPath = url.toLocalFile();

                // SCHUTZ 1: Ordner auf sich selbst droppen (Identischer Pfad)
                // Verhindert z.B. das Droppen von /Projekte/A auf /Projekte/A in Instanz 2
                if (draggedPath == targetFolderPath) {
                    return false; // Sofort blockieren! Zeigt das Verbots-Schild.
                }

                // SCHUTZ 2: Ordner in einen eigenen Unterordner droppen
                // Verhindert das physikalisch unmögliche Verschieben von /A in /A/B/C
                if (targetFolderPath.startsWith(draggedPath + "/")) {
                    return false; // Ebenfalls blockieren!
                }
            }
        }
    }
    return QAbstractTableModel::canDropMimeData(data, action, row, column, parent);
}

bool CustomTableModel::dropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column, const QModelIndex &parent) {
    Q_UNUSED(row);
    Q_UNUSED(column);
/*
    // --- DEBUG-BLOCK START ---
    if (data) {
        qDebug() << "\n========== QMimeData INSIGHT ==========";
        qDebug() << "Verfügbare Formate:" << data->formats();

        for (const QString &format : data->formats()) {
            QByteArray rawData = data->data(format);
            qDebug() << "---------------------------------------";
            qDebug() << "MIME-Type:" << format;
            qDebug() << "Größe:    " << rawData.size() << "Bytes";

            // Spezialbehandlung für URLs (sehr nützlich bei Browser-Dnd)
            if (format == "text/uri-list" && data->hasUrls()) {
                qDebug() << "Inhalt (als URLs interpretiert):";
                for (const QUrl &url : data->urls()) {
                    qDebug() << "  -> URL:" << url.toString()
                    << "[Lokale Datei?" << (url.isLocalFile() ? "JA" : "NEIN") << "]";
                }
            }
            // Reine Text-Formate lesbar als String ausgeben
            else if (format.startsWith("text/") || format.contains("string") || format.contains("text")) {
                // Konvertiert die Rohdaten von UTF-8 in einen lesbaren QString
                QString textContent = QString::fromUtf8(rawData);
                // Zeilenumbrüche für die Ausgabe etwas einrücken
                textContent.replace("\n", "\n  ");
                qDebug() << "Inhalt (Text):\n  " << textContent;
            }
            // Binäre oder Qt-interne Formate als Hex-Vorschau anzeigen
            else {
                // Wir zeigen nur die ersten 64 Bytes an, damit das Terminal nicht geflutet wird
                QByteArray preview = rawData.left(64);
                qDebug() << "Inhalt (Hex-Vorschau):\n  " << preview.toHex(' ');
                if (rawData.size() > 64) {
                    qDebug() << "  ... (" << (rawData.size() - 64) << "weitere Bytes)";
                }
            }
        }
        qDebug() << "=======================================\n";
    } else {
        qDebug() << "dropMimeData aufgerufen, aber QMimeData ist NULL!";
    }
    // --- DEBUG-BLOCK ENDE ---
*/

    if (action == Qt::IgnoreAction) return true;
    if (!data || !data->hasUrls()) return false;

    // Standardziel: Das aktuell geöffnete Verzeichnis
    QString targetDir;
	if (!m_currentDirectoryPath.isEmpty() && m_currentDirectoryPath != "drives://") {
		targetDir = m_currentDirectoryPath;
	}
	
    // Wenn auf ein gültiges Item gedroppt wurde und das ein Ordner ist,
    // verschieben/kopieren wir die Dateien IN diesen Unterordner!
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

                    // Firefox liefert das unter Windows als UTF-16 (2 Bytes pro Zeichen).
                    // Wir wandeln die Rohdaten explizit von UTF-16 in einen QString um:
                    webTitle = QString::fromUtf16(
                        reinterpret_cast<const char16_t*>(rawDesc.constData()),
                        rawDesc.size() / 2
                        );

                    // Da Windows-Strings oft mit einem Null-Terminator '\0' enden,
                    // schneiden wir diesen und eventuelle Leerzeichen ab.
                    int nullPos = webTitle.indexOf(QChar('\0'));
                    if (nullPos != -1) {
                        webTitle = webTitle.left(nullPos);
                    }
                    webTitle = webTitle.trimmed();
                }

                // Fallback, falls es kein Firefox/Mozilla-Browser war oder das Feld fehlt
                if (webTitle.isEmpty() && data->hasText()) {
                    // Wenn es eine URL ist, extrahieren wir den Hostnamen, sonst nehmen wir den Text
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

        // WICHTIGE QT-BESONDERHEIT:
        // In Qt zeichnet drawRect(x, y, w, h) bei einem 1px-Stift historisch bedingt
        // ein Rechteck, das w+1 Pixel breit und h+1 Pixel hoch ist.
        // Damit der Rand exakt auf den Pixeln 0 bis 95 liegt, müssen wir -1 rechnen.
        painter.drawRect(0, 0, canvas.width() - 1, canvas.height() - 1);

        // Der Painter wird am Ende des Scopes {} automatisch geschlossen und gespeichert
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
        if (!QDir::toNativeSeparators(dirPath).endsWith(QDir::separator())) {
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

            // Filterung nach Dateien und Ordnern
            if (fileInfo.isFile() && !m_rules.flt.showFiles) {
                continue;
            }
            if (fileInfo.isDir() && !m_rules.flt.showFolders) {
                continue;
            }

            // Filterung nach Dateinamen/Suchbegriff
            if (!bSearchStringFilenameEmpty) {
                if (Helpers::getNameMatchQuality(fileInfo.fileName(), fileInfo.path(), searchStringFilename, searchStringFilenameSplit, caseSensitivityFilename) == 0) {
                    continue;
                }
            }

            // iAnchorPathLength ist 0, da kein gemeinsames Quellverzeichnis (dirPath) existiert
            CustomFileInfo info = createCustomFileInfo(fileInfo, 0);

            newFiles.push_back(info);
        }
    }

    recalculateNewNames();

    beginResetModel();
    m_files = std::move(newFiles);
    endResetModel();
}

void CustomTableModel::populateModel_mkFileSearch(const QString &searchDir, const QString &searchStringFilename, const QString &searchStringContent, bool bRegExFilename, bool bRegExContent, bool bFilenameCaseSensitive, bool bContentCaseSensitive, Qt::CheckState cbDirState, const QSet<QString> &FileExtTextSet) {
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
    if (!QDir::toNativeSeparators(searchDir).endsWith(QDir::separator())) {
        iAnchorPathLength++;
    }

    QDir::Filters searchFlags = QDir::Hidden | QDir::NoDotAndDotDot | QDir::System; // QDir::System needed for *.lnk files on Windows

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

    // Timer zur Zeitmessung für periodische Updates (1000 ms)
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
                // Bei Verwerfen vor dem continue prüfen, ob 1000 ms vergangen sind
                if (progressTimer.elapsed() >= 1000) {
                    emit searchProgress(iItemsFound, iNameMatched, iContentMatched);
                    progressTimer.restart();
                }
                continue;
            }
        }

        if (!bsearchStringContentEmpty) {
            if (bRegExContent) {
                contentMatchCount = Helpers::getRegExContentMatchCount(fileInfo, qreContent, FileExtTextSet);
            } else {
                contentMatchCount = Helpers::getContentMatchCount(fileInfo, searchStringContent, caseSensitivityContent, FileExtTextSet);
            }

            if (contentMatchCount == 0) {
                // Bei Verwerfen vor dem continue prüfen, ob 1000 ms vergangen sind
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

        // Prüfen, ob seit dem letzten Signal 1000 ms vergangen sind
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

#if defined(Q_OS_LINUX)
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
        QDirIterator iter(dirPath, {"*.desktop"}, QDir::NoDotAndDotDot | QDir::Files);   // Parameter: Pfad, Namensfilter, Filter-Flags

        while (iter.hasNext()) {
            iter.next();

            if (m_abortSearch.load()) {
                bSearchInterrupted = true;
                break;
            }

            QString fileName = iter.fileName(); // Die ID (z.B. "firefox.desktop")

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
#endif

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

#ifdef Q_OS_WIN
    if (m_currentDirectoryPath == "drives://") {
        QFileInfoList drives = QDir::drives();
        for (const QFileInfo &driveInfo : std::as_const(drives)) {
            CustomFileInfo info;
            info.drivePath = driveInfo.absoluteFilePath();

            QStorageInfo storage(driveInfo.absoluteFilePath());
            QString volumeName = storage.name();

            // Fallback: Falls das Laufwerk keinen Namen hat (oder z.B. ein leeres CD-Laufwerk ist)
            if (volumeName.isEmpty()) {
                if (storage.fileSystemType() == "CDFS" || storage.fileSystemType() == "UDF") {
                    volumeName = tr("Optical Drive");
                } else {
                    volumeName = tr("Local Drive");
                }
            }

            QString driveLetter = driveInfo.absoluteFilePath().left(2);

            info.name = QString("(%1) %2").arg(driveLetter, volumeName);

            info.isDir = true;
            info.isDrive = true;
            info.isHidden = false;
            info.size = storage.bytesTotal();
            info.date = QDateTime();
            info.iconIndex = 0;
            info.displayName = info.name;
            info.type = storage.fileSystemType();
            info.isExecutable = false;
            info.path = driveInfo.absoluteFilePath();
            info.anchorPathLength = 0;

            m_individualIconCache.insert(info.drivePath, {m_iconProvider.icon(driveInfo), QDateTime()});


            newFiles.push_back(info);
        }
    }
    else
#endif
        {
        QDir::Filters filters = QDir::Files | QDir::Dirs | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot;
        QDirIterator it(dirPath, filters, QDirIterator::NoIteratorFlags);
        while (it.hasNext()) {
            it.next();
            CustomFileInfo info = createCustomFileInfo(it.fileInfo());

            newFiles.push_back(info);
        }
    }

    beginResetModel();
    m_files = std::move(newFiles);
    endResetModel();
}

void CustomTableModel::removeFilePaths(const QSet<QString> &paths) {
    if (paths.isEmpty()) return;

    // Wir laufen rückwärts durch unsere internen Daten
    for (int i = m_files.size() - 1; i >= 0; --i) {
        const CustomFileInfo &file = m_files[i];
        QString currentPath = file.filePath;

        if (paths.contains(currentPath)) {
            // Qt mitteilen, dass wir genau diese Zeile gleich löschen
            beginRemoveRows(QModelIndex(), i, i);

            m_files.erase(m_files.begin() + i);

            // Optional: Auch aus dem CRC-Cache werfen, da die Datei weg ist
            m_CrcCache.remove(currentPath);

            endRemoveRows(); // View aktualisiert sich jetzt automatisch sauber
        }
    }
}

CustomFileInfo CustomTableModel::createCustomFileInfo(const QFileInfo &fileInfo, int anchorPathLength) const {
    CustomFileInfo info;
    info.name = fileInfo.fileName();
    info.isDir = fileInfo.isDir();
    info.isSymbolicLink = fileInfo.isSymbolicLink();

#ifdef Q_OS_WIN
    if (fileInfo.isShortcut()) {
        LnkInfo lnkinfo = Helpers::getLnkInfo(fileInfo.filePath());
        if (lnkinfo.exists) {
            info.isHidden = lnkinfo.isHidden;
            info.size = lnkinfo.size;
            info.date = lnkinfo.lastWriteTime;
        }
    }
    else
#endif
    {
        info.isHidden = fileInfo.isHidden();
        info.size = info.isDir ? 0 : fileInfo.size();
        info.date = fileInfo.lastModified();
    }

    info.iconIndex = 0;
    info.displayName = info.name;

    QString ext;
    if (!info.isDir) {
        int lastDot = info.name.lastIndexOf('.');
        if (lastDot > 0) {
            ext = info.name.sliced(lastDot + 1).toLower();

            if (!m_settings->showFileExtensions && m_settings->knownExts.contains(ext)) {
                info.displayName = info.name.sliced(0, lastDot);
            }
        }
    }

    info.type = ext;

#ifdef Q_OS_WIN
    info.isExecutable = (ext == "exe" || ext == "scr" || ext == "msi" || ext == "com" || ext == "bat");
#else
    info.isExecutable = fileInfo.isExecutable();
#endif
    info.path = fileInfo.absolutePath();
    info.filePath = fileInfo.absoluteFilePath();
    info.anchorPathLength = anchorPathLength;
    return info;
}
