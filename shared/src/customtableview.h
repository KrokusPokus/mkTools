#ifndef CUSTOMTABLEVIEW_H
#define CUSTOMTABLEVIEW_H

#include <QDragMoveEvent>
#include <QDropEvent>
#include <QTableView>

class QRubberBand;

class CustomTableView : public QTableView {
    Q_OBJECT

signals:
    void filesDropped(const QList<QUrl> &urls, const QString &targetDirectory, Qt::DropAction action);

public:
    explicit CustomTableView(QWidget *parent = nullptr);

private:
    void updateTargetCache(const QPoint &pos, const QMimeData *mimeData);
    Qt::DropAction resolveDropAction(Qt::KeyboardModifiers mods) const;
    bool interceptInvalidFileTarget(QDragMoveEvent *event, bool isEnterEvent);

    bool m_hadMultiSelectionBeforeClick = false;

    QString m_targetDirPrevious;
    bool m_isSameFolderCached = false;
    bool m_isSameDriveCached = false;

    QRubberBand *m_rubberBand = nullptr;    // for RubberBand
    QPoint m_origin;                        // for RubberBand
    QItemSelection m_baseSelection;         // for RubberBand

protected:
    void startDrag(Qt::DropActions supportedActions) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    bool edit(const QModelIndex &index, EditTrigger trigger, QEvent *event) override;

    void mouseMoveEvent(QMouseEvent *event) override;       // for RubberBand
    void mouseReleaseEvent(QMouseEvent *event) override;    // for RubberBand
};

#endif // CUSTOMTABLEVIEW_H
