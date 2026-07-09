#include "customtablemodel.h"
#include "helpers.h"

#include <QApplication>
#include <QDirIterator>
#include <QMimeData>
#include <QPainter>
#include <QRegularExpression>
#include <QStorageInfo>

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
            case 0: return file.displayName;
            case 1: {
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
            case 2: {
                    if (file.isDir && m_currentDirectoryPath != "drives://") {
                        return QVariant();
                    }
                    return formatAdaptiveSize(file.size);
                }
            case 3: return file.date.toString("yyyy-MM-dd  HH:mm:ss");
            case 4: return file.type;
            case 5: return file.nameMatchQuality;
            case 6: return file.contentMatchCount;
            case 7: return QVariant();  // CRC
            default: return QVariant();
        }
    }
    else if (role == Qt::EditRole) {
        switch (col) {
            case 0: return file.name;
            case 1: return file.path;
            case 2: return file.size;
            case 3: return file.date;
            case 4: return file.type;
            case 5: return file.nameMatchQuality;
            case 6: return file.contentMatchCount;
            case 7: return QVariant();  // CRC
            default:
                return data(index, Qt::DisplayRole);
        }
    }
    else if (role == Qt::DecorationRole) {
        if (col != 0) return QVariant(); // Icons nur in der Namensspalte anzeigen

        QString absolutePath = QDir::cleanPath(file.path + "/" + file.name);
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
        case 0:
        case 1:
            return QVariant(Qt::AlignLeft | Qt::AlignVCenter);
        case 2:
            return QVariant(Qt::AlignRight | Qt::AlignVCenter);
        case 3:
        case 4:
            return QVariant(Qt::AlignLeft | Qt::AlignVCenter);
        case 5:
        case 6:
        case 7:
            return QVariant(Qt::AlignCenter | Qt::AlignVCenter);
        default:
            return QVariant(Qt::AlignLeft | Qt::AlignVCenter);
        }
    }
    else if (role == CustomTableModel::UseRedTextRole) {
        // Prüfen, ob Datei ausführbar ist UND das Setting aktiv ist
        if (file.isExecutable && !file.isDir && m_settings->executableFilesRed) {
            return QVariant(true);
        }
        // Standardfarbe des Systems/Themes beibehalten
        return QVariant(false);
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
        case 0:
            return tr("Name");
        case 1:
            return tr("Path");
        case 2:
            return tr("Size");
        case 3:
            return tr("Changed");
        case 4:
            return tr("Type");
        case 5:
            return tr("Rating");
        case 6:
            return tr("Count");
        case 7:
            return tr("CRC");
        default:
            return QVariant();
        }
    }

    if (orientation == Qt::Horizontal && role == Qt::TextAlignmentRole) {
        switch (section) {
        case 0:
        case 1:
            return QVariant(Qt::AlignLeft | Qt::AlignVCenter);
        case 2:
            return QVariant(Qt::AlignRight | Qt::AlignVCenter);
        case 3:
        case 4:
            return QVariant(Qt::AlignLeft | Qt::AlignVCenter);
        case 5:
        case 6:
        case 7:
            return QVariant(Qt::AlignCenter | Qt::AlignVCenter);
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
    if (!index.isValid() || role != Qt::EditRole || index.column() != 0) {
        return false;
    }

    int row = index.row();
    if (row < 0 || row >= static_cast<int>(m_files.size())) {
        return false;
    }

    QString newName = cleanFileName(value.toString().trimmed());

    // Abbrechen, wenn der Name leer ist oder sich gar nicht geändert hat
    if (newName.isEmpty() || newName == m_files[row].name) {
        return false;
    }

    // --- 1. DATEI AUF DER FESTPLATTE UMBENENNEN ---
    QString oldPath = QDir::cleanPath(m_files[row].path + "/" + m_files[row].name);
    QString newPath = QDir::cleanPath(m_files[row].path + "/" + newName);
    
    // Versuchen, das Filesystem-Objekt umzubenennen
    if (!QFile::rename(oldPath, newPath)) {
        // Wenn das OS streikt (z.B. Datei geöffnet, keine Rechte), brechen wir ab
        return false;
    }


    // 2. Struct im Speicher aktualisieren
    CustomFileInfo &info = m_files[row];

    QString ext;
    info.name = newName;
    info.displayName = newName;

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

    // --- 3. VIEW BENACHRICHTIGEN ---
    emit dataChanged(index, index, {Qt::DisplayRole, Qt::EditRole});

    return true;
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
        return QDir::cleanPath(file.path + "/" + file.name);
    }
}

bool CustomTableModel::isPathIconUpToDate(const QString &path, const QDateTime &currentDate) const {
    auto it = m_individualIconCache.find(path);
    if (it == m_individualIconCache.end()) {
        return false;
    }
    // Wenn das Datum im Cache ungültig ist (Laufwerk-Dummy) oder exakt übereinstimmt -> Aktuell!
    return !it.value().lastModified.isValid() || it.value().lastModified == currentDate;
}

void CustomTableModel::addPathIcon(const QString &path, const QIcon &icon, const QDateTime &lastModified, int row) {
    m_individualIconCache.insert(path, {icon, lastModified});

    // Sofort die View benachrichtigen, dass genau DIESE Zeile (Spalte 0) neu gezeichnet werden muss
    QModelIndex idx = this->index(row, 0);
    emit dataChanged(idx, idx, {Qt::DecorationRole});
}

bool CustomTableModel::isThumbnailUpToDate(const QString &path, const QDateTime &currentDate) const {
    auto it = m_individualThumbnailCache.find(path);
    if (it == m_individualThumbnailCache.end()) {
        return false;
    }
    return !it.value().lastModified.isValid() || it.value().lastModified == currentDate;
}

void CustomTableModel::addPathThumbnail(const QString &path, const QPixmap &thumb, const QDateTime &lastModified, int row) {
    m_individualThumbnailCache.insert(path, {thumb, lastModified});

    int targetRow = row;

    if (targetRow == -1) {
        for (int i = 0; i < m_files.size(); ++i) {
            QString fullFilePath = QDir::cleanPath(m_files[i].path + "/" + m_files[i].name);
            if (fullFilePath == path) {
                targetRow = i;
                break;
            }
        }
    }

    if (targetRow != -1 && targetRow < this->rowCount()) {
        QModelIndex idx = this->index(targetRow, 0);
        emit dataChanged(idx, idx, {Qt::DecorationRole});
    }
}

void CustomTableModel::setCutMarkers(const QStringList &absolutePaths) {
    // Wir wandeln die Liste zur performanten Suche in ein QSet um
    QSet<QString> cutSet(absolutePaths.begin(), absolutePaths.end());

    for (size_t i = 0; i < m_files.size(); ++i) {
        QString fullPath = QDir::cleanPath(m_files[i].path + "/" + m_files[i].name);
        bool newCutState = cutSet.contains(fullPath);

        if (m_files[i].isCut != newCutState) {
            m_files[i].isCut = newCutState;

            QModelIndex startIdx = this->index(static_cast<int>(i), 0);
            QModelIndex endIdx = this->index(static_cast<int>(i), this->columnCount() - 1);
            emit dataChanged(startIdx, endIdx, {CustomTableModel::IsCutRole, Qt::DecorationRole});
        }
    }
}

void CustomTableModel::clearCutMarkers() {
    for (size_t i = 0; i < m_files.size(); ++i) {
        if (m_files[i].isCut) {
            m_files[i].isCut = false;

            QModelIndex startIdx = this->index(static_cast<int>(i), 0);
            QModelIndex endIdx = this->index(static_cast<int>(i), this->columnCount() - 1);
            emit dataChanged(startIdx, endIdx, {CustomTableModel::IsCutRole, Qt::DecorationRole});
        }
    }
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
            QString absolutePath = QDir::cleanPath(m_files[row].path + "/" + m_files[row].name);
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
            QString targetFolderPath = QDir::cleanPath(m_files[parentRow].path + "/" + m_files[parentRow].name);

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
            targetDir = QDir::cleanPath(m_files[parentRow].path + "/" + m_files[parentRow].name);
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

                createInternetShortcut(webUrlStr, targetDir, webTitle);
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

void CustomTableModel::populateModel_mkFileSearch(const QString &searchDir, const QString &searchStringFilename, const QString &searchStringContent, bool bRegExFilename, bool bRegExContent, bool bFilenameCaseSensitive, bool bContentCaseSensitive, Qt::CheckState cbDirState, const QSet<QString> &FileExtTextSet) {
    m_abortSearch.store(false);
    std::vector<CustomFileInfo> newFiles;
    m_currentDirectoryPath = searchDir;

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

    QDirIterator it(searchDir, searchFlags, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();

        QCoreApplication::processEvents();

        if (m_abortSearch.load()) {
            bSearchInterrupted = true;
            break;
        }

        iItemsFound++;

        if (!bSearchStringFilenameEmpty) {
            if (bRegExFilename) {
                nameMatchQuality = getRegExNameMatchQuality(it.fileInfo(), qreFileName);
            } else {
                nameMatchQuality = getNameMatchQuality(it.fileInfo(), searchStringFilename, searchStringFilenameSplit, caseSensitivityFilename);
            }

            if (nameMatchQuality == 0) {
                continue;
            }
        }


        if (!bsearchStringContentEmpty) {
            if (bRegExContent) {
                contentMatchCount = getRegExContentMatchCount(it.fileInfo(), qreContent, FileExtTextSet);
            } else {
                contentMatchCount = getContentMatchCount(it.fileInfo(), searchStringContent, caseSensitivityContent, FileExtTextSet);
            }

            if (contentMatchCount == 0) {
                continue;
            }

            iContentMatched += contentMatchCount;
        }

        iNameMatched++;

        QFileInfo fileInfo = it.fileInfo();

        CustomFileInfo info;
        info.name = fileInfo.fileName();
        info.isDir = fileInfo.isDir();
#ifdef Q_OS_WIN
        info.isHidden = fileInfo.isHidden() || info.name.startsWith('.');
#else
        info.isHidden = fileInfo.isHidden();
#endif
        info.size = info.isDir ? 0 : fileInfo.size();
        info.date = fileInfo.lastModified();
        info.iconIndex = 0;

        QString ext;
        info.displayName = info.name;

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
        info.anchorPathLength = iAnchorPathLength;
        info.nameMatchQuality = nameMatchQuality;
        info.contentMatchCount = contentMatchCount;

        newFiles.push_back(info);
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

                        nameMatchQuality = getDesktopNameMatchQuality(filePath, searchString, searchStringSplit, recentOpenList, alternativeName);
                        if (nameMatchQuality == 0) {
                            continue;
                        }

                        iNameMatched++;

                        QFileInfo fileInfo = iter.fileInfo();

                        CustomFileInfo info;
                        info.name = fileInfo.fileName();
                        info.isDir = fileInfo.isDir();
                        info.isHidden = fileInfo.isHidden();
                        info.size = info.isDir ? 0 : fileInfo.size();
                        info.date = fileInfo.lastModified();
                        info.iconIndex = 0;

                        QString ext;
                        info.displayName = info.name;

                        if (!info.isDir) {
                            int lastDot = info.name.lastIndexOf('.');
                            if (lastDot > 0) {
                                ext = info.name.sliced(lastDot + 1).toLower();

                                if (!m_settings->showFileExtensions && m_settings->knownExts.contains(ext)) {
                                    info.displayName = info.name.sliced(0, lastDot);
                                }
                            }
                        }

                        QString displayName = iniNameLocalised.isEmpty() ? iniName : iniNameLocalised;
                        if (iSystemSettings == 1) {
                            info.displayName = tr("System Settings:") + ' ' + displayName;
                        } else {
                            // If this is *not* systemsettings or is *exactly* systemsettings, don't prepend the term!
                            info.displayName = displayName;
                        }

                        info.type = ext;

                        info.isExecutable = fileInfo.isExecutable();

                        info.path = fileInfo.absolutePath();
                        info.anchorPathLength = 0;
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

                nameMatchQuality = getLauncherNameMatchQuality(iter.fileInfo(), searchString, searchStringSplit, recentOpenList);
                if (nameMatchQuality == 0) {
                    continue;
                }

                iNameMatched++;

                QFileInfo fileInfo = iter.fileInfo();

                CustomFileInfo info;
                info.name = fileInfo.fileName();
                info.isDir = fileInfo.isDir();
#ifdef Q_OS_WIN
                info.isHidden = fileInfo.isHidden() || info.name.startsWith('.');
#else
                info.isHidden = fileInfo.isHidden();
#endif
                info.size = info.isDir ? 0 : fileInfo.size();
                info.date = fileInfo.lastModified();
                info.iconIndex = 0;

                QString ext;
                info.displayName = info.name;

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
                info.anchorPathLength = 0;
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
    
            QFileInfo fileInfo = it.fileInfo();
    
            CustomFileInfo info;
            info.name = fileInfo.fileName();
            info.isDir = fileInfo.isDir();
#ifdef Q_OS_WIN
            info.isHidden = fileInfo.isHidden() || info.name.startsWith('.');
#else
            info.isHidden = fileInfo.isHidden();
#endif
            info.size = info.isDir ? 0 : fileInfo.size();
            info.date = fileInfo.lastModified();
            info.iconIndex = 0;
    
            QString ext;
            info.displayName = info.name;
    
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
            info.anchorPathLength = 0;

            newFiles.push_back(info);
        }
    }

    beginResetModel();
    m_files = std::move(newFiles);
    endResetModel();
}
