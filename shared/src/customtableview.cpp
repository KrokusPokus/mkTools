#include "customtableview.h"
#include "customtablemodel.h"
#include "helpers.h"

#include <QAbstractProxyModel>
#include <QDrag>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QTimer>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <shellapi.h>   // needed for ExtractIconW() and DestroyIcon()
#endif

CustomTableView::CustomTableView(QWidget *parent)
    : QTableView(parent)
{
    // Hier könntest du auch Standard-Einstellungen für deine View setzen,
    // z.B. setAcceptDrops(true);
}

void CustomTableView::startDrag(Qt::DropActions supportedActions) {
    // 1. Prüfen, ob die rechte Maustaste gedrückt ist, während der Drag startet
    bool isRightClick = (QGuiApplication::mouseButtons() & Qt::RightButton);

    QModelIndexList allIndexes = selectedIndexes();
    if (allIndexes.isEmpty()) return;

    QModelIndexList indexes;
    for (const QModelIndex &idx : std::as_const(allIndexes)) {
        if (idx.column() == 0) {
            indexes.append(idx);
        }
    }
    if (indexes.isEmpty()) return;

    // 2. Standard-MimeData vom Model für die ausgewählten Elemente generieren lassen
    QMimeData *mimeData = model()->mimeData(indexes);
    if (!mimeData) return;

    // 3. Wenn es ein Rechtsklick-Drag ist, flaggen wir das Objekt temporär
    if (isRightClick) {
        mimeData->setData("application/x-rightclickdrag", QByteArray("1"));
    }

    QDrag *drag = new QDrag(this);
    drag->setMimeData(mimeData);

    // --- Drag image generation ---

    QIcon dragIcon;
    if (indexes.count() > 1) {
#if defined(Q_OS_WIN)
        HICON hIcon = ExtractIconW(GetModuleHandle(nullptr), L"shell32.dll", -133);
        if (hIcon) {
            QImage img = QImage::fromHICON(hIcon);
            dragIcon = QIcon(QPixmap::fromImage(img));
            DestroyIcon(hIcon);
        }
#else
        dragIcon = QIcon::fromTheme("document-multiple");
#endif
    } else {
        dragIcon = model()->data(indexes.first(), Qt::DecorationRole).value<QIcon>();
    }

    if (!dragIcon.isNull()) {
        QPixmap pm = dragIcon.pixmap(32, 32);

        // --- NEU: 50% Transparenz anwenden ---
        // 1. Eine neue, leere Pixmap mit transparentem Hintergrund erstellen
        QPixmap transparentPm(pm.size());
        transparentPm.fill(Qt::transparent);

        // 2. QPainter auf der neuen Pixmap starten
        QPainter painter(&transparentPm);
        painter.setRenderHint(QPainter::Antialiasing);

        // 3. Deckkraft auf 50% (0.5) setzen und das Original hineinzeichnen
        painter.setOpacity(0.5);
        painter.drawPixmap(0, 0, pm);
        painter.end(); // Maler schließen, um die Pixmap freizugeben

        // 4. Die transparente Pixmap an den Drag übergeben
        drag->setPixmap(transparentPm);
        drag->setHotSpot(QPoint(transparentPm.width() / 2, transparentPm.height() / 2));
    }

    drag->exec(supportedActions | Qt::CopyAction | Qt::MoveAction | Qt::LinkAction);
}

// This should mirror the logic in dragMoveEvent()
void CustomTableView::dragEnterEvent(QDragEnterEvent *event) {
    m_targetDirPrevious.clear();
    m_isSameFolderCached = false;
    m_isSameDriveCached = false;

    QAbstractItemView::dragEnterEvent(event);
    updateTargetCache(event->position().toPoint(), event->mimeData());
    event->setDropAction(resolveDropAction(event->modifiers()));
    event->accept();
}

// This should mirror the logic in dragEnterEvent()
void CustomTableView::dragMoveEvent(QDragMoveEvent *event) {

    // Mitigation for Qt bug: send event with fake coordinates to clear the hover indicator
    if (interceptInvalidFileTarget(event, false)) {
        return;
    }

    QAbstractItemView::dragMoveEvent(event);

    if (!event->isAccepted()) return;

    updateTargetCache(event->position().toPoint(), event->mimeData());
    event->setDropAction(resolveDropAction(event->modifiers()));
    event->accept();
}

void CustomTableView::dropEvent(QDropEvent *event) {

    updateTargetCache(event->position().toPoint(), event->mimeData());

    if (event->mimeData()->hasFormat("application/x-rightclickdrag")) {
        // 1. DATEN DIREKT SICHERN (bevor das Event zerstört wird)
        QList<QUrl> urls = event->mimeData()->urls();
        QPoint pos = event->position().toPoint();
        QModelIndex targetIndex = indexAt(pos);

        // 2. NATIVEN LOOP SOFORT BEENDEN (Löst den Linux-Mauszeiger/Grab auf)
        event->setDropAction(Qt::CopyAction);
        event->accept();

        // 3. MENÜ ASYNCHRON ÖFFNEN
        QTimer::singleShot(0, this, [this, urls, pos, targetIndex]() {

            QAbstractItemModel *currentModel = model();
            QModelIndex sourceIndex = targetIndex;

            while (auto proxy = qobject_cast<QAbstractProxyModel*>(currentModel)) {
                currentModel = proxy->sourceModel();
                sourceIndex = proxy->mapToSource(sourceIndex);
            }
            auto *customModel = qobject_cast<CustomTableModel*>(currentModel);
            if (!customModel) return;

            QString targetDir = customModel->currentDirectoryPath();

            if (sourceIndex.isValid()) {
                int parentRow = sourceIndex.row();
                if (parentRow >= 0 && parentRow < customModel->rowCount()) {
                    if (customModel->isDirectory(sourceIndex.row())) {
                        targetDir = customModel->filePath(sourceIndex);
                    }
                }
            }

            QMenu menu(this);
            QAction *actCopy = new QAction(tr("Copy here"), &menu);
            QAction *actMove = new QAction(tr("Move here"), &menu);
            QAction *actLink = new QAction(tr("Link here"), &menu);
            QAction *actCancel = new QAction(tr("Cancel"), &menu);

            menu.addAction(actCopy);
            if (!m_isSameFolderCached) {
                menu.addAction(actMove);
            }
            menu.addAction(actLink);
            menu.addSeparator();
            menu.addAction(actCancel);

            if (m_isSameFolderCached || !m_isSameDriveCached) {
                menu.setDefaultAction(actCopy);
            } else {
                menu.setDefaultAction(actMove);
            }

            QAction *selectedAction = menu.exec(mapToGlobal(pos));

            Qt::DropAction chosenAction = Qt::IgnoreAction;
            if (selectedAction == actCopy)  chosenAction = Qt::CopyAction;
            else if (selectedAction == actMove)  chosenAction = Qt::MoveAction;
            else if (selectedAction == actLink)  chosenAction = Qt::LinkAction;
            else return;

            emit filesDropped(urls, targetDir, chosenAction);
        });

        return;
    } else {
        event->setDropAction(resolveDropAction(event->modifiers()));
        QAbstractItemView::dropEvent(event);
    }
}

bool CustomTableView::edit(const QModelIndex &index, EditTrigger trigger, QEvent *event) {
    // Part of mitigation against going into rename mode from clicking a selected item while more than one is selected
    if (trigger == QAbstractItemView::SelectedClicked && m_hadMultiSelectionBeforeClick) {
        return false;
    }

    return QTableView::edit(index, trigger, event);
}

void CustomTableView::mousePressEvent(QMouseEvent *event) {
    // Part of mitigation against going into rename mode from clicking a selected item while more than one is selected
    if (selectionModel()) {
        QModelIndexList selectedIdxList = selectionModel()->selectedIndexes();
        QSet<int> uniqueRows;
        for (const QModelIndex &idx : std::as_const(selectedIdxList)) {
            uniqueRows.insert(idx.row());
        }
        m_hadMultiSelectionBeforeClick = (uniqueRows.size() > 1);
    } else {
        m_hadMultiSelectionBeforeClick = false;
    }

    // for RubberBand (tableView only)
    if (event->button() != Qt::LeftButton) {
        if (m_rubberBand && m_rubberBand->isVisible()) {
            m_rubberBand->hide();
            // Die Selektion auf den Stand VOR dem Gummiband zurücksetzen
            if (selectionModel()) {
                selectionModel()->select(m_baseSelection, QItemSelectionModel::ClearAndSelect);
            }
        }
    }

    if (event->button() == Qt::LeftButton) {
        QPoint pos = event->pos();

        // When clicking column 0 onto real element: default behaviour
        if (columnAt(pos.x()) == 0 && indexAt(pos).isValid()) {
            QTableView::mousePressEvent(event);
            return;
        }

        m_origin = pos;

        // 1. Selektion VOR dem Drag merken (wichtig für Strg-Erweiterung & Verkleinern des Rahmens)
        if (event->modifiers() & Qt::ControlModifier) {
            m_baseSelection = selectionModel()->selection();
        } else {
            m_baseSelection.clear();
            selectionModel()->clearSelection(); // Normaler Klick/Drag löscht alte Selektion
        }

        // 2. Gummiband initialisieren und anzeigen
        if (!m_rubberBand) {
            m_rubberBand = new QRubberBand(QRubberBand::Rectangle, viewport());
        }
        m_rubberBand->setGeometry(QRect(m_origin, QSize()));
        m_rubberBand->show();

        // Falls im leeren Raum geklickt wurde, fangen wir das Event hier ab
        if (!indexAt(m_origin).isValid()) {
            event->accept();
            return;
        }
    }

    QTableView::mousePressEvent(event);
}

// for RubberBand (tableView only)
void CustomTableView::mouseMoveEvent(QMouseEvent *event) {
    // Nur aktiv werden, wenn das Rubberband im Press-Event auch wirklich gestartet wurde
    if (m_rubberBand && m_rubberBand->isVisible() && (event->buttons() & Qt::LeftButton)) {
        QRect rect = QRect(m_origin, event->pos()).normalized();
        m_rubberBand->setGeometry(rect);

        if (!model()) return;

        // Vertikale Zeilengrenzen bestimmen
        int topRow = rowAt(rect.top());
        int bottomRow = rowAt(rect.bottom());

        // Schutz vor Out-of-Bounds beim vertikalen Verlassen der Tabelle
        if (topRow == -1) {
            if (rect.top() < rowViewportPosition(0)) topRow = 0;
            else {
                selectionModel()->select(m_baseSelection, QItemSelectionModel::ClearAndSelect);
                return;
            }
        }
        if (bottomRow == -1) {
            bottomRow = model()->rowCount() - 1;
        }

        topRow = qBound(0, topRow, model()->rowCount() - 1);
        bottomRow = qBound(0, bottomRow, model()->rowCount() - 1);

        // Wir ignorieren die linke/rechte Grenze des Rubberbands und spannen die
        // Selektion stattdessen IMMER von Spalte 0 bis zur letzten Spalte auf.
        int lastCol = model()->columnCount() - 1;
        QModelIndex tl = model()->index(topRow, 0);
        QModelIndex br = model()->index(bottomRow, lastCol);
        QItemSelection currentSelection(tl, br);

        // Alten Zustand wiederherstellen (für das "Schrumpfen" des Rahmens)
        selectionModel()->select(m_baseSelection, QItemSelectionModel::ClearAndSelect);

        // Auswahl mit dem Rows-Flag anwenden, um Qt mitzuteilen, dass es Zeilen sind
        QItemSelectionModel::SelectionFlags flags = QItemSelectionModel::Select | QItemSelectionModel::Rows;
        if (event->modifiers() & Qt::ControlModifier) {
            flags = QItemSelectionModel::Toggle | QItemSelectionModel::Rows;
        }
        selectionModel()->select(currentSelection, flags);

        return; // Basisklasse überspringen, da wir die Selektion selbst steuern
    }

    // Wenn kein Rubberband aktiv ist, greift das Standardverhalten
    // (wichtig, damit Qt bei Bewegung in Spalte 0 den Drag-Vorgang einleitet!)
    QTableView::mouseMoveEvent(event);
}

// for RubberBand (tableView only)
void CustomTableView::mouseReleaseEvent(QMouseEvent *event) {
    if (m_rubberBand) {
        m_rubberBand->hide();
    }

    QTableView::mouseReleaseEvent(event);
}

bool CustomTableView::interceptInvalidFileTarget(QDragMoveEvent *event, bool isEnterEvent) {
    // 1. Index unter der echten Mausposition ermitteln und entpacken
    QModelIndex index = indexAt(event->position().toPoint());
    QAbstractItemModel *currentModel = model();
    while (auto proxy = qobject_cast<QAbstractProxyModel*>(currentModel)) {
        index = proxy->mapToSource(index);
        currentModel = proxy->sourceModel();
    }
    auto *customModel = qobject_cast<CustomTableModel*>(currentModel);

    // 2. Prüfen, ob wir auf einer Datei (ungültiges Ziel) stehen
    if (customModel && index.isValid()) {
        QString itemPath = customModel->filePath(index);
        if (!itemPath.isEmpty() && !QFileInfo(itemPath).isDir()) {

            // 3. Trick anwenden: Fake-Event mit (-1, -1) an die korrekte Basis-Funktion senden
            if (isEnterEvent) {
                QDragEnterEvent fakeEvent(QPoint(-1, -1), event->possibleActions(), event->mimeData(), event->buttons(), event->modifiers());
                QAbstractItemView::dragEnterEvent(&fakeEvent);
            } else {
                QDragMoveEvent fakeEvent(QPoint(-1, -1), event->possibleActions(), event->mimeData(), event->buttons(), event->modifiers());
                QAbstractItemView::dragMoveEvent(&fakeEvent);
            }

            // Dem Betriebssystem signalisieren: Drop hier verboten!
            event->ignore();
            return true; // Event erfolgreich abgefangen
        }
    }

    return false; // Kein Datei-Target, normaler Ablauf erforderlich
}

void CustomTableView::updateTargetCache(const QPoint &pos, const QMimeData *mimeData) {
    // 1. Visuellen Index holen und durch Proxy-Kette entpacken
    QModelIndex sourceIndex = indexAt(pos);
    QAbstractItemModel *currentModel = model();
    while (auto proxy = qobject_cast<QAbstractProxyModel*>(currentModel)) {
        sourceIndex = proxy->mapToSource(sourceIndex);
        currentModel = proxy->sourceModel();
    }

    auto *tableModel = qobject_cast<CustomTableModel*>(currentModel);
    if (!tableModel) return;

    // 2. Zielverzeichnis ermitteln
    QString currentTargetDir = tableModel->currentDirectoryPath();
    if (sourceIndex.isValid() && dropIndicatorPosition() == QAbstractItemView::OnItem) {
        QString itemPath = tableModel->filePath(sourceIndex);
        if (!itemPath.isEmpty() && QFileInfo(itemPath).isDir()) {
            currentTargetDir = itemPath;
        }
    }

    // 3. Wenn sich der Ordner geändert hat: Cache neu berechnen
    if (currentTargetDir != m_targetDirPrevious) {
        m_targetDirPrevious = currentTargetDir;
        m_isSameFolderCached = false;
        m_isSameDriveCached = false;

        if (mimeData && mimeData->hasUrls()) {
            QList<QUrl> urls = mimeData->urls();
            if (!urls.isEmpty()) {
                QFileInfo firstFile(urls.first().toLocalFile());

                if (firstFile.absolutePath() == currentTargetDir) {
                    m_isSameFolderCached = true;
                }
                m_isSameDriveCached = onSameStorageDevice(firstFile.absolutePath(), currentTargetDir);
            }
        }
    }
}

Qt::DropAction CustomTableView::resolveDropAction(Qt::KeyboardModifiers mods) const {
    if (m_isSameFolderCached || !m_isSameDriveCached) {
        return Qt::CopyAction;
    }
    if (((mods & Qt::ControlModifier) && (mods & Qt::ShiftModifier)) || (mods & Qt::AltModifier)) {
        return Qt::LinkAction;
    }
    if (mods & Qt::ControlModifier) {
        return Qt::CopyAction;
    }
    return Qt::MoveAction;
}
