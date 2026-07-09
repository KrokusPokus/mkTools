#ifndef FILESORTPROXYMODEL_H
#define FILESORTPROXYMODEL_H

#include <QAbstractTableModel>
#include <QCollator>
#include <QSortFilterProxyModel>

class FileSortProxyModel : public QSortFilterProxyModel {
    Q_OBJECT

public:
    using QSortFilterProxyModel::QSortFilterProxyModel;

    explicit FileSortProxyModel(QObject *parent);
    void setShowHiddenFiles(bool show);
    void setFilterTerms(const QString &filterTerms);

private:
    bool m_showHiddenFiles = false;
    QString m_activeFilterTerms;
    QStringList m_activeFilterTermsSplit;
    QCollator m_collator;

protected:
    bool lessThan(const QModelIndex &source_left, const QModelIndex &source_right) const override;
    bool filterAcceptsRow(int source_row, const QModelIndex &source_parent) const override;

};
#endif // FILESORTPROXYMODEL_H
