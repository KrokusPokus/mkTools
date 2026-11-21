#include "customtableview.h"
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
        dragIcon = QIcon::fromTheme("document-multiple");
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

// ---------------------------------------------------------------------------------------------
// Workaround: Gummiband bleibt unter KDE Plasma Wayland stehen (KWin-Fehler, nicht unserer)
// ---------------------------------------------------------------------------------------------
// Ablauf des Fehlers:
//   1. Ein ANDERES Fenster ist aktiv. Der User drückt die linke Maustaste in der View (auf
//      leerer Fläche) und zieht ein Gummiband auf. Der Klick aktiviert unser Fenster; mit der
//      KWin-Standardeinstellung "Activate, pass click and raise on release" wird es aber erst
//      beim LOSLASSEN nach vorne geholt. War unser Fenster schon aktiv, tritt der Fehler nicht auf.
//   2. Er zieht mit gedrückter Taste bis über die Titelleiste des eigenen Fensters. Unter
//      Plasma Wayland ist das eine serverseitige Dekoration, die KWin selbst zeichnet.
//      Solange die Taste gehalten wird, greift der implizite Pointer-Grab korrekt: Wir
//      bekommen weiter MouseMove-Events (mit negativen y-Koordinaten).
//   3. Er lässt die Taste ÜBER DER TITELLEISTE los. Statt eines wl_pointer.button(released)
//      schickt KWin dem Client nur ein wl_pointer.leave. Qt erhält also nie ein
//      MouseButtonRelease, sondern nur ein QEvent::Leave, während es die Taste weiter für
//      gedrückt hält (QGuiApplication::mouseButtons() bleibt Qt::LeftButton).
//      (Warum, steht unten unter "Ursache in KWin".)
//   4. Folge: Die View bleibt im Gummiband-Zustand, das Rechteck bleibt sichtbar stehen und
//      verschwindet erst beim nächsten Klick ins Fenster.
//   Per Diagnose-Log verifiziert (Qt 6.12.0, KWin 6.8.3). Dolphin zeigt denselben Fehler.
//   Unter X11 tritt er nicht auf.
//
// Ursache in KWin (im KWin-Quellcode nachvollzogen, Stand KWin 6.8):
//   a) Linksklick in ein inaktives Fenster löst in KWin standardmäßig den Fenster-Befehl
//      Options::MouseActivateRaiseOnReleaseAndPassClick aus (Options::defaultCommandWindow1(),
//      src/options.h). Beim DRÜCKEN ruft Window::performMousePressCommand() nur
//      workspace()->requestFocus() auf und merkt sich m_delayedRaise = true. Angehoben wird
//      das Fenster erst beim Loslassen. (Sinn der Einstellung: Man kann aus einem verdeckten
//      Fenster heraus Drag&Drop starten, ohne dass es dabei nach vorne springt.)
//   b) Beim Loslassen arbeitet PointerInputRedirection::processButton() (src/pointer_input.cpp)
//      in dieser Reihenfolge:
//        1. updateButton()   -> die Taste gilt ab jetzt KWin-intern als losgelassen
//        2. Filterkette      -> u. a. WindowActionInputFilter, ganz am Ende ForwardInputFilter
//        3. update()         -> Pointer-Fokus neu berechnen (Ende des impliziten Grabs)
//      Gedacht ist: Erst wird das Release an den Client geschickt (Schritt 2, ForwardInputFilter
//      -> SeatInterface::notifyPointerButton()), dann wechselt der Fokus (Schritt 3).
//   c) In Schritt 2 kommt aber der WindowActionInputFilter (src/input.cpp) VOR dem
//      ForwardInputFilter dran. Er führt für das Release den verzögerten Befehl aus:
//      Window::performMouseReleaseCommand() -> performDelayedRaise() -> Workspace::raiseWindow().
//      Weil unser Fenster vorher nicht ganz oben lag, ändert sich die Stapelreihenfolge. Der
//      StackingUpdatesBlocker in raiseWindow() sendet beim Verlassen SYNCHRON das Signal
//      Workspace::stackingOrderChanged, und das ist mit InputDeviceHandler::update() verbunden
//      (src/input.cpp, InputDeviceHandler::init()).
//   d) Dieses vorzeitige update() wird nicht mehr blockiert: focusUpdatesBlocked() prüft nur
//      areButtonsPressed(), und die Taste ist seit Schritt 1 schon als losgelassen markiert.
//      Also berechnet KWin den Fokus neu. Der Zeiger steht über der Titelleiste, daher setzt
//      updateDecoration() die Dekoration als Ziel und updateFocus() entzieht unserer
//      Client-Fläche den Fokus: SeatInterface::notifyPointerLeave() -> wl_pointer.leave an uns.
//   e) Danach erreicht das Release endlich den ForwardInputFilter. PointerInterface::sendButton()
//      (src/wayland/pointer.cpp) schickt aber nur an die aktuell fokussierte Fläche, und die gibt
//      es nicht mehr. Das Release wird ohne jede Meldung verworfen.
//   Das erklärt alle Beobachtungen:
//   - War unser Fenster schon aktiv/oben, gibt es kein verzögertes Anheben, also kein
//     vorzeitiges update() und keinen Fehler.
//   - Beim Loslassen über unserer eigenen Client-Fläche ergibt die Neuberechnung denselben
//     Fokus, also kein Leave und keinen Fehler.
//   - Der Fehler hängt nicht vom Toolkit ab, deshalb hat Dolphin ihn genauso.
//   Passende Korrektur in KWin wäre: Fokus-Neuberechnungen blockieren, solange ein Release
//   durch die Filterkette läuft. Das update() am Ende von processButton() holt sie danach in
//   der richtigen Reihenfolge nach (erst Release an den Client, dann Fokuswechsel).
//
// Erkennung: Bei korrekt funktionierendem implizitem Grab kommt während eines gehaltenen
// Gummibands KEIN Leave beim Viewport an, auch nicht, wenn der Zeiger das Fenster verlässt.
// Ein Leave mitten in einem aktiven Gummiband-Zug bedeutet daher: Der Release wurde
// verschluckt.
//
// Abhilfe: Wir holen den fehlenden Release selbst nach, indem wir mouseReleaseEvent() mit einem
// künstlichen Event an der aktuellen Cursorposition aufrufen. Damit läuft der reguläre
// Aufräum-Code (unser eigener und der von Qt): Gummiband ausblenden, Zustand auf NoState
// zurücksetzen, Autoscroll stoppen. Die bis dahin aufgezogene Auswahl bleibt erhalten,
// genau wie bei einem normalen Loslassen.
// Bewusst auf Wayland beschränkt, damit wir das Verhalten auf anderen Plattformen nicht
// verändern. Normale Klicks und Datei-Drag&Drop berührt der Workaround nicht.
//
// Bekannter Rest: QGuiApplication::mouseButtons() meldet bis zum nächsten echten Klick
// weiterhin Qt::LeftButton, weil der globale Zustand von Qt nur durch echte Plattform-Events
// zurückgesetzt wird. Im Code wird mouseButtons() nur in startDrag() auf Qt::RightButton
// geprüft, daher ist das unkritisch.
//
// Kann entfernt werden, sobald KWin das Release beim verzögerten Anheben ("raise on release")
// korrekt an den Client ausliefert.
// ---------------------------------------------------------------------------------------------
bool CustomTableView::viewportEvent(QEvent *event) {
    if (event->type() == QEvent::Leave
        && QGuiApplication::platformName().startsWith(QLatin1String("wayland"))
        && m_rubberBand && m_rubberBand->isVisible()) {   // unser eigenes Gummiband ist aktiv
        const QPoint localPos = viewport()->mapFromGlobal(QCursor::pos());
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(localPos), QPointF(QCursor::pos()),
                            Qt::LeftButton, Qt::NoButton, QGuiApplication::keyboardModifiers());
        mouseReleaseEvent(&release);
    }
    return QTableView::viewportEvent(event);
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
                QDragEnterEvent fakeEvent(QPointF(-1, -1), event->possibleActions(), event->mimeData(), event->buttons(), event->modifiers());
                QAbstractItemView::dragEnterEvent(&fakeEvent);
            } else {
                QDragMoveEvent fakeEvent(QPointF(-1, -1), event->possibleActions(), event->mimeData(), event->buttons(), event->modifiers());
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
                m_isSameDriveCached = Helpers::onSameStorageDevice(firstFile.absolutePath(), currentTargetDir);
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
