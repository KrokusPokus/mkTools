#include "customlistview.h"
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

CustomListView::CustomListView(QWidget *parent)
    : QListView(parent)
{
    // Hier könntest du auch Standard-Einstellungen für deine View setzen,
    // z.B. setAcceptDrops(true);
}

void CustomListView::startDrag(Qt::DropActions supportedActions) {
    // 1. Prüfen, ob die rechte Maustaste gedrückt ist, während der Drag startet
    bool isRightClick = (QGuiApplication::mouseButtons() & Qt::RightButton);

    QModelIndexList indexes = selectedIndexes();
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
    QPixmap pm;

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
        if (m_thumbnailMode) {
            pm = model()->data(indexes.first(), Qt::DecorationRole).value<QPixmap>();
        } else {
            dragIcon = model()->data(indexes.first(), Qt::DecorationRole).value<QIcon>();
        }
    }

    if (!dragIcon.isNull()) {
        pm = dragIcon.pixmap(32, 32);
    }

    if (!pm.isNull()) {
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

void CustomListView::dragEnterEvent(QDragEnterEvent *event) {
    m_targetDirPrevious.clear();
    m_isSameFolderCached = false;
    m_isSameDriveCached = false;

    QAbstractItemView::dragEnterEvent(event);
    updateTargetCache(event->position().toPoint(), event->mimeData());
    event->setDropAction(resolveDropAction(event->modifiers()));
    event->accept();
}

void CustomListView::dragMoveEvent(QDragMoveEvent *event) {

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

void CustomListView::dropEvent(QDropEvent *event) {

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

bool CustomListView::edit(const QModelIndex &index, EditTrigger trigger, QEvent *event) {
    // Part of mitigation against going into rename mode from clicking a selected item while more than one is selected
    if (trigger == QAbstractItemView::SelectedClicked && m_hadMultiSelectionBeforeClick) {
        return false;
    }

    return QListView::edit(index, trigger, event);
}

void CustomListView::mousePressEvent(QMouseEvent *event) {
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

    // Part of mitigation for Shift+Up/Shift+Down range selection bug
    QModelIndex clicked = indexAt(event->pos());
    if (clicked.isValid() && !(event->modifiers() & Qt::ShiftModifier)) {
        m_selectionAnchor = clicked;
    }

    m_isMouseClickSelection = true;     // Flag aktivieren: Wir befinden uns in einem echten Mausklick
    QListView::mousePressEvent(event);
    m_isMouseClickSelection = false;    // Direkt danach wieder zurücksetzen
}

// Part of mitigation for Shift+Up/Shift+Down range selection bug
void CustomListView::keyPressEvent(QKeyEvent *event) {
    bool shiftPressed = event->modifiers() & Qt::ShiftModifier;

    m_isKeyboardSelection = true;       // Flag aktivieren: Wir befinden uns in einem Tastatur-Event
    QListView::keyPressEvent(event);    // Qt die normale Bewegung machen lassen (ändert den currentIndex)
    m_isKeyboardSelection = false;      // Direkt danach wieder zurücksetzen

    // Wenn KEIN Shift gedrückt war, wandert der Selektions-Anker stur mit
    if (!shiftPressed) {
        m_selectionAnchor = currentIndex();
    }
}

// Part of mitigation for Shift+Up/Shift+Down range selection bug
void CustomListView::setSelection(const QRect &rect, QItemSelectionModel::SelectionFlags command) {
    bool isShift = QGuiApplication::keyboardModifiers() & Qt::ShiftModifier;
    QModelIndex current = currentIndex();

    // Wir erlauben unsere lineare Selektion NUR, wenn das Event nachweislich
    // von einem direkten Tastendruck oder einem stationären Mausklick stammt.
    bool safeToLinearize = m_isKeyboardSelection || m_isMouseClickSelection;

    if (!isShift || !safeToLinearize || !current.isValid() || !m_selectionAnchor.isValid()) {
        // Falls der User wirklich zieht (z.B. Gummiband-Auswahl im leeren Raum),
        // greift das Standard-Verhalten von Qt.
        QListView::setSelection(rect, command);
        return;
    }

    int startRow = qMin(m_selectionAnchor.row(), current.row());
    int endRow = qMax(m_selectionAnchor.row(), current.row());

    QItemSelection linearSelection;
    for (int r = startRow; r <= endRow; ++r) {
        QModelIndex idx = model()->index(r, 0, rootIndex());
        if (idx.isValid()) {
            linearSelection.append(QItemSelectionRange(idx));
        }
    }

    // Wir füttern das Selektionsmodell direkt mit unserer mathematisch exakten Range
    if (selectionModel()) {
        selectionModel()->select(linearSelection, command);
    }
}

// Part of mitigation for Shift+Up/Shift+Down range selection bug
void CustomListView::currentChanged(const QModelIndex &current, const QModelIndex &previous) {
    // Erst die Basisklasse ihre Arbeit machen lassen
    QListView::currentChanged(current, previous);

    // Wenn die Änderung von außen (programmatisch) kommt,
    // ist weder das Tastatur- noch das Maus-Flag aktiv.
    if (!m_isKeyboardSelection && !m_isMouseClickSelection) {
        // Den Anker sauber auf den neuen aktuellen Index setzen
        m_selectionAnchor = current;
    }
}

bool CustomListView::interceptInvalidFileTarget(QDragMoveEvent *event, bool isEnterEvent) {
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

void CustomListView::updateTargetCache(const QPoint &pos, const QMimeData *mimeData) {
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

Qt::DropAction CustomListView::resolveDropAction(Qt::KeyboardModifiers mods) const {
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
