#include "customlistview.h"
#include "customtablemodel.h"
#include "helpers.h"

#include <QAbstractProxyModel>
#include <QCursor>
#include <QDrag>
#include <QGuiApplication>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QTimer>

CustomListView::CustomListView(QWidget *parent)
    : QListView(parent)
{
    // You could also set default settings for your view here,
    // e.g. setAcceptDrops(true);
}

void CustomListView::startDrag(Qt::DropActions supportedActions) {
    // 1. Check whether the right mouse button is pressed while the drag starts
    bool isRightClick = (QGuiApplication::mouseButtons() & Qt::RightButton);

    QModelIndexList indexes = selectedIndexes();
    if (indexes.isEmpty()) return;

    // 2. Let the model generate the default MimeData for the selected items
    QMimeData *mimeData = model()->mimeData(indexes);
    if (!mimeData) return;

    // 3. If it is a right-click drag, we flag the object temporarily
    if (isRightClick) {
        mimeData->setData("application/x-rightclickdrag", QByteArray("1"));
    }

    QDrag *drag = new QDrag(this);
    drag->setMimeData(mimeData);

    // --- Drag image generation ---

    QIcon dragIcon;
    QPixmap pm;

    if (indexes.count() > 1) {
        dragIcon = QIcon::fromTheme("document-multiple");
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
        // --- NEW: Apply 50% transparency ---
        // 1. Create a new, empty pixmap with a transparent background
        QPixmap transparentPm(pm.size());
        transparentPm.fill(Qt::transparent);

        // 2. Start a QPainter on the new pixmap
        QPainter painter(&transparentPm);
        painter.setRenderHint(QPainter::Antialiasing);

        // 3. Set the opacity to 50% (0.5) and draw the original into it
        painter.setOpacity(0.5);
        painter.drawPixmap(0, 0, pm);
        painter.end(); // Close the painter to release the pixmap

        // 4. Hand the transparent pixmap over to the drag
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
        // 1. SAVE THE DATA RIGHT AWAY (before the event is destroyed)
        QList<QUrl> urls = event->mimeData()->urls();
        QPoint pos = event->position().toPoint();
        QModelIndex targetIndex = indexAt(pos);

        // 2. END THE NATIVE LOOP IMMEDIATELY (releases the Linux mouse cursor/grab)
        event->setDropAction(Qt::CopyAction);
        event->accept();

        // 3. OPEN THE MENU ASYNCHRONOUSLY
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

    m_isMouseClickSelection = true;     // Enable flag: we are in a real mouse click
    QListView::mousePressEvent(event);
    m_isMouseClickSelection = false;    // Reset it right afterwards
}

// Part of mitigation for Shift+Up/Shift+Down range selection bug
void CustomListView::keyPressEvent(QKeyEvent *event) {
    bool shiftPressed = event->modifiers() & Qt::ShiftModifier;

    m_isKeyboardSelection = true;       // Enable flag: we are in a keyboard event
    QListView::keyPressEvent(event);    // Let Qt do the normal movement (changes the currentIndex)
    m_isKeyboardSelection = false;      // Reset it right afterwards

    // If Shift was NOT pressed, the selection anchor simply moves along
    if (!shiftPressed) {
        m_selectionAnchor = currentIndex();
    }
}

// Part of mitigation for Shift+Up/Shift+Down range selection bug
void CustomListView::setSelection(const QRect &rect, QItemSelectionModel::SelectionFlags command) {
    bool isShift = QGuiApplication::keyboardModifiers() & Qt::ShiftModifier;
    QModelIndex current = currentIndex();

    // We allow our linear selection ONLY if the event verifiably comes
    // from a direct key press or a stationary mouse click.
    bool safeToLinearize = m_isKeyboardSelection || m_isMouseClickSelection;

    if (!isShift || !safeToLinearize || !current.isValid() || !m_selectionAnchor.isValid()) {
        // If the user really drags (e.g. rubber band selection in empty space),
        // Qt's default behavior applies.
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

    // We feed the selection model directly with our mathematically exact range
    if (selectionModel()) {
        selectionModel()->select(linearSelection, command);
    }
}

// Part of mitigation for Shift+Up/Shift+Down range selection bug
void CustomListView::currentChanged(const QModelIndex &current, const QModelIndex &previous) {
    // Let the base class do its work first
    QListView::currentChanged(current, previous);

    // If the change comes from outside (programmatically),
    // neither the keyboard flag nor the mouse flag is set.
    // Exception: during rubber band selection, QAbstractItemView::mouseMoveEvent() sets the
    // currentIndex to the item under the mouse. That must not move the anchor.
    if (!m_isKeyboardSelection && !m_isMouseClickSelection && state() != QAbstractItemView::DragSelectingState) {
        // Set the anchor cleanly to the new current index
        m_selectionAnchor = current;
    }
}

// ---------------------------------------------------------------------------------------------
// Workaround: Rubber band gets stuck on KDE Plasma Wayland (KWin bug, not ours)
// ---------------------------------------------------------------------------------------------
// How the bug happens:
//   1. ANOTHER window is active. The user presses the left mouse button in the view (on
//      empty space) and drags a rubber band. The click activates our window; with the
//      KWin default setting "Activate, pass click and raise on release", however, it is only
//      raised on RELEASE. If our window was already active, the bug does not occur.
//   2. The user drags with the button held down until over the title bar of our own window. On
//      Plasma Wayland, that is a server-side decoration that KWin draws itself.
//      As long as the button is held, the implicit pointer grab works correctly: we
//      keep receiving MouseMove events (with negative y coordinates).
//   3. The user releases the button OVER THE TITLE BAR. Instead of a wl_pointer.button(released),
//      KWin only sends the client a wl_pointer.leave. So Qt never receives a
//      MouseButtonRelease, only a QEvent::Leave, while it still considers the button
//      pressed (QGuiApplication::mouseButtons() stays Qt::LeftButton).
//      (For the reason, see "Cause in KWin" below.)
//   4. Result: The view stays in rubber band state, the rectangle stays visible and
//      only disappears with the next click into the window.
//   Verified with a diagnostic log (Qt 6.12.0, KWin 6.8.3). Dolphin shows the same bug.
//   It does not occur on X11.
//
// Cause in KWin (traced in the KWin source code, as of KWin 6.8):
//   a) By default, a left click into an inactive window triggers the KWin window command
//      Options::MouseActivateRaiseOnReleaseAndPassClick (Options::defaultCommandWindow1(),
//      src/options.h). On PRESS, Window::performMousePressCommand() only calls
//      workspace()->requestFocus() and remembers m_delayedRaise = true. The window is only
//      raised on release. (Purpose of the setting: you can start a drag&drop out of a covered
//      window without it jumping to the front.)
//   b) On release, PointerInputRedirection::processButton() (src/pointer_input.cpp) works
//      in this order:
//        1. updateButton()   -> from now on, the button counts as released inside KWin
//        2. filter chain     -> among others WindowActionInputFilter, ForwardInputFilter at the very end
//        3. update()         -> recalculate the pointer focus (end of the implicit grab)
//      The intention is: first the release is sent to the client (step 2, ForwardInputFilter
//      -> SeatInterface::notifyPointerButton()), then the focus changes (step 3).
//   c) In step 2, however, the WindowActionInputFilter (src/input.cpp) runs BEFORE the
//      ForwardInputFilter. For the release, it executes the delayed command:
//      Window::performMouseReleaseCommand() -> performDelayedRaise() -> Workspace::raiseWindow().
//      Since our window was not on top before, the stacking order changes. The
//      StackingUpdatesBlocker in raiseWindow() SYNCHRONOUSLY emits the signal
//      Workspace::stackingOrderChanged when it goes out of scope, and that is connected to
//      InputDeviceHandler::update() (src/input.cpp, InputDeviceHandler::init()).
//   d) This premature update() is no longer blocked: focusUpdatesBlocked() only checks
//      areButtonsPressed(), and the button has been marked as released since step 1.
//      So KWin recalculates the focus. The pointer is over the title bar, so
//      updateDecoration() sets the decoration as the target and updateFocus() takes the focus
//      away from our client surface: SeatInterface::notifyPointerLeave() -> wl_pointer.leave to us.
//   e) After that, the release finally reaches the ForwardInputFilter. PointerInterface::sendButton()
//      (src/wayland/pointer.cpp), however, only sends to the currently focused surface, and that
//      does not exist anymore. The release is discarded without any message.
//   This explains all observations:
//   - If our window was already active/on top, there is no delayed raise, hence no
//     premature update() and no bug.
//   - When releasing over our own client surface, the recalculation yields the same
//     focus, hence no leave and no bug.
//   - The bug does not depend on the toolkit, which is why Dolphin has it as well.
//   A suitable fix in KWin would be: block focus recalculations while a release
//   is passing through the filter chain. The update() at the end of processButton() then catches up on them
//   in the right order (first the release to the client, then the focus change).
//
// Detection: With a correctly working implicit grab, NO leave arrives at the viewport while a
// rubber band is being held, not even when the pointer leaves the window.
// A leave in the middle of an active rubber band drag therefore means: the release was
// swallowed.
//
// Remedy: We deliver the missing release ourselves by calling mouseReleaseEvent() with a
// synthetic event at the current cursor position. This runs the regular
// cleanup code (ours and Qt's): hide the rubber band, reset the state to NoState,
// stop autoscrolling. The selection made up to that point is kept,
// just like with a normal release.
// Deliberately limited to Wayland, so that we don't change the behavior on other
// platforms. The workaround does not affect normal clicks and file drag&drop.
//
// Known leftover: QGuiApplication::mouseButtons() keeps reporting Qt::LeftButton until the next
// real click, because Qt's global state is only reset by real platform events.
// In the code, mouseButtons() is only checked for Qt::RightButton in startDrag(),
// so this is not critical.
//
// Can be removed as soon as KWin correctly delivers the release to the client when raising
// with a delay ("raise on release").
// ---------------------------------------------------------------------------------------------
bool CustomListView::viewportEvent(QEvent *event) {
    if (event->type() == QEvent::Leave
        && QGuiApplication::platformName().startsWith(QLatin1String("wayland"))
        && state() == QAbstractItemView::DragSelectingState) {   // Qt's own rubber band is active
        const QPoint localPos = viewport()->mapFromGlobal(QCursor::pos());
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(localPos), QPointF(QCursor::pos()),
                            Qt::LeftButton, Qt::NoButton, QGuiApplication::keyboardModifiers());
        mouseReleaseEvent(&release);
    }
    return QListView::viewportEvent(event);
}

bool CustomListView::interceptInvalidFileTarget(QDragMoveEvent *event, bool isEnterEvent) {
    // 1. Determine the index under the real mouse position and unwrap it
    QModelIndex index = indexAt(event->position().toPoint());
    QAbstractItemModel *currentModel = model();
    while (auto proxy = qobject_cast<QAbstractProxyModel*>(currentModel)) {
        index = proxy->mapToSource(index);
        currentModel = proxy->sourceModel();
    }
    auto *customModel = qobject_cast<CustomTableModel*>(currentModel);

    // 2. Check whether we are over a file (invalid target)
    if (customModel && index.isValid()) {
        QString itemPath = customModel->filePath(index);
        if (!itemPath.isEmpty() && !QFileInfo(itemPath).isDir()) {

            // 3. Apply a trick: send a fake event with (-1, -1) to the correct base function
            if (isEnterEvent) {
                QDragEnterEvent fakeEvent(QPointF(-1, -1), event->possibleActions(), event->mimeData(), event->buttons(), event->modifiers());
                QAbstractItemView::dragEnterEvent(&fakeEvent);
            } else {
                QDragMoveEvent fakeEvent(QPointF(-1, -1), event->possibleActions(), event->mimeData(), event->buttons(), event->modifiers());
                QAbstractItemView::dragMoveEvent(&fakeEvent);
            }

            // Signal to the operating system: drop not allowed here!
            event->ignore();
            return true; // Event intercepted successfully
        }
    }

    return false; // No file target, normal handling required
}

void CustomListView::updateTargetCache(const QPoint &pos, const QMimeData *mimeData) {
    // 1. Get the visual index and unwrap it through the proxy chain
    QModelIndex sourceIndex = indexAt(pos);
    QAbstractItemModel *currentModel = model();
    while (auto proxy = qobject_cast<QAbstractProxyModel*>(currentModel)) {
        sourceIndex = proxy->mapToSource(sourceIndex);
        currentModel = proxy->sourceModel();
    }

    auto *tableModel = qobject_cast<CustomTableModel*>(currentModel);
    if (!tableModel) return;

    // 2. Determine the target directory
    QString currentTargetDir = tableModel->currentDirectoryPath();
    if (sourceIndex.isValid() && dropIndicatorPosition() == QAbstractItemView::OnItem) {
        QString itemPath = tableModel->filePath(sourceIndex);
        if (!itemPath.isEmpty() && QFileInfo(itemPath).isDir()) {
            currentTargetDir = itemPath;
        }
    }

    // 3. If the folder has changed: recalculate the cache
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
                m_isSameDriveCached = Helpers::onSameStorageDevice(firstFile.absolutePath(), currentTargetDir);
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
