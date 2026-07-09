#include "filesortproxymodel.h"
#include "customtablemodel.h"

#include <QDateTime>

FileSortProxyModel::FileSortProxyModel(QObject *parent)
    : QSortFilterProxyModel(parent)
{
    m_collator.setNumericMode(true);                    // Sorgt dafür, dass "9" < "10" ist
    m_collator.setCaseSensitivity(Qt::CaseInsensitive); // Groß-/Kleinschreibung ignorieren
}

bool FileSortProxyModel::lessThan(const QModelIndex &source_left, const QModelIndex &source_right) const {
    auto *model = qobject_cast<CustomTableModel*>(sourceModel());
    if (!model) return QSortFilterProxyModel::lessThan(source_left, source_right);

    bool leftIsDir = model->isDirectory(source_left.row());
    bool rightIsDir = model->isDirectory(source_right.row());

    if (leftIsDir != rightIsDir) {
        return (sortOrder() == Qt::AscendingOrder) ? leftIsDir : rightIsDir;
    }

    // Wenn beide identisch, dann nach Spalte sortieren
    QVariant leftData = sourceModel()->data(source_left, Qt::EditRole);
    QVariant rightData = sourceModel()->data(source_right, Qt::EditRole);

    switch (source_left.column()) {
    case 0: // Name
    case 1: // Pfad
        return m_collator.compare(leftData.toString(), rightData.toString()) < 0;
    case 2: // Größe
        return leftData.toLongLong() < rightData.toLongLong();
    case 3: // Datum
        return leftData.toDateTime() < rightData.toDateTime();
    case 4: // Typ
        return m_collator.compare(leftData.toString(), rightData.toString()) < 0;
    case 5: // Rating (Match-Qualität)
    case 6: // Count (Anzahl der Treffer)
        return leftData.toInt() < rightData.toInt();
    case 7: // CRC
        return m_collator.compare(leftData.toString(), rightData.toString()) < 0;
    default:
        return QSortFilterProxyModel::lessThan(source_left, source_right);
    }
}

bool FileSortProxyModel::filterAcceptsRow(int source_row, const QModelIndex &source_parent) const {

    QModelIndex index = sourceModel()->index(source_row, 0, source_parent);

    if (!m_showHiddenFiles && (sourceModel()->data(index, CustomTableModel::IsHiddenRole).toBool() == true)) {
        return false;
    }

    if (!m_activeFilterTerms.isEmpty()) {
        QString fileName = sourceModel()->data(index, Qt::EditRole).toString();

        for (const QString &word : std::as_const(m_activeFilterTermsSplit)) {
            if (fileName.indexOf(word, 0, Qt::CaseInsensitive) == -1) { // if word is not part of file name, don't show this item
                return false;
            }
        }
    }

    return true;
}

void FileSortProxyModel::setShowHiddenFiles(bool show) {
    if (m_showHiddenFiles != show) {
        beginFilterChange();
        m_showHiddenFiles = show;
        endFilterChange(); // trigger recalc
    }
}

void FileSortProxyModel::setFilterTerms(const QString &filterTerms) {
    if (filterTerms != m_activeFilterTerms) {
        beginFilterChange();
        m_activeFilterTerms = filterTerms;
        m_activeFilterTermsSplit = filterTerms.split(' ', Qt::SkipEmptyParts);
        endFilterChange(); // trigger recalc
    }
}
