#include "mainwindow.h"
#include "filepropertiesdialog.h"

#include <QActionGroup>
#include <QApplication>
#include <QBuffer>
#include <QClipboard>
#include <QtConcurrent>
#include <QDateTime>
#include <QDBusConnection>
#include <QDesktopServices>
#include <QDirIterator>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHeaderView>
#include <QIcon>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMimeDatabase>
#include <QMimeType>
#include <QObject>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSettings>
#include <QSharedMemory>  // for handing over a file list in fileOperation()
#include <QShortcut>
#include <QSize>
#include <QStandardPaths>
#include <QStringList>
#include <QStringView>
#include <QThread>
#include <QUrl>
#include <QUuid>

#include <algorithm> // Für std::reverse
#include <utility> // Für std::as_const

#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <shellapi.h>
#include <shlobj.h>
#endif

MainWindow::MainWindow(const QString &targetDirectory, const QString &focusPath, QWidget *parent)
    : QMainWindow(parent), m_currentDirectory(targetDirectory)
{
    this->setMinimumSize(1, 1);
    setWindowTitle(QDir::toNativeSeparators(m_currentDirectory));
    setWindowIcon(QIcon(":/icons/app.ico"));
    resize(728, 545);

    m_centralWidget = new QWidget(this);
    setCentralWidget(m_centralWidget);

    m_mainLayout = new QVBoxLayout(m_centralWidget);

    m_mainLayout->setContentsMargins(0, 0, 0, 0);
    m_mainLayout->setSpacing(0);


    // --------------------------------------------------------------------

    m_topControlsContainerWidget = new QWidget();
    QHBoxLayout *topControlsHBoxLayout = new QHBoxLayout(m_topControlsContainerWidget);
    topControlsHBoxLayout->setContentsMargins(0, 0, 0, 0);
    topControlsHBoxLayout->setSpacing(0);

    m_LineEdit1 = new QLineEdit();
    m_LineEdit1->setStyleSheet("QLineEdit { border: 1px solid #a09beb; padding: 0px; padding-left: 4px; background-color: #1a1a1a; color: #ffffff;}");
    if (m_settings.showPlaceholderText) {
        m_LineEdit1->setPlaceholderText(tr("(filter terms)"));
    }
    topControlsHBoxLayout->addWidget(m_LineEdit1);

    m_mainLayout->addWidget(m_topControlsContainerWidget);
    m_topControlsContainerWidget->hide();

    // --------------------------------------------------------------------

    m_abstractModel = new CustomTableModel(&m_settings, 5, this);   // mkLauncher uses 5 rows: Name, Path (hidden), Size, Changed, Type [unused: Rating, Count, CRC]

    // --------------------------------------------------------------------

    m_proxyModel = new FileSortProxyModel(this);
    m_proxyModel->setSourceModel(m_abstractModel);
    m_proxyModel->setDynamicSortFilter(true);

    // --------------------------------------------------------------------

    m_tableView = new CustomTableView(this);
    m_tableView->setModel(m_proxyModel);
    m_tableView->setSortingEnabled(true);   // Unlike with TableWidget, this doesn't need turning on und off for speed optimization.
    m_proxyModel->sort(eColName, Qt::AscendingOrder);

    TableItemDelegate *tableItemDelegate = new TableItemDelegate(m_tableView);
    m_tableView->setItemDelegate(tableItemDelegate);
    CustomProxyStyle *tableStyle = new CustomProxyStyle();
    tableStyle->setParent(m_tableView);
    m_tableView->setStyle(tableStyle);
    m_tableView->horizontalHeader()->setStyle(tableStyle);

    m_tableView->setEditTriggers(QAbstractItemView::EditKeyPressed | QAbstractItemView::SelectedClicked);
    m_tableView->setContextMenuPolicy(Qt::NoContextMenu);
    m_tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tableView->setAlternatingRowColors(m_settings.alternatingRowColors);
    m_tableView->setShowGrid(m_settings.showGrid);
    m_tableView->setColumnHidden(eColPath, true);
    //m_tableView->setColumnHidden(eColCRC, true);

    m_tableView->verticalHeader()->setVisible(false);
    m_tableView->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    m_tableView->verticalHeader()->setMinimumSectionSize(0);
    m_tableView->verticalHeader()->setDefaultSectionSize(18);

    m_tableView->horizontalHeader()->setSortIndicator(eColName, Qt::AscendingOrder);
    m_tableView->horizontalHeader()->setSectionsMovable(true);
    m_tableView->horizontalHeader()->setHighlightSections(false);
    m_tableView->horizontalHeader()->setFixedHeight(22);

    m_tableView->setDragEnabled(true);
    m_tableView->setAcceptDrops(true);
    m_tableView->setDropIndicatorShown(true);
    m_tableView->setDragDropMode(QAbstractItemView::DragDrop);
    m_tableView->setDefaultDropAction(Qt::MoveAction);

#ifdef Q_OS_WIN
    if (isCurrentProcessElevated()) {
        m_tableView->setStyleSheet(
            "QTableView {"
            "    border: none;"
            "    background-color: #aa0000;"
            "    alternate-background-color: #880000;"
            "    color: white;"
            "    selection-color: white;"
            "    outline: none;"
            "}"
            "QTableView::item:hover {"
            "    background-color: #aa0000;"
            "}"
            "QTableView::item:alternate:hover {"
            "    background-color: #880000;"
            "}"
            "QTableView::item:selected:active,"
            "QTableView::item:selected:active:hover {"
            "    background-color: #6b69d6;"
            "}"
            "QTableView::item:selected:!active,"
            "QTableView::item:selected:!active:hover {"
            "    background-color: #a0a0a0;"
            "}"
            /* Styling für die Ecke unten rechts */
            "QAbstractScrollArea::corner {"
            "    background: #1b1b1b;"
            "    border: none;"
            "}"
            "QHeaderView {"
            "    background-color: #404040;"
            "    border: none;"
            "}"
            "QHeaderView::section {"
            "    border: none;" // Needed to disable native OS style that otherwise prevents background-color from working
            "    background-color: #404040;"
            "    color: #ffffff;"
            "    font-weight: normal;"
            "    border-bottom: 1px solid #616161;"
            "    border-right: 1px solid #616161;"
            "    padding-left: 6px;"
            "    padding-right: 6px;"
            "}"
            );
    } else {
        m_tableView->setStyleSheet(
            "QTableView {"
            "    border: none;"
            "    background-color: #2e2e2e;"
            "    alternate-background-color: #282828;"
            "    color: white;"
            "    selection-color: white;"
            "    outline: none;"
            "}"
            "QTableView::item:hover {"
            "    background-color: #2e2e2e;"
            "}"
            "QTableView::item:alternate:hover {"
            "    background-color: #282828;"
            "}"
            "QTableView::item:selected:active,"
            "QTableView::item:selected:active:hover {"
            "    background-color: #6b69d6;"
            "}"
            "QTableView::item:selected:!active,"
            "QTableView::item:selected:!active:hover {"
            "    background-color: #505050;"
            "}"
            /* Styling für die Ecke unten rechts */
            "QAbstractScrollArea::corner {"
            "    background: #1b1b1b;"
            "    border: none;"
            "}"
            "QHeaderView {"
            "    background-color: #404040;"
            "    border: none;"
            "}"
            "QHeaderView::section {"
            "    border: none;" // Needed to disable native OS style that otherwise prevents background-color from working
            "    background-color: #404040;"
            "    color: #ffffff;"
            "    font-weight: normal;"
            "    border-bottom: 1px solid #616161;"
            "    border-right: 1px solid #616161;"
            "    padding-left: 6px;"
            "    padding-right: 6px;"
            "}"
            );
    }

    m_tableView->verticalScrollBar()->setStyleSheet(
        "QScrollBar:vertical {"
        "    border: none;"
        "    background: #1b1b1b;"
        "    width: 17px;"
        "    margin: 5px 0px 5px 0px;"
        "}"

        "QScrollBar::handle:vertical {"
        "    background: #3f3f3f;"
        "    min-height: 20px;"
        "    margin-left: 5px;"
        "    margin-right: 5px;"
        "    border-radius: 3px;"
        "}"

        "QScrollBar::handle:vertical:hover {"
        "    background: #6966f7;"
        "}"

        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {"
        "    background: none;"
        "}"

        "QScrollBar::sub-line:vertical {"
        "    border: none;"
        "    background: #1b1b1b;"
        "    height: 5px;"
        "    subcontrol-position: top;"
        "    subcontrol-origin: margin;"
        "}"

        "QScrollBar::add-line:vertical {"
        "    border: none;"
        "    background: #1b1b1b;"
        "    height: 5px;"
        "    subcontrol-position: bottom;"
        "    subcontrol-origin: margin;"
        "}"
        );

    m_tableView->horizontalScrollBar()->setStyleSheet(
        "QScrollBar:horizontal {"
        "    border: none;"
        "    background: #1b1b1b;"
        "    height: 17px;"
        "    margin: 0px 5px 0px 5px;"
        "}"

        "QScrollBar::handle:horizontal {"
        "    background: #3f3f3f;"
        "    min-width: 20px;"
        "    margin-top: 5px;"
        "    margin-bottom: 5px;"
        "    border-radius: 3px;"
        "}"

        "QScrollBar::handle:horizontal:hover {"
        "    background: #6966f7;"
        "}"

        "QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal {"
        "    background: none;"
        "}"

        "QScrollBar::sub-line:horizontal {"
        "    border: none;"
        "    background: #1b1b1b;"
        "    width: 5px;"
        "    subcontrol-position: left;"
        "    subcontrol-origin: margin;"
        "}"

        "QScrollBar::add-line:horizontal {"
        "    border: none;"
        "    background: #1b1b1b;"
        "    width: 5px;"
        "    subcontrol-position: right;"
        "    subcontrol-origin: margin;"
        "}"
        );
#elif defined(Q_OS_LINUX)
    if (isCurrentProcessElevated()) {
        m_tableView->setStyleSheet(
            "QTableView {"
            "    background-color: #aa0000;"
            "    alternate-background-color: #880000;"
            "    color: #ffffff;"
            "}"
            "QTableView::item:hover {"
            "    background-color: #aa0000;"
            "}"
            "QTableView::item:alternate:hover {"
            "    background-color: #880000;"
            "}"
            );
    }
#endif

    // --------------------------------------------------------------------

    m_listView = new CustomListView(this);
    m_listView->setModel(m_proxyModel);
    ListItemDelegate *listItemDelegate = new ListItemDelegate(m_tableView);
    m_listView->setThumbnailMode(false); // custom function!
    m_listView->setItemDelegate(listItemDelegate);
    CustomProxyStyle *listViewStyle = new CustomProxyStyle();
    listViewStyle->setParent(m_listView);
    m_listView->setStyle(listViewStyle);

    m_listView->setViewMode(QListView::IconMode);
    m_listView->setFlow(QListView::TopToBottom);
    m_listView->setWrapping(true);
    m_listView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_listView->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_listView->setResizeMode(QListView::Adjust);
    m_listView->setIconSize(QSize(16, 16));
    m_listView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_listView->setContextMenuPolicy(Qt::NoContextMenu);
    m_listView->setEditTriggers(QAbstractItemView::EditKeyPressed | QAbstractItemView::SelectedClicked);

    m_listView->setDragEnabled(true);
    m_listView->setAcceptDrops(true);
    m_listView->setDropIndicatorShown(true);
    m_listView->setDragDropOverwriteMode(true);
    m_listView->setDragDropMode(QAbstractItemView::DragDrop);
    m_listView->setDefaultDropAction(Qt::MoveAction);

    // Wir nehmen das Selektionsmodell der Tabelle und zwingen die Liste, dasselbe zu nutzen!
    m_selectionModel = m_tableView->selectionModel();
    m_listView->setSelectionModel(m_selectionModel);

#ifdef Q_OS_WIN
    if (isCurrentProcessElevated()) {
        m_listView->setStyleSheet(
            "QListView {"
            "    border: none;"
            "    background-color: #aa0000;"
            "    alternate-background-color: #880000;"
            "    color: white;"
            "    selection-color: white;"
            "    outline: none;"
            "}"
            "QListView::item {"
            "    padding-top: 0px;"
            "    padding-bottom: 0px;"
            "    height: 18px;"
            "}"
            "QListView::item:hover {"
            "    background-color: #aa0000;"
            "}"
            "QListView::item:selected:active,"
            "QListView::item:selected:active:hover {"
            "    background-color: #6b69d6;"
            "}"
            "QListView::item:selected:!active,"
            "QListView::item:selected:!active:hover {"
            "    background-color: #a0a0a0;"
            "}"
            /* Styling für die Ecke unten rechts */
            "QAbstractScrollArea::corner {"
            "    background: #1b1b1b;"
            "    border: none;"
            "}"
            );
    } else {
        m_listView->setStyleSheet(
            "QListView {"
            "    border: none;"
            "    background-color: #2e2e2e;"
            "    color: white;"
            "    selection-color: white;"
            "    outline: none;"
            "}"
            "QListView::item {"
            "    padding-top: 0px;"
            "    padding-bottom: 0px;"
            "    height: 18px;"
            "}"
            "QListView::item:hover {"
            "    background-color: #2e2e2e;"
            "}"
            "QListView::item:selected:active,"
            "QListView::item:selected:active:hover {"
            "    background-color: #6b69d6;"
            "}"
            "QListView::item:selected:!active,"
            "QListView::item:selected:!active:hover {"
            "    background-color: #505050;"
            "}"
            /* Styling für die Ecke unten rechts */
            "QAbstractScrollArea::corner {"
            "    background: #1b1b1b;"
            "    border: none;"
            "}"
            );
    }

    m_listView->verticalScrollBar()->setStyleSheet(
        "QScrollBar:vertical {"
        "    border: none;"
        "    background: #1b1b1b;"
        "    width: 17px;"
        "    margin: 5px 0px 5px 0px;"
        "}"

        "QScrollBar::handle:vertical {"
        "    background: #3f3f3f;"
        "    min-height: 20px;"
        "    margin-left: 5px;"
        "    margin-right: 5px;"
        "    border-radius: 3px;"
        "}"

        "QScrollBar::handle:vertical:hover {"
        "    background: #6966f7;"
        "}"

        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {"
        "    background: none;"
        "}"

        "QScrollBar::sub-line:vertical {"
        "    border: none;"
        "    background: #1b1b1b;"
        "    height: 5px;"
        "    subcontrol-position: top;"
        "    subcontrol-origin: margin;"
        "}"

        "QScrollBar::add-line:vertical {"
        "    border: none;"
        "    background: #1b1b1b;"
        "    height: 5px;"
        "    subcontrol-position: bottom;"
        "    subcontrol-origin: margin;"
        "}"
        );

    m_listView->horizontalScrollBar()->setStyleSheet(
        "QScrollBar:horizontal {"
        "    border: none;"
        "    background: #1b1b1b;"
        "    height: 17px;"
        "    margin: 0px 5px 0px 5px;"
        "}"

        "QScrollBar::handle:horizontal {"
        "    background: #3f3f3f;"
        "    min-width: 20px;"
        "    margin-top: 5px;"
        "    margin-bottom: 5px;"
        "    border-radius: 3px;"
        "}"

        "QScrollBar::handle:horizontal:hover {"
        "    background: #6966f7;"
        "}"

        "QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal {"
        "    background: none;"
        "}"

        "QScrollBar::sub-line:horizontal {"
        "    border: none;"
        "    background: #1b1b1b;"
        "    width: 5px;"
        "    subcontrol-position: left;"
        "    subcontrol-origin: margin;"
        "}"

        "QScrollBar::add-line:horizontal {"
        "    border: none;"
        "    background: #1b1b1b;"
        "    width: 5px;"
        "    subcontrol-position: right;"
        "    subcontrol-origin: margin;"
        "}"
        );
#elif defined(Q_OS_LINUX)
    if (isCurrentProcessElevated()) {
        m_listView->setStyleSheet(
            "QListView {"
            "    background-color: #aa0000;"
            "    color: #ffffff;"
            "}"
            "QListView::item {"
            "    padding-top: 0px;"
            "    padding-bottom: 0px;"
            "    height: 18px;"
            "}"
            );
    } else {
        m_listView->setStyleSheet(
            "QListView::item {"
            "    padding-top: 0px;"
            "    padding-bottom: 0px;"
            "    height: 18px;"
            "}"
            );
    }
#endif

    // --------------------------------------------------------------------

    m_thumbnailView = new CustomListView(this);
    m_thumbnailView->setModel(m_proxyModel);
    ThumbItemDelegate *thumbItemDelegate = new ThumbItemDelegate(m_tableView);
    m_thumbnailView->setItemDelegate(thumbItemDelegate);
    CustomProxyStyle *thumbnailViewStyle = new CustomProxyStyle();
    thumbnailViewStyle->setParent(m_thumbnailView);
    m_thumbnailView->setStyle(thumbnailViewStyle);

    m_thumbnailView->setSelectionModel(m_selectionModel);
    m_thumbnailView->setThumbnailMode(true); // custom function!
    m_thumbnailView->setViewMode(QListView::IconMode);
    m_thumbnailView->setResizeMode(QListView::Adjust);
    m_thumbnailView->setIconSize(QSize(96, 96));
    m_thumbnailView->setGridSize(QSize());
    //m_thumbnailView->setUniformItemSizes(true);   // Enormer Performance-Schub für Qt!
    m_thumbnailView->setSpacing(2);
    m_thumbnailView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_thumbnailView->setContextMenuPolicy(Qt::NoContextMenu);
    m_thumbnailView->setEditTriggers(QAbstractItemView::EditKeyPressed | QAbstractItemView::SelectedClicked);
    m_thumbnailView->setDragEnabled(true);
    m_thumbnailView->setAcceptDrops(true);
    m_thumbnailView->setDropIndicatorShown(true);
    m_thumbnailView->setDragDropOverwriteMode(true);
    m_thumbnailView->setDragDropMode(QAbstractItemView::DragDrop);
    m_thumbnailView->setDefaultDropAction(Qt::MoveAction);

#ifdef Q_OS_WIN
    if (isCurrentProcessElevated()) {
        m_thumbnailView->setStyleSheet(
            "QListView {"
            "    border: none;"
            "    background-color: #aa0000;"
            "    color: white;"
            "    selection-color: white;"
            "    outline: none;"
            "}"
            "QListView::item:hover {"
            "    background-color: #aa0000;"
            "}"
            "QListView::item:selected:active,"
            "QListView::item:selected:active:hover {"
            "    background-color: #6b69d6;"
            "}"
            "QListView::item:selected:!active,"
            "QListView::item:selected:!active:hover {"
            "    background-color: #a0a0a0;"
            "}"
            /* Styling für die Ecke unten rechts */
            "QAbstractScrollArea::corner {"
            "    background: #1b1b1b;"
            "    border: none;"
            "}"
            );
    } else {
        m_thumbnailView->setStyleSheet(
            "QListView {"
            "    border: none;"
            "    background-color: #2e2e2e;"
            "    color: white;"
            "    selection-color: white;"
            "    outline: none;"
            "}"
            "QListView::item:hover {"
            "    background-color: #2e2e2e;"
            "}"
            "QListView::item:selected:active,"
            "QListView::item:selected:active:hover {"
            "    background-color: #6b69d6;"
            "}"
            "QListView::item:selected:!active,"
            "QListView::item:selected:!active:hover {"
            "    background-color: #505050;"
            "}"
            /* Styling für die Ecke unten rechts */
            "QAbstractScrollArea::corner {"
            "    background: #1b1b1b;"
            "    border: none;"
            "}"
            );
    }

    m_thumbnailView->verticalScrollBar()->setStyleSheet(
        "QScrollBar:vertical {"
        "    border: none;"
        "    background: #1b1b1b;"
        "    width: 17px;"
        "    margin: 5px 0px 5px 0px;"
        "}"

        "QScrollBar::handle:vertical {"
        "    background: #3f3f3f;"
        "    min-height: 20px;"
        "    margin-left: 5px;"
        "    margin-right: 5px;"
        "    border-radius: 3px;"
        "}"

        "QScrollBar::handle:vertical:hover {"
        "    background: #6966f7;"
        "}"

        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {"
        "    background: none;"
        "}"

        "QScrollBar::sub-line:vertical {"
        "    border: none;"
        "    background: #1b1b1b;"
        "    height: 5px;"
        "    subcontrol-position: top;"
        "    subcontrol-origin: margin;"
        "}"

        "QScrollBar::add-line:vertical {"
        "    border: none;"
        "    background: #1b1b1b;"
        "    height: 5px;"
        "    subcontrol-position: bottom;"
        "    subcontrol-origin: margin;"
        "}"
        );

    m_thumbnailView->horizontalScrollBar()->setStyleSheet(
        "QScrollBar:horizontal {"
        "    border: none;"
        "    background: #1b1b1b;"
        "    height: 17px;"
        "    margin: 0px 5px 0px 5px;"
        "}"

        "QScrollBar::handle:horizontal {"
        "    background: #3f3f3f;"
        "    min-width: 20px;"
        "    margin-top: 5px;"
        "    margin-bottom: 5px;"
        "    border-radius: 3px;"
        "}"

        "QScrollBar::handle:horizontal:hover {"
        "    background: #6966f7;"
        "}"

        "QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal {"
        "    background: none;"
        "}"

        "QScrollBar::sub-line:horizontal {"
        "    border: none;"
        "    background: #1b1b1b;"
        "    width: 5px;"
        "    subcontrol-position: left;"
        "    subcontrol-origin: margin;"
        "}"

        "QScrollBar::add-line:horizontal {"
        "    border: none;"
        "    background: #1b1b1b;"
        "    width: 5px;"
        "    subcontrol-position: right;"
        "    subcontrol-origin: margin;"
        "}"
        );
#elif defined(Q_OS_LINUX)
    if (isCurrentProcessElevated()) {
        m_thumbnailView->setStyleSheet(
            "QListView {"
            "    background-color: #aa0000;"
            "    color: #ffffff;"
            "}"
            );
    }
#endif

    // --------------------------------------------------------------------

    m_viewStack = new QStackedWidget(this);
    m_viewStack->addWidget(m_listView);
    m_viewStack->addWidget(m_tableView);
    m_viewStack->addWidget(m_thumbnailView);
    m_mainLayout->addWidget(m_viewStack);

    m_viewStack->setCurrentIndex(0);
    m_abstractModel->setModelViewMode(ViewMode::List);

    // --------------------------------------------------------------------
    // Shortcuts: Whole Window

    QShortcut *WindowShortcutN = new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Comma), this);
    WindowShortcutN->setContext(Qt::WindowShortcut);
    connect(WindowShortcutN, &QShortcut::activated, this, &MainWindow::action_EditSettingsFile);

    // --------------------------------------------------------------------
    // Context menu Actions

    m_actionListViewOpenFiles = new QAction(tr("Open"), this);
    connect(m_actionListViewOpenFiles, &QAction::triggered, this, &MainWindow::action_ListViewOpenFiles);

    m_actionListViewEditFiles = new QAction(tr("Edit"), this);
    m_actionListViewEditFiles->setShortcut(QKeySequence("Ctrl+E"));
    connect(m_actionListViewEditFiles, &QAction::triggered, this, &MainWindow::action_ListViewEditFiles);

    m_actionListViewCopyPaths = new QAction(tr("Copy Path"), this);
    m_actionListViewCopyPaths->setShortcut(QKeySequence("Ctrl+Shift+C"));
    m_actionListViewCopyPaths->setIcon(QIcon::fromTheme("edit-copy-path"));
    connect(m_actionListViewCopyPaths, &QAction::triggered, this, &MainWindow::action_ListViewCopyPaths);

    m_actionListViewCutFiles = new QAction(tr("Cut"), this);
    m_actionListViewCutFiles->setShortcut(QKeySequence("Ctrl+X"));
    m_actionListViewCutFiles->setIcon(QIcon::fromTheme("edit-cut"));
    connect(m_actionListViewCutFiles, &QAction::triggered, this, &MainWindow::action_ListViewCutFiles);

    m_actionListViewCopyFiles = new QAction(tr("Copy"), this);
    m_actionListViewCopyFiles->setShortcut(QKeySequence("Ctrl+C"));
    m_actionListViewCopyFiles->setIcon(QIcon::fromTheme("edit-copy"));
    connect(m_actionListViewCopyFiles, &QAction::triggered, this, &MainWindow::action_ListViewCopyFiles);

    m_actionListViewDeleteFiles = new QAction(tr("Delete"), this);
    m_actionListViewDeleteFiles->setShortcut(QKeySequence::Delete);
    m_actionListViewDeleteFiles->setIcon(QIcon::fromTheme("edit-delete"));
    connect(m_actionListViewDeleteFiles, &QAction::triggered, this, [this]() { action_ListViewDeleteFiles(true); });

    m_actionListViewRenameFiles = new QAction(tr("Rename"), this);
    m_actionListViewRenameFiles->setShortcut(QKeySequence(Qt::Key_F2));
    m_actionListViewRenameFiles->setIcon(QIcon::fromTheme("edit-rename"));
    connect(m_actionListViewRenameFiles, &QAction::triggered, this, &MainWindow::action_ListViewRenameFiles);

    m_actionListViewFileProperties = new QAction(tr("Properties"), this);
    m_actionListViewFileProperties->setShortcut(QKeySequence("Ctrl+I"));
    m_actionListViewFileProperties->setIcon(QIcon::fromTheme("document-properties"));
    connect(m_actionListViewFileProperties, &QAction::triggered, this, &MainWindow::action_ListViewFileProperties);

    m_actionListViewPasteFiles = new QAction(tr("Paste"),this);
    m_actionListViewPasteFiles->setShortcut(QKeySequence("Ctrl+V"));
    m_actionListViewPasteFiles->setIcon(QIcon::fromTheme("edit-paste"));
    connect(m_actionListViewPasteFiles, &QAction::triggered, this, &MainWindow::action_ListViewPasteFiles);


    m_actionListViewNewFolder = new QAction(tr("Folder"),this);
    m_actionListViewNewFolder->setShortcut(QKeySequence("Ctrl+N"));
#if defined(Q_OS_WIN)
    m_actionListViewNewFolder->setIcon(QIcon(m_iconProvider.icon(QFileIconProvider::Folder).pixmap(16, 16)));
#else
    m_actionListViewNewFolder->setIcon(QIcon::fromTheme("folder-new"));
#endif
    connect(m_actionListViewNewFolder, &QAction::triggered, this, &MainWindow::action_ListViewNewFolder);


    m_actionListViewNewTextFile = new QAction(tr("Text File"),this);
#if defined(Q_OS_WIN)
    QFileInfo dummyInfo("any_filename.txt");
    m_actionListViewNewTextFile->setIcon(QIcon(m_iconProvider.icon(dummyInfo).pixmap(16, 16)));
#else
    m_actionListViewNewTextFile->setIcon(QIcon::fromTheme("document-new"));
#endif
    connect(m_actionListViewNewTextFile, &QAction::triggered, this, &MainWindow::action_ListViewNewTextFile);


    m_actionViewModeList = new QAction(tr("List"),this);
    m_actionViewModeList->setIcon(QIcon::fromTheme("view-list-details"));
    m_actionViewModeList->setCheckable(true);
    m_actionViewModeList->setShortcut(QKeySequence("Ctrl+1"));
    connect(m_actionViewModeList, &QAction::triggered, this, &MainWindow::action_ViewModeList);

    m_actionViewModeDetails = new QAction(tr("Details"),this);
    m_actionViewModeDetails->setIcon(QIcon::fromTheme("view-list-tree"));
    m_actionViewModeDetails->setCheckable(true);
    m_actionViewModeDetails->setShortcut(QKeySequence("Ctrl+2"));
    connect(m_actionViewModeDetails, &QAction::triggered, this, &MainWindow::action_ViewModeDetails);

    m_actionViewModeThumbs = new QAction(tr("Thumbnails"),this);
    m_actionViewModeThumbs->setIcon(QIcon::fromTheme("view-list-icons"));
    m_actionViewModeThumbs->setCheckable(true);
    m_actionViewModeThumbs->setShortcut(QKeySequence("Ctrl+3"));
    connect(m_actionViewModeThumbs, &QAction::triggered, this, &MainWindow::action_ViewModeThumbs);

    auto *viewModeGroup = new QActionGroup(this);
    viewModeGroup->addAction(m_actionViewModeList);
    viewModeGroup->addAction(m_actionViewModeDetails);
    viewModeGroup->addAction(m_actionViewModeThumbs);
    viewModeGroup->setExclusive(true);

    m_actionSortByName = new QAction(tr("Name"),this);
    m_actionSortByName->setCheckable(true);
    connect(m_actionSortByName, &QAction::triggered, this, &MainWindow::action_SortByName);

    m_actionSortBySize = new QAction(tr("Size"),this);
    m_actionSortBySize->setCheckable(true);
    connect(m_actionSortBySize, &QAction::triggered, this, &MainWindow::action_SortBySize);

    m_actionSortByDate = new QAction(tr("Date"),this);
    m_actionSortByDate->setCheckable(true);
    connect(m_actionSortByDate, &QAction::triggered, this, &MainWindow::action_SortByDate);

    m_actionSortByType = new QAction(tr("Type"),this);
    m_actionSortByType->setCheckable(true);
    connect(m_actionSortByType, &QAction::triggered, this, &MainWindow::action_SortByType);

    auto *sortByGroup = new QActionGroup(this);
    sortByGroup->addAction(m_actionSortByName);
    sortByGroup->addAction(m_actionSortBySize);
    sortByGroup->addAction(m_actionSortByDate);
    sortByGroup->addAction(m_actionSortByType);
    sortByGroup->setExclusive(true);

    m_actionSortAscending = new QAction(tr("A-Z"),this);
    m_actionSortAscending->setCheckable(true);
    connect(m_actionSortAscending, &QAction::triggered, this, &MainWindow::action_SortAscending);

    m_actionSortDescending = new QAction(tr("Z-A"),this);
    m_actionSortDescending->setCheckable(true);
    connect(m_actionSortDescending, &QAction::triggered, this, &MainWindow::action_SortDescending);

    auto *sortOrderGroup = new QActionGroup(this);
    sortOrderGroup->addAction(m_actionSortAscending);
    sortOrderGroup->addAction(m_actionSortDescending);
    sortOrderGroup->setExclusive(true);


    if (m_settings.showIconsInMenu == false) {
        m_actionListViewOpenFiles->setIconVisibleInMenu(false);
        m_actionListViewEditFiles->setIconVisibleInMenu(false);
        m_actionListViewCopyPaths->setIconVisibleInMenu(false);
        m_actionListViewCutFiles->setIconVisibleInMenu(false);
        m_actionListViewCopyFiles->setIconVisibleInMenu(false);
        m_actionListViewDeleteFiles->setIconVisibleInMenu(false);
        m_actionListViewRenameFiles->setIconVisibleInMenu(false);
        m_actionListViewFileProperties->setIconVisibleInMenu(false);
        m_actionListViewPasteFiles->setIconVisibleInMenu(false);
        //m_actionListViewNewFolder->setIconVisibleInMenu(false);
        //m_actionListViewNewTextFile->setIconVisibleInMenu(false);
        m_actionViewModeList->setIconVisibleInMenu(false);
        m_actionViewModeDetails->setIconVisibleInMenu(false);
        m_actionViewModeThumbs->setIconVisibleInMenu(false);
    }

    if (m_settings.showShortcutsInMenu == false) {
        m_actionListViewOpenFiles->setShortcutVisibleInContextMenu(false);
        m_actionListViewEditFiles->setShortcutVisibleInContextMenu(false);
        m_actionListViewCopyPaths->setShortcutVisibleInContextMenu(false);
        m_actionListViewCutFiles->setShortcutVisibleInContextMenu(false);
        m_actionListViewCopyFiles->setShortcutVisibleInContextMenu(false);
        m_actionListViewDeleteFiles->setShortcutVisibleInContextMenu(false);
        m_actionListViewRenameFiles->setShortcutVisibleInContextMenu(false);
        m_actionListViewFileProperties->setShortcutVisibleInContextMenu(false);
        m_actionListViewPasteFiles->setShortcutVisibleInContextMenu(false);
        m_actionListViewNewFolder->setShortcutVisibleInContextMenu(false);
        m_actionListViewNewTextFile->setShortcutVisibleInContextMenu(false);
        m_actionViewModeList->setShortcutVisibleInContextMenu(false);
        m_actionViewModeDetails->setShortcutVisibleInContextMenu(false);
        m_actionViewModeThumbs->setShortcutVisibleInContextMenu(false);
    }

    // --------------------------------------------------------------------

#ifdef Q_OS_LINUX
    loadMimeCache();

    qint64 pid = QCoreApplication::applicationPid();
    QString serviceName = QString("org.mkFolderWidget.pid%1").arg(pid);

    if (QDBusConnection::sessionBus().registerService(serviceName)) {
        QDBusConnection::sessionBus().registerObject("/Main", this, QDBusConnection::ExportScriptableSlots | QDBusConnection::ExportScriptableSignals | QDBusConnection::ExportScriptableProperties );
        qDebug() << "DBus-Service erfolgreich registriert:" << serviceName;
    } else {
        qWarning() << "Konnte DBus-Service nicht registrieren:" << serviceName;
    }
#endif

    // --------------------------------------------------------------------

    // Note: any single event of any widget will first flow through this filter before reaching the target widget
    qApp->installEventFilter(this);

    m_fileSystemWatcher = new QFileSystemWatcher(this);

    // --------------------------------------------------------------------

    m_timerUpdateIcons = new QTimer(this);
    m_timerUpdateIcons->setSingleShot(true);
    connect(m_timerUpdateIcons, &QTimer::timeout, this, &MainWindow::onTimedUpdateIcons);

    m_watcherDebounceTimer = new QTimer(this);
    m_watcherDebounceTimer->setSingleShot(true);
    connect(m_watcherDebounceTimer, &QTimer::timeout, this, &MainWindow::reloadDirectory);

    m_scrollToDebounceTimer = new QTimer(this);
    m_scrollToDebounceTimer->setSingleShot(true);
    connect(m_scrollToDebounceTimer, &QTimer::timeout, this, &MainWindow::scrollToCurrentItem);

    // --------------------------------------------------------------------

    connect(m_tableView, &QTableView::doubleClicked, this, &MainWindow::onListItemDoubleClicked);
    connect(m_listView, &QListView::doubleClicked, this, &MainWindow::onListItemDoubleClicked);
    connect(m_thumbnailView, &QListView::doubleClicked, this, &MainWindow::onListItemDoubleClicked);

    connect(m_tableView->verticalScrollBar(), &QScrollBar::valueChanged, this, &MainWindow::onVerticalBarScrollChange);
    connect(m_listView->horizontalScrollBar(), &QScrollBar::valueChanged, this, &MainWindow::onHorizontalBarScrollChange);
    connect(m_thumbnailView->verticalScrollBar(), &QScrollBar::valueChanged, this, &MainWindow::onVerticalBarScrollChange);

    connect(m_tableView->horizontalHeader(), &QHeaderView::sectionClicked, this, &MainWindow::onListViewHeaderClicked);
    connect(QApplication::clipboard(), &QClipboard::dataChanged, this, &MainWindow::onClipboardChanged);
    connect(m_fileSystemWatcher, &QFileSystemWatcher::directoryChanged, this, &MainWindow::onDirectoryChangedOnDisk);
    connect(m_LineEdit1, &QLineEdit::textChanged, this, &MainWindow::onFilterBoxChange);

    connect(m_abstractModel, &CustomTableModel::filesDropped, this, &MainWindow::onFilesDropped);
    connect(m_listView, &CustomListView::filesDropped, this, &MainWindow::onFilesDropped);
    connect(m_thumbnailView, &CustomListView::filesDropped, this, &MainWindow::onFilesDropped);
    connect(m_tableView, &CustomTableView::filesDropped, this, &MainWindow::onFilesDropped);

    // part of mitigation for Shift+Pos1 / Shift+End not working in tableView
    connect(m_tableView->selectionModel(), &QItemSelectionModel::currentChanged, this, &MainWindow::onTableCurrentChanged);

    // Signale zur Deaktivierung des Watchers während Rename EditBox aktiv ist
    connect(listItemDelegate, &ListItemDelegate::editingStarted, this, [this]() {
        m_isEditingFile = true;
        m_refreshPendingWhileEditing = false;
    });
    connect(listItemDelegate, &ListItemDelegate::closeEditor, this, [this]() {
        m_isEditingFile = false;
        if (m_refreshPendingWhileEditing) {
            m_refreshPendingWhileEditing = false;
            browseFolder(m_currentDirectory);
        }
    });

    connect(tableItemDelegate, &TableItemDelegate::editingStarted, this, [this]() {
        m_isEditingFile = true;
        m_refreshPendingWhileEditing = false;
    });
    connect(tableItemDelegate, &TableItemDelegate::closeEditor, this, [this]() {
        m_isEditingFile = false;
        if (m_refreshPendingWhileEditing) {
            m_refreshPendingWhileEditing = false;
            browseFolder(m_currentDirectory);
        }
    });

    connect(thumbItemDelegate, &ThumbItemDelegate::editingStarted, this, [this]() {
        m_isEditingFile = true;
        m_refreshPendingWhileEditing = false;
    });
    connect(thumbItemDelegate, &ThumbItemDelegate::closeEditor, this, [this]() {
        m_isEditingFile = false;
        if (m_refreshPendingWhileEditing) {
            m_refreshPendingWhileEditing = false;
            browseFolder(m_currentDirectory);
        }
    });


    //qDebug() << "Unterstützte Formate:" << QImageReader::supportedImageFormats();

    browseFolder(targetDirectory, focusPath);
}

MainWindow::~MainWindow() = default;

void MainWindow::onTableCurrentChanged(const QModelIndex &current, const QModelIndex &previous) {
    Q_UNUSED(previous);

    // Nur wenn SHIFT gerade NICHT gedrückt ist, wandert der Anker mit dem Fokus mit.
    // Das fängt Mausklicks, Pfeiltasten etc. perfekt und ohne Timer ab!
    if (!(QApplication::keyboardModifiers() & Qt::ShiftModifier) && current.isValid()) {
        m_tableView->setProperty("selectionAnchor", current);
    }
}

void MainWindow::onFilterBoxChange(const QString &text) {
    if (text != m_activeFilterTerms) {
        m_activeFilterTerms = text;
        m_proxyModel->setFilterTerms(m_activeFilterTerms);
        m_timerUpdateIcons->start(50);
    }
}

void MainWindow::onVerticalBarScrollChange() {
    //m_timerCalcCrc->start(20);
    m_timerUpdateIcons->start(20);
}

void MainWindow::onHorizontalBarScrollChange() {
    //m_timerCalcCrc->start(20);
    m_timerUpdateIcons->start(20);
}

void MainWindow::onListViewHeaderClicked() {
    //m_timerCalcCrc->start(20);
    m_timerUpdateIcons->start(20);
}

void MainWindow::onToggleListViewHeader() {
    m_bHeaderVisible = !m_bHeaderVisible;
    updateColumns();
}

void MainWindow::onDirectoryChangedOnDisk(const QString &path) {
    Q_UNUSED(path);

    qDebug() << "QFileSystemWatcher::directoryChanged:" << path;
    // Used during Rename EditBox to suppress directory reloads
    if (m_isEditingFile) {
        m_refreshPendingWhileEditing = true;
        return;
    }

    // Used by "New Folder" and "New Textfile" mechanism
    if (m_ignoreNextWatcherSignal) {
        m_ignoreNextWatcherSignal = false;
        return;
    }

    m_watcherDebounceTimer->start(100);
}

void MainWindow::scrollToCurrentItem() {
    auto *activeView = qobject_cast<QAbstractItemView*>(m_viewStack->currentWidget());
    if (!activeView) return;

    if (m_proxyModel && m_proxyModel->rowCount() > 0) {
        QModelIndex currentIdx = activeView->currentIndex();

        if (currentIdx.isValid()) {
            activeView->scrollTo(currentIdx, QAbstractItemView::EnsureVisible);
        }
    }
}

void MainWindow::reloadDirectory() {
    if (!m_currentDirectory.isEmpty()) {
        browseFolder(m_currentDirectory);
    }
}

void MainWindow::browseFolder(QString directoryPath, const QString &focusPath, bool isHistoryNavigation) {
    qDebug() << "browseFolder(" << directoryPath << "," << focusPath << "," << isHistoryNavigation << ")";
    m_BenchmarkTimer.start();

    if (directoryPath != "drives://") {
        // Handle non-existing paths
        QDir dir(directoryPath);
        while (!dir.exists()) {
            if (!dir.cdUp()) {
                directoryPath = QDir::rootPath();
                break;
            }
            directoryPath = dir.absolutePath();
        }
    }

    // Save history
    if (!isHistoryNavigation && !m_currentDirectory.isEmpty() && m_currentDirectory != directoryPath) {
        m_backHistory.append(m_currentDirectory);   // Den alten Ordner auf den Back-Stack legen

        m_forwardHistory.clear();                   // Sobald der User manuell wohin klickt, wird die Vorwärts-Historie gelöscht
    }

    bool isSameDirectory = (directoryPath == m_currentDirectory);

    auto *activeView = qobject_cast<QAbstractItemView*>(m_viewStack->currentWidget());

    // --- STORE FOCUS AND SELECTION ---
    QStringList selectedPathsToRestore;
    QString focusedPathToRestore = focusPath;

    if (isSameDirectory && m_selectionModel) {
        // A) Selektierte Zeilen merken
        QModelIndexList selectedProxyIndexes = m_selectionModel->selectedIndexes();
        for (const QModelIndex &proxyIdx : std::as_const(selectedProxyIndexes)) {
            if (proxyIdx.column() != 0) {
                continue;
            }

            QModelIndex sourceIndex = m_proxyModel->mapToSource(proxyIdx);
            QString path = m_abstractModel->filePath(sourceIndex);
            if (!path.isEmpty()) {
                selectedPathsToRestore.append(path);
            }
        }

        // B) Fokussiertes (Current) Element merken (falls keines via focusPath erzwungen wurde)
        if (focusedPathToRestore.isEmpty()) {
            QModelIndex currentProxyIdx = activeView->currentIndex();
            if (currentProxyIdx.isValid()) {
                QModelIndex sourceIndex = m_proxyModel->mapToSource(currentProxyIdx);
                focusedPathToRestore = m_abstractModel->filePath(sourceIndex);
            }
        }
    }
    qDebug() << "browseFolder() focused:" << focusedPathToRestore << " Selected:" << selectedPathsToRestore;
    if (!isSameDirectory && focusPath.isEmpty()) {
        if (m_selectionModel) m_selectionModel->clear();
    }

    // --- LOAD NEW FOLDER ---

    m_abstractModel->populateModel_mkFolderWidget(directoryPath);

    m_tableView->setRootIndex(QModelIndex());
    m_listView->setRootIndex(QModelIndex());
    m_thumbnailView->setRootIndex(QModelIndex());

    m_currentDirectory = directoryPath;

    qDebug() << "browseFolder():" << m_BenchmarkTimer.elapsed() << " ms elapsed till loadDirectory() finished";

    // --- UPDATE FILE SYSTEM WATCHER ---
    QStringList watchedPaths = m_fileSystemWatcher->directories();
    if (!watchedPaths.isEmpty()) {
        m_fileSystemWatcher->removePaths(watchedPaths);
    }
    if (!directoryPath.isEmpty() && QFileInfo(directoryPath).isDir() && (directoryPath != "drives://")) {
        m_fileSystemWatcher->addPath(directoryPath);
    }
    qDebug() << "browseFolder():" << m_BenchmarkTimer.elapsed() << " ms elapsed till m_fileSystemWatcher updated";

    // --- DISABLE SIGNALS FOR SPEEDUP ---
    if (m_selectionModel) {
        m_selectionModel->blockSignals(true);
    }

    // LOAD PREVIOUS FOCUS AND SELECTION
    int rowCount = m_proxyModel->rowCount();
    QItemSelection restoreSelection;
    QModelIndex proxyIndexToFocus;

    const QSet<QString> selectedSet(selectedPathsToRestore.begin(), selectedPathsToRestore.end()); // O(n) instead of O(n²)
    if (rowCount > 0 && m_selectionModel) {
        int colCount = m_proxyModel->columnCount();
        for (int i = 0; i < rowCount; ++i) {
            QModelIndex proxyIdx = m_proxyModel->index(i, 0);
            QModelIndex sourceIndex = m_proxyModel->mapToSource(proxyIdx);
            QString currentPath = m_abstractModel->filePath(sourceIndex);

            // Prüfen, ob diese Zeile selektiert war
            if (selectedSet.contains(currentPath)) {
                QModelIndex topLeft = proxyIdx;
                QModelIndex bottomRight = m_proxyModel->index(i, colCount - 1);
                restoreSelection.select(topLeft, bottomRight);
            }

            // Prüfen, ob diese Zeile den Fokus hatte (oder haben soll)
            if (!focusedPathToRestore.isEmpty() && currentPath == focusedPathToRestore) {
                proxyIndexToFocus = proxyIdx;
            }
        }

        // Fokus anwenden
        if (proxyIndexToFocus.isValid()) {
            activeView->setCurrentIndex(proxyIndexToFocus);
        } else if (restoreSelection.isEmpty()) {
            QModelIndex firstProxyIndex = m_proxyModel->index(0, 0);
            activeView->setCurrentIndex(firstProxyIndex);
        }

        // Selektion anwenden
        if (!restoreSelection.isEmpty() && m_selectionModel) {
            m_selectionModel->select(restoreSelection, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        }
    }

    // --- RE-ENABLE SIGNALS ---
    if (m_selectionModel) {
        m_selectionModel->blockSignals(false);
    }

    if (m_currentDirectory == "drives://") {
        setWindowTitle(tr("This Computer"));
        setWindowIcon(style()->standardIcon(QStyle::SP_ComputerIcon));
    } else {
        setWindowTitle(QDir::toNativeSeparators(m_currentDirectory));
        QFileInfo fileInfo(m_currentDirectory);
        setWindowIcon(m_iconProvider.icon(fileInfo));
    }

    // --- Update columns of tableView ---
    if (m_viewStack->currentWidget() == m_tableView) {
        qDebug() << "browseFolder():" << m_BenchmarkTimer.elapsed() << " ms elapsed till before updateColumns()";
        updateColumns();
        qDebug() << "browseFolder():" << m_BenchmarkTimer.elapsed() << " ms elapsed till end of updateColumns()";
    }

    if (activeView) {
        QModelIndex currentIdx = activeView->currentIndex();
        if (currentIdx.isValid()) {
            activeView->scrollTo(currentIdx, QAbstractItemView::EnsureVisible);
        }
    }

    qDebug() << "browseFolder():" << m_BenchmarkTimer.elapsed() << " ms elapsed total";

    m_timerUpdateIcons->start(20);
}

void MainWindow::updateColumns() {
    if (m_bHeaderVisible) {
        // m_tableView->horizontalHeader()->setVisible(true);
        
        m_tableView->horizontalHeader()->setSectionResizeMode(eColSize, QHeaderView::ResizeToContents);
        m_tableView->horizontalHeader()->setSectionResizeMode(eColDate, QHeaderView::ResizeToContents);
        m_tableView->horizontalHeader()->setSectionResizeMode(eColType, QHeaderView::ResizeToContents);
        m_tableView->horizontalHeader()->setSectionResizeMode(eColName, QHeaderView::Stretch);
        m_tableView->horizontalHeader()->doItemsLayout();
        int eColNameWidth = m_tableView->columnWidth(eColName);
        m_tableView->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
        m_tableView->setColumnWidth(eColName, eColNameWidth);
    } else {
        //m_tableView->horizontalHeader()->setVisible(false);
    }
}

void MainWindow::onShowContextMenu(QAbstractItemView *senderView, const QPoint &pos) {
    if (!senderView) return;

    QModelIndex proxyIndex = senderView->indexAt(pos);
    QString filePath;

    if (proxyIndex.isValid()) {
        QModelIndex sourceIndex = m_proxyModel->mapToSource(proxyIndex);
        filePath = m_abstractModel->filePath(sourceIndex);
    }

    QMenu mainMenu(this);

    if (filePath.isEmpty()) {
        if      (m_viewStack->currentWidget() == m_listView)      m_actionViewModeList->setChecked(true);
        else if (m_viewStack->currentWidget() == m_tableView)     m_actionViewModeDetails->setChecked(true);
        else if (m_viewStack->currentWidget() == m_thumbnailView) m_actionViewModeThumbs->setChecked(true);

        QMenu *subMenuView = mainMenu.addMenu(tr("View"));
        if (m_settings.showIconsInMenu) {
            subMenuView->setIcon(QIcon::fromTheme("view-list-details"));
        }
        subMenuView->addAction(m_actionViewModeList);
        subMenuView->addAction(m_actionViewModeDetails);
        subMenuView->addAction(m_actionViewModeThumbs);

        mainMenu.addSeparator(); //-----------------------------------------

        int currentColumn = m_proxyModel->sortColumn();

        if      (currentColumn == 0) m_actionSortByName->setChecked(true);
        else if (currentColumn == 1) m_actionSortBySize->setChecked(true);
        else if (currentColumn == 2) m_actionSortByDate->setChecked(true);
        else if (currentColumn == 3) m_actionSortByType->setChecked(true);

        Qt::SortOrder currentOrder = m_proxyModel->sortOrder();
        m_actionSortAscending->setChecked(currentOrder == Qt::AscendingOrder);
        m_actionSortDescending->setChecked(currentOrder == Qt::DescendingOrder);

        QMenu *subMenuSortBy = mainMenu.addMenu(tr("Sort by"));
        if (m_settings.showIconsInMenu) {
            subMenuSortBy->setIcon(QIcon::fromTheme("view-sort"));
        }
        subMenuSortBy->addAction(m_actionSortByName);
        subMenuSortBy->addAction(m_actionSortBySize);
        subMenuSortBy->addAction(m_actionSortByDate);
        subMenuSortBy->addAction(m_actionSortByType);
        subMenuSortBy->addSeparator();
        subMenuSortBy->addAction(m_actionSortAscending);
        subMenuSortBy->addAction(m_actionSortDescending);

        mainMenu.addSeparator(); //-----------------------------------------

        mainMenu.addAction(m_actionListViewPasteFiles);
        const QMimeData *mimeData = QApplication::clipboard()->mimeData();  // Note: This CAN be slow if acquiring the clipboard is delayed.
        bool canPaste = (mimeData && mimeData->hasUrls());
        m_actionListViewPasteFiles->setEnabled(canPaste);

        mainMenu.addSeparator();

        QMenu *subMenuNew = mainMenu.addMenu(tr("New"));
        if (m_settings.showIconsInMenu) {
            subMenuNew->setIcon(QIcon::fromTheme("list-add"));
        }
        subMenuNew->addAction(m_actionListViewNewFolder);
        subMenuNew->addAction(m_actionListViewNewTextFile);

        mainMenu.addSeparator(); //-----------------------------------------
        mainMenu.addAction(m_actionListViewFileProperties);
    } else if (m_currentDirectory == "drives://") {
        mainMenu.addAction(m_actionListViewOpenFiles);
        mainMenu.setDefaultAction(m_actionListViewOpenFiles);
        mainMenu.addSeparator(); //-----------------------------------------
        mainMenu.addAction(m_actionListViewFileProperties);
    } else {
        mainMenu.addAction(m_actionListViewOpenFiles);
        mainMenu.setDefaultAction(m_actionListViewOpenFiles);

        QFileInfo fileInfo(filePath);
        QString fileExt = fileInfo.suffix().toLower();
        if (m_settings.audioExts.contains(fileExt) || m_settings.imageExts.contains(fileExt) || m_settings.textExts.contains(fileExt) || m_settings.videoExts.contains(fileExt)) {
            mainMenu.addAction(m_actionListViewEditFiles);
        }

#ifdef Q_OS_WIN
        mainMenu.addSeparator(); //-----------------------------------------
        QDir sendToDir(getSendToPath());
        if (sendToDir.exists()) {
            QMenu *sendToMenu = mainMenu.addMenu(tr("Send to"));
            QFileInfoList shortcuts = sendToDir.entryInfoList({"*.lnk"}, QDir::Files);

            for (const QFileInfo &shortcutInfo : std::as_const(shortcuts)) {
                QString displayName = shortcutInfo.completeBaseName();

                QIcon cleanIcon = m_iconProvider.icon(shortcutInfo);

                QPixmap pix = cleanIcon.pixmap(16, 16);

                QAction *sendAction = sendToMenu->addAction(QIcon(pix), displayName);
                sendAction->setData(shortcutInfo.absoluteFilePath());	// store lnk path inside sendAction object

                connect(sendAction, &QAction::triggered, this, [this, shortcutInfo]() {
                    QStringList pathList = getActiveViewPathList();
                    if (pathList.isEmpty()) return;

                    QString allParams;
                    for (const QString &p : std::as_const(pathList)) {
                        if (!allParams.isEmpty()) allParams += " ";
                        allParams += "\"" + QDir::toNativeSeparators(p) + "\"";
                    }

                    ShellExecuteW(nullptr, L"open",
                                  reinterpret_cast<const wchar_t*>(shortcutInfo.absoluteFilePath().utf16()),
                                  reinterpret_cast<const wchar_t*>(allParams.utf16()),
                                  nullptr, SW_SHOWNORMAL);
                });
            }
        }
#else
        QMimeDatabase db;
        QMimeType mime = db.mimeTypeForFile(filePath);
        QString mimeName = mime.name();

        QStringList appIds = m_mimeCache.value(mime.name());
        qDebug() << "mimeName:" << mimeName << "appIds:" << appIds;
        if (appIds.isEmpty()) {
            for (const QString &parent : mime.allAncestors()) {
                appIds = m_mimeCache.value(parent);
                if (!appIds.isEmpty()) break;
            }
        }

        if (!appIds.isEmpty()) {
            mainMenu.addSeparator(); //-----------------------------------------

            QMenu *openWithMenu = mainMenu.addMenu(tr("Open with"));
            if (m_settings.showIconsInMenu) {
                openWithMenu->setIcon(QIcon::fromTheme("system-run"));
            }
            for (const QString &id : std::as_const(appIds)) {
                DesktopEntry info = getDesktopEntryById(id);
                if (info.isValid) {
                    QAction *action = openWithMenu->addAction(QIcon::fromTheme(info.icon), info.name);
                    connect(action, &QAction::triggered, [info, filePath, this]() {
                        openFileListWithHandler(info.id, getActiveViewPathList());
                    });
                }
            }
        }
#endif
        mainMenu.addSeparator(); //-----------------------------------------
        //mainMenu.addAction(m_actionListViewBrowseToFile);
        mainMenu.addAction(m_actionListViewCopyPaths);
        mainMenu.addAction(m_actionListViewCutFiles);
        mainMenu.addAction(m_actionListViewCopyFiles);
        mainMenu.addAction(m_actionListViewDeleteFiles);
        mainMenu.addAction(m_actionListViewRenameFiles);
        mainMenu.addSeparator(); //-----------------------------------------
        mainMenu.addAction(m_actionListViewFileProperties);
    }

    mainMenu.exec(senderView->viewport()->mapToGlobal(pos));
}

// ---------------------------------------------------------------------------------------------
// Actions

QString MainWindow::getActiveViewCurrentItemPath() {
    QModelIndex proxyIndex = m_selectionModel->currentIndex();
    if (!proxyIndex.isValid()) {
        return QString();
    }
    QModelIndex sourceIndex = m_proxyModel->mapToSource(proxyIndex);
    return m_abstractModel->filePath(sourceIndex);
}

QStringList MainWindow::getActiveViewPathList() {
    QStringList pathList;
    QModelIndexList selectedIndexes = m_selectionModel->selectedIndexes();  // we use selectedIndexes() instead of selectedRows() because the latter only list rows in which all columns are selected!
    for (const QModelIndex &proxyIndex : std::as_const(selectedIndexes)) {
        if (proxyIndex.column() == eColName) {
            QModelIndex sourceIndex = m_proxyModel->mapToSource(proxyIndex);
            QString path = m_abstractModel->filePath(sourceIndex);
            if (!path.isEmpty()) {
                pathList << path;
            }
        }
    }

    return pathList;
}

QSet<int> MainWindow::getActiveViewRowSet() {
    QSet<int> rowSet;

    // 1. Alle ausgewählten Zellen/Indizes holen
    QModelIndexList selectedIndexes = m_selectionModel->selectedIndexes();  // we use selectedIndexes() instead of selectedRows() because the latter only list rows in which all columns are selected!
    if (selectedIndexes.isEmpty()) return rowSet;

    for (const QModelIndex &proxyIndex : std::as_const(selectedIndexes)) {
        if (proxyIndex.column() == eColName) {
            QModelIndex sourceIndex = m_proxyModel->mapToSource(proxyIndex);
            rowSet.insert(sourceIndex.row());
        }
    }

    return rowSet;
}

void MainWindow::onListItemDoubleClicked(const QModelIndex &proxyIndex) {
    // We put this in a timer, so we can finish processing this double click event before the folder change fires.
    // This is to prevent the glitch that the focused file in the new folder is set in name edit mode if our mouse happens to hover over it.
    QTimer::singleShot(0, this, [this]() {
        action_ListViewOpenFiles();
    });
}

void MainWindow::action_ListViewOpenFiles() {
    QString path = getActiveViewCurrentItemPath();
    if (path.isEmpty()) return;

    QFileInfo fileInfo(path);
    if (!fileInfo.exists()) {
        return;
    }

    QString fileExt = fileInfo.suffix().toLower();

    if (fileInfo.isDir()) {
        if (QGuiApplication::keyboardModifiers() & Qt::ControlModifier) {
            // Launch in new instance
            QString appPath = QCoreApplication::applicationFilePath();
            QStringList arguments;
            arguments << "-X" << QString::number(this->x() + 40)
                      << "-Y" << QString::number(this->y() + 40)
                      << "-W" << QString::number(this->width())
                      << "-H" << QString::number(this->height())
                      << "-p" << fileInfo.absoluteFilePath();
            QProcess::startDetached(appPath, arguments);
        } else {
            browseFolder(fileInfo.absoluteFilePath());
        }
        return;
    }

    if (fileExt == "desktop") {
        launchDesktopFile(getDesktopEntry(fileInfo));
#ifdef Q_OS_LINUX
    } else if (fileInfo.isExecutable() && !fileInfo.isDir() && !m_settings.audioExts.contains(fileExt) && !m_settings.imageExts.contains(fileExt) && !m_settings.videoExts.contains(fileExt)) {
        // Workaround on linux where executible files are not neccessarily executed when opened via QDesktopServices::openUrl().
        QProcess::startDetached(path, QStringList(), fileInfo.absolutePath());
#endif
    } else {
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    }
}

void MainWindow::action_ListViewEditFiles() {
    if (m_currentDirectory == "drives://") return;

    QStringList pathList = getActiveViewPathList();
    if (pathList.isEmpty()) return;

    QStringList pathListAudio;
    QStringList pathListImage;
    QStringList pathListText;
    QStringList pathListVideo;

    for (const QString &fullPath : std::as_const(pathList)) {
        QFileInfo fileInfo(fullPath);
        QString fileExt = fileInfo.suffix().toLower();
        if (fileExt.isEmpty()) {
            continue;
        }

        if (m_settings.audioExts.contains(fileExt)) {
            pathListAudio << fullPath;
        } else if (m_settings.imageExts.contains(fileExt)) {
            pathListImage << fullPath;
        } else if (m_settings.textExts.contains(fileExt)) {
            pathListText << fullPath;
        } else if (m_settings.videoExts.contains(fileExt)) {
            pathListVideo << fullPath;
        }
    }

    if (!pathListAudio.isEmpty()) {
        openFileListWithHandler(m_settings.audioEditor, pathListAudio);
    }

    if (!pathListImage.isEmpty()) {
        openFileListWithHandler(m_settings.imageEditor, pathListImage);
    }

    if (!pathListText.isEmpty()) {
        openFileListWithHandler(m_settings.textEditor, pathListText);
    }

    if (!pathListVideo.isEmpty()) {
        openFileListWithHandler(m_settings.videoEditor, pathListVideo);
    }
}

void MainWindow::action_ListViewCopyPaths() {
    if (m_currentDirectory == "drives://") return;

    QStringList pathList = getActiveViewPathList();
    if (pathList.isEmpty()) return;

    QStringList pathListNative;
    for (const QString &path : std::as_const(pathList)) {
        pathListNative << QDir::toNativeSeparators(path);
    }

#ifdef Q_OS_WIN
    QString sClipboardList = pathListNative.join("\r\n");
#else
    QString sClipboardList = pathListNative.join("\n");
#endif

    QGuiApplication::clipboard()->setText(sClipboardList);
}

void MainWindow::action_ListViewDeleteFiles(bool bRecycleOnly) {
    if (m_currentDirectory == "drives://") return;

    QStringList pathList = getActiveViewPathList();
    if (pathList.isEmpty()) {
        return;
    }

    if (!showDeleteConfirmationDialog(pathList, bRecycleOnly)) {
        return;
    }

    QList<QUrl> urlFileList;
    for (const QString &path : std::as_const(pathList)) {
        if (!path.isEmpty()) {
            urlFileList << QUrl::fromLocalFile(path);
        }
    }

    if (bRecycleOnly) {
        fileOperation(OperationType::Recycle, urlFileList, "", false);
    } else {
        fileOperation(OperationType::Delete, urlFileList, "", false);
    }

    /*
    // Wir löschen die Dateien nur noch von der Festplatte.
    // m_abstractModel bekommt das live mit.
    for (const QString &path : std::as_const(pathList)) {
        bool success = false;

        if (bRecycleOnly) {
            success = QFile::moveToTrash(path);
        } else {
            QFileInfo info(path);

            if (info.isDir()) {
                QDir dir(path);
                success = dir.removeRecursively();
            } else {
                success = QFile::remove(path);
            }
        }

        if (!success) {
            qDebug() << "Could not delete from disk:" << path;
        }
    }
    */
}

void MainWindow::action_ListViewCutFiles() {
    if (m_currentDirectory == "drives://") return;

    QStringList pathList = getActiveViewPathList();
    if (pathList.isEmpty()) return;

    m_abstractModel->setCutMarkers(pathList);
    setupClipboardForCopyOrCut(pathList, true);
}

void MainWindow::action_ListViewCopyFiles() {
    if (m_currentDirectory == "drives://") return;

    QStringList pathList = getActiveViewPathList();
    if (pathList.isEmpty()) return;

    removeCutMarkers();
    setupClipboardForCopyOrCut(pathList, false);
}

void MainWindow::setupClipboardForCopyOrCut(const QStringList &cutFilePaths, bool isCut) {
    QList<QUrl> urls;
    for (const QString &path : cutFilePaths) {
        if (!path.isEmpty()) {
            urls << QUrl::fromLocalFile(path);
        }
    }

    auto *mimeData = new QMimeData();
    mimeData->setUrls(urls);

#ifdef Q_OS_WIN
    // For MS Windows: set "Preferred DropEffect" (Copy oder Move)
    QByteArray buffer;
    if (isCut) {
        buffer.append(static_cast<char>(Qt::MoveAction));
    } else {
        buffer.append(static_cast<char>(Qt::CopyAction));
    }
    buffer.append('\0'); buffer.append('\0'); buffer.append('\0');
    mimeData->setData("Preferred DropEffect", buffer);
#elif defined(Q_OS_LINUX)
    // For Linux on GNOME-based Desktops (Nautilus, PCManFM, etc.)
    // Format: "cut" oder "copy", dann Zeilenumbruch, dann alle URLs (ebenfalls per \n getrennt)
    QByteArray gnomeData;
    if (isCut) {
        gnomeData = "cut";
    } else {
        gnomeData = "copy";
    }
    for (const QUrl &url : std::as_const(urls)) {
        gnomeData.append("\n");
        gnomeData.append(url.toEncoded());
    }
    mimeData->setData("x-special/gnome-copied-files", gnomeData);

    // For Linux on KDE-based desktops (Dolphin)
    if (isCut) {
        mimeData->setData("application/x-kde-cutselection", QByteArray("1"));
    }
#endif

    m_currentClipboardToken = QByteArray::number(QDateTime::currentMSecsSinceEpoch());
    mimeData->setData(m_privateTokenName, m_currentClipboardToken);

    QGuiApplication::clipboard()->setMimeData(mimeData);
}

void MainWindow::onClipboardChanged() {
    // We can not just always remove the markers, since we get notified of our own changes to the clipboard as well.
    // We probably should set some flag with a random value when we cut items and check if this flag is still there.
    const QMimeData* mimeData = QApplication::clipboard()->mimeData();

    if (!mimeData->hasFormat(m_privateTokenName) || mimeData->data(m_privateTokenName) != m_currentClipboardToken) {
        removeCutMarkers();
        m_currentClipboardToken.clear();
    }
}

void MainWindow::removeCutMarkers() {
    m_abstractModel->clearCutMarkers();
}

void MainWindow::action_ListViewBrowseToFile() {
    QString path = getActiveViewCurrentItemPath();
    if (path.isEmpty()) return;

    browseToFile(path, m_settings.fileManager);
}

void MainWindow::action_ListViewRenameFiles() {
    auto *activeView = qobject_cast<QAbstractItemView*>(m_viewStack->currentWidget());
    if (!activeView) return;

    QModelIndex proxyIndex = activeView->currentIndex();
    if (!proxyIndex.isValid()) return;

    proxyIndex = proxyIndex.siblingAtColumn(eColName);

    activeView->setFocus();
    activeView->setCurrentIndex(proxyIndex);

    activeView->edit(proxyIndex);
}

void MainWindow::action_ListViewFileProperties() {
    QStringList pathList = getActiveViewPathList();
    if (pathList.isEmpty()) {
        pathList = { m_currentDirectory };
    }

    auto *dialog = new FilePropertiesDialog(pathList);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

void MainWindow::action_ListViewPasteFiles() {
    if (m_currentDirectory == "drives://") return;

    const QMimeData *mimeData = QApplication::clipboard()->mimeData();
    if (!mimeData || !mimeData->hasUrls()) return;

    QList<QUrl> urlFileList = mimeData->urls();

    OperationType operationType = OperationType::Copy;

#ifdef Q_OS_WIN
    // Prüfen Windows Cut-Flag
    if (mimeData->hasFormat("Preferred DropEffect")) {
        QByteArray dropEffect = mimeData->data("Preferred DropEffect");
        if (dropEffect.size() >= static_cast<int>(sizeof(DWORD))) {
            // Die ersten 4 Bytes als DWORD interpretieren
            DWORD effect = 0;
            memcpy(&effect, dropEffect.constData(), sizeof(DWORD));

            // Wenn das MOVE-Bit gesetzt ist, ändern wir die Operation auf Verschieben
            if (effect & DROPEFFECT_MOVE) {
                operationType = OperationType::Move;
            }
        }
    }
#elif defined(Q_OS_LINUX)
    // Prüfen auf KDE-Cut-Flag
    if (mimeData->hasFormat("application/x-kde-cutselection")) {
        if (mimeData->data("application/x-kde-cutselection") == "1") {
            operationType = OperationType::Move;
        }
    }
    // Prüfen auf GNOME-Cut-Flag (falls von Nautilus o.ä. ausgeschnitten wurde)
    else if (mimeData->hasFormat("x-special/gnome-copied-files")) {
        QString gnomeData = QString::fromUtf8(mimeData->data("x-special/gnome-copied-files"));
        if (gnomeData.startsWith("cut")) {
            operationType = OperationType::Move;
        }
    }
#endif

    fileOperation(operationType, urlFileList, m_currentDirectory, true);
}

void MainWindow::onFilesDropped(const QList<QUrl> &urlList, const QString &targetDir, Qt::DropAction dropAction) {

    OperationType operationType = OperationType::Copy;

    if (dropAction == Qt::CopyAction) {
        operationType = OperationType::Copy;
    } else if (dropAction == Qt::MoveAction) {
        operationType = OperationType::Move;
    } else if (dropAction == Qt::LinkAction) {
        operationType = OperationType::Link;
    } else {
        return;
    }

    fileOperation(operationType, urlList, targetDir, false);
}

void MainWindow::fileOperation(OperationType operationType, const QList<QUrl> &urlList, const QString &targetDir, bool fromClipboard) {
    if (urlList.isEmpty()) return;

    QFileInfo firstFile(urlList.first().toLocalFile());
    if (firstFile.absolutePath() == targetDir && operationType == OperationType::Move) {
        return;
    }

    if (operationType == OperationType::Link) {
        for (const QUrl &url : urlList) {
            if (!url.isLocalFile()) continue;

            QString targetPath = url.toLocalFile();
            QFileInfo targetInfo(targetPath);

            QString baseName = targetInfo.completeBaseName();
            QString ext = targetInfo.suffix();
            QString dotExt = ext.isEmpty() ? "" : "." + ext;

            QString linkFileName = targetInfo.fileName();

#if defined(Q_OS_WIN)
            // Windows-Spezifisch: Eine Shell-Verknüpfung MUSS zwingend auf .lnk enden
            if (!linkFileName.endsWith(".lnk", Qt::CaseInsensitive)) {
                linkFileName += ".lnk";
            }
#endif
            QDir dir(targetDir);
            QString fullLinkPath = dir.filePath(linkFileName);

            if (QFile::exists(fullLinkPath)) {
                int counter = 1;
                while (QFile::exists(fullLinkPath)) {
#if defined(Q_OS_WIN)
                    // Erzeugt z.B. "datei (1).txt.lnk" oder "ordner (1).lnk"
                    QString numberedName = QString("%1 (%2)%3.lnk").arg(baseName).arg(counter).arg(dotExt);
#else
                    // Erzeugt z.B. "datei (1).txt" oder "ordner (1)" (Symlink unter Linux)
                    QString numberedName = QString("%1 (%2)%3").arg(baseName).arg(counter).arg(dotExt);
#endif
                    fullLinkPath = dir.filePath(numberedName);
                    counter++;
                }
            }

            if (QFile::link(targetPath, fullLinkPath)) {
                qDebug() << "Symlink created:" << fullLinkPath << "->Target->" << targetPath;
            } else {
                qCritical() << "Symlink creation failed:" << targetPath;
            }
        }

        return;
    }


    // Pfad zum separaten Tool im selben Ordner ermitteln
    QString programName = "mkTransactionHandler";
#if defined(Q_OS_WIN)
    programName += ".exe";
#endif
    QString appDir = QCoreApplication::applicationDirPath();
    QString programPath = QDir(appDir).filePath(programName);

    if (!QFile::exists(programPath)) {
        return;
    }

    // 1. Daten in ein JSON-Objekt verpacken
    QJsonObject jsonObj;
    jsonObj["targetDir"] = targetDir;
    jsonObj["opType"] = static_cast<int>(operationType);
    jsonObj["fromClipboard"] = fromClipboard;

    QJsonArray urlArray;
    for (const QUrl &url : urlList) {
        urlArray.append(url.toString());
    }
    jsonObj["urls"] = urlArray;

    QByteArray jsonData = QJsonDocument(jsonObj).toJson(QJsonDocument::Compact);

    // 2. Einen einzigartigen Schlüssel für den Speicher erzeugen
    QString memoryKey = "mkTransactionHandler_" + QUuid::createUuid().toString(QUuid::WithoutBraces);

    // 3. Shared Memory reservieren und JSON hineinschreiben
    auto *sharedMemory = new QSharedMemory(memoryKey, this);
    if (sharedMemory->create(jsonData.size())) {
        sharedMemory->lock();
        char *to = static_cast<char*>(sharedMemory->data());
        const char *from = jsonData.data();
        memcpy(to, from, qMin(sharedMemory->size(), jsonData.size()));
        sharedMemory->unlock();

        // Hinweis: Wir löschen sharedMemory hier NICHT sofort, da das CopyTool
        // ein paar Millisekunden braucht, um sich anzudocken.
        // Es wird automatisch gelöscht, wenn das MainWindow geschlossen wird.
    } else {
        qCritical() << "Konnte Shared Memory nicht erstellen:" << sharedMemory->errorString();
        return;
    }

    QStringList arguments;
    arguments << memoryKey;
    arguments << QString::number(jsonData.size());

    qint64 pid;
    bool success = QProcess::startDetached(programPath, arguments, QString(), &pid);

    if (success) {
        qDebug() << "mkTransactionHandler erfolgreich im Hintergrund gestartet. PID:" << pid;
        if (fromClipboard && operationType == OperationType::Move) {
            QApplication::clipboard()->clear();
        }
    } else {
        qCritical() << "Fehler beim Starten von mkTransactionHandler!";
    }
}

void MainWindow::action_ViewModeList() {
    setMainViewMode(ViewMode::List);
}

void MainWindow::action_ViewModeDetails() {
    setMainViewMode(ViewMode::Detail);
}

void MainWindow::action_ViewModeThumbs() {
    setMainViewMode(ViewMode::Thumbnail);
}

void MainWindow::action_SortByName() {
    m_proxyModel->sort(eColName, Qt::AscendingOrder);
    m_tableView->horizontalHeader()->setSortIndicator(eColName, Qt::AscendingOrder);
    m_timerUpdateIcons->start(20);
}

void MainWindow::action_SortBySize() {
    m_proxyModel->sort(eColSize, Qt::DescendingOrder);
    m_tableView->horizontalHeader()->setSortIndicator(eColSize, Qt::DescendingOrder);
    m_timerUpdateIcons->start(20);
}

void MainWindow::action_SortByDate() {
    m_proxyModel->sort(eColDate, Qt::DescendingOrder);
    m_tableView->horizontalHeader()->setSortIndicator(eColDate, Qt::DescendingOrder);
    m_timerUpdateIcons->start(20);
}

void MainWindow::action_SortByType() {
    m_proxyModel->sort(eColType, Qt::AscendingOrder);
    m_tableView->horizontalHeader()->setSortIndicator(eColType, Qt::AscendingOrder);
    m_timerUpdateIcons->start(20);
}

void MainWindow::action_SortAscending() {
    int currentColumn = m_proxyModel->sortColumn();
    m_proxyModel->sort(currentColumn, Qt::AscendingOrder);
    m_tableView->horizontalHeader()->setSortIndicator(currentColumn, Qt::AscendingOrder);
    m_timerUpdateIcons->start(20);
}

void MainWindow::action_SortDescending() {
    int currentColumn = m_proxyModel->sortColumn();
    m_proxyModel->sort(currentColumn, Qt::DescendingOrder);
    m_tableView->horizontalHeader()->setSortIndicator(currentColumn, Qt::DescendingOrder);
    m_timerUpdateIcons->start(20);
}


void MainWindow::action_EditSettingsFile() {
    QDesktopServices::openUrl(QUrl::fromLocalFile(m_settings.getSettingsPath()));
}

void MainWindow::onTimedUpdateIcons() {
    if (!m_proxyModel || m_proxyModel->rowCount() == 0) return;
    if (!m_abstractModel) return;

    // 1. Herausfinden, welche View gerade sichtbar ist
    auto *activeView = qobject_cast<QAbstractItemView*>(m_viewStack->currentWidget());
    if (!activeView) return;

    int firstVisible = 0;
    int lastVisible = m_proxyModel->rowCount() - 1;

    // --- VIEW-SPEZIFISCHE ERMITTLUNG DER SICHTBARKEIT ---

    if (auto *tableView = qobject_cast<QTableView*>(activeView)) {
        // Fall A: Tabelle (scrollt klassisch von oben nach unten)
        firstVisible = tableView->rowAt(0);
        lastVisible = tableView->rowAt(tableView->viewport()->height() - 1);

        if (firstVisible == -1) firstVisible = 0;
        if (lastVisible == -1) lastVisible = m_proxyModel->rowCount() - 1;

    } else if (auto *listView = qobject_cast<QListView*>(activeView)) {
        // Fall B: Deine Wrapping-Liste (bricht unten um, neue Spalten rechts)

        // 1. Die absolut erste sichtbare Datei oben links bestimmen
        QModelIndex firstIdx = listView->indexAt(QPoint(5, 5));
        firstVisible = firstIdx.isValid() ? firstIdx.row() : 0;

        QRect viewportRect = listView->viewport()->rect();
        lastVisible = firstVisible;

        // 2. Wir laufen von 'firstVisible' vorwärts durch die Dateien.
        // Da die Dateien Spalte für Spalte von links nach rechts abgelegt werden,
        // können wir den Loop abbrechen, sobald eine Datei zu weit rechts liegt!
        for (int i = firstVisible; i < m_proxyModel->rowCount(); ++i) {
            QModelIndex idx = m_proxyModel->index(i, 0);
            QRect itemRect = listView->visualRect(idx);

            // Prüfen, ob die Datei im sichtbaren Viereck liegt
            if (viewportRect.intersects(itemRect)) {
                lastVisible = i; // Gültige sichtbare Datei gefunden
            }
            // WICHTIGER ABBRUCH: Liegt die linke Kante der Datei bereits rechts außerhalb des sichtbaren Viewports?
            else if (itemRect.left() > viewportRect.right()) {
                break;
            }
        }
    }

    // Sicherheitsnetz für die Indizes
    firstVisible = qMax(0, firstVisible);
    lastVisible  = qMin(m_proxyModel->rowCount() - 1, lastVisible);

    const int MAX_ICONS_PER_BATCH = 5; // Verhindert das Einfrieren der GUI
    int iconsProcessedInThisBatch = 0;
    bool processMoreLater = false;

    bool isThumbnailMode = (m_viewStack->currentWidget() == m_thumbnailView);

    // 3. Nur die aktuell sichtbaren Zeilen durchlaufen
    for (int i = firstVisible; i <= lastVisible; ++i) {
        if (i >= m_proxyModel->rowCount()) {
            break;
        }

        QModelIndex proxyIndex = m_proxyModel->index(i, 0);
        if (!proxyIndex.isValid()) {
            continue;
        }

        QModelIndex sourceIndex = m_proxyModel->mapToSource(proxyIndex);
        if (!sourceIndex.isValid()) {
            continue;
        }

        if (sourceIndex.row() >= m_abstractModel->rowCount(QModelIndex())) {
            continue;
        }

        QString fullPath = m_abstractModel->filePath(sourceIndex);
        if (fullPath.isEmpty()) continue;

        QFileInfo fileInfo(fullPath);
        if (!fileInfo.exists()) {
            continue;
        }

        if (isThumbnailMode) {
            // Prüfen, ob das Bild im Cache existiert UND noch aktuell ist
            if (m_abstractModel->isThumbnailUpToDate(fullPath, fileInfo.lastModified())) {
                continue;
            }

            if (m_loadingThumbnails.contains(fullPath)) {
                continue;
            }

            if (iconsProcessedInThisBatch >= 2) { // Kleineres Limit für Thumbnails!
                processMoreLater = true;
                break;
            }

            // Unterstützte Formate statisch cachen (wird nur einmalig beim allerersten Aufruf erstellt)
            static const QSet<QByteArray> supportedFormats = []() {
                auto formats = QImageReader::supportedImageFormats();
                return QSet<QByteArray>(formats.begin(), formats.end());
            }();

            if (supportedFormats.contains(fileInfo.suffix().toLower().toUtf8()) && !hasIconExt(fileInfo)) {
                m_loadingThumbnails.insert(fullPath);

                auto *watcher = new QFutureWatcher<QImage>(this);
                watcher->setProperty("filePath", fullPath);

                connect(watcher, &QFutureWatcher<QImage>::finished, this, [this, watcher]() {
                    QString path = watcher->property("filePath").toString();
                    QImage img = watcher->result();

                    m_loadingThumbnails.remove(path);

                    if (!img.isNull()) {
                        QFileInfo fi(path);
                        m_abstractModel->addPathThumbnail(path, QPixmap::fromImage(img), fi.lastModified(), -1);
                    } else {
                        qDebug() << "QFutureWatcher generateThumbnailAsync() couldn't get img for path" << path;
                    }

                    watcher->deleteLater();
                });

                QFuture<QImage> future = QtConcurrent::run(&MainWindow::generateThumbnailAsync, fileInfo);
                watcher->setFuture(future);

                continue;
            } else {
                QPixmap thumbIcon = generateThumbnailIcon(fileInfo);
                m_abstractModel->addPathThumbnail(fullPath, thumbIcon, fileInfo.lastModified(), sourceIndex.row());
                iconsProcessedInThisBatch++;
            }
        }
        else {
            // Prüfen, ob das Icon im Cache existiert UND noch aktuell ist
            if (m_abstractModel->isPathIconUpToDate(fullPath, fileInfo.lastModified())) {
                continue;
            }

            // Wenn wir unser Batch-Limit für diesen Frame erreicht haben, brechen wir ab und merken uns, dass wir noch nicht fertig sind!
            if (iconsProcessedInThisBatch >= MAX_ICONS_PER_BATCH) {
                processMoreLater = true;
                break;
            }

            // 5. Feststellen, ob ein individuelles Icon benötigt wird

            bool needsTrueIcon = false;
#ifdef Q_OS_WIN
            needsTrueIcon = (fileInfo.isDir() && (GetFileAttributesW(reinterpret_cast<const WCHAR*>(fullPath.utf16())) & FILE_ATTRIBUTE_READONLY)) ||
                            fullPath.endsWith(".exe", Qt::CaseInsensitive) ||
                            fullPath.endsWith(".ico", Qt::CaseInsensitive) ||
                            fullPath.endsWith(".lnk", Qt::CaseInsensitive) ||
                            fullPath.endsWith(".msi", Qt::CaseInsensitive) ||
                            fullPath.endsWith(".cur", Qt::CaseInsensitive) ||
                            fullPath.endsWith(".ani", Qt::CaseInsensitive);
#else
            needsTrueIcon = fileInfo.isDir() ||
                            fileInfo.isExecutable() ||
                            fullPath.endsWith(".desktop", Qt::CaseInsensitive);
#endif

            if (needsTrueIcon) {
                QIcon trueIcon = m_iconProvider.icon(fileInfo);

                // addPathIcon speichert es im Cache UND löst den Neuzeichen-Befehl für die Zeile aus
                // WICHTIG: Wir übergeben sourceIndex.row(), damit das Modell
                // die richtige Zeile im std::vector aktualisiert!

                m_abstractModel->addPathIcon(fullPath, trueIcon, fileInfo.lastModified(), sourceIndex.row());

                // Wir zählen nur die wirklich *geladenen* Icons, da diese CPU-Zeit kosten
                iconsProcessedInThisBatch++;
            }
        }
    }

    // Wenn die Schleife abgebrochen wurde, weil noch Icons fehlen,
    // fordern wir SOFORT (0 ms) den nächsten Batch an.
    // Qt nutzt die 0 ms, um kurz zu prüfen, ob der Nutzer geklickt oder gescrollt hat!
    if (processMoreLater) {
        m_timerUpdateIcons->start(0);
    }
}

#ifdef Q_OS_WIN
QString MainWindow::getSendToPath() {
    PWSTR path = nullptr;
    // FOLDERID_SendTo ist die offizielle GUID für diesen Ordner
    HRESULT hr = SHGetKnownFolderPath(FOLDERID_SendTo, 0, nullptr, &path);
    if (SUCCEEDED(hr)) {
        QString result = QString::fromWCharArray(path);
        CoTaskMemFree(path); // Wichtig: Speicher freigeben
        return result;
    }
    return QString();
}
#endif

void MainWindow::loadMimeCache() {
    m_mimeCache.clear();
    m_mimeCache.reserve(500);

    QStringList appDirs = QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation);  // Order: User before System
    std::reverse(appDirs.begin(), appDirs.end());   // Reverse to System before User, so we can overwrite System with User keys while parsing
    for (const QString &dirPath : std::as_const(appDirs)) {
        QString cachePath = QDir(dirPath).filePath("mimeinfo.cache");
        parseMimeInfoCache(cachePath);
    }

    parseMimeAppsList(QDir(QDir::homePath()).filePath(".config/mimeapps.list"));
}

void MainWindow::parseMimeInfoCache(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;

    QTextStream in(&file);
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();

        auto equalsPos = line.indexOf('=');
        if (equalsPos < 1) continue;  // -1 (kein '=') und 0 (leerer mime) überspringen

        QString mime = line.first(equalsPos).trimmed();
        QStringList newApps = line.sliced(equalsPos + 1).split(';', Qt::SkipEmptyParts);

        QStringList &currentApps = m_mimeCache[mime];
        for (const QString &app : std::as_const(newApps)) {
            QString trimmed = app.trimmed();
            if (!trimmed.isEmpty() && !currentApps.contains(trimmed)) {
                currentApps.append(trimmed);
            }
        }
    }
}

void MainWindow::parseMimeAppsList(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;

    QTextStream in(&file);
    QString currentGroup;

    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith('#')) continue;

        if (line.startsWith('[') && line.endsWith(']')) {
            currentGroup = line.mid(1, line.length() - 2);
            continue;
        }

        auto equalsPos = line.indexOf('=');
        if (equalsPos < 1) continue; // -1 (kein '=') und 0 (leerer mime) überspringen

        QString mime = line.first(equalsPos).trimmed();
        QStringList apps = line.sliced(equalsPos + 1).trimmed().split(';', Qt::SkipEmptyParts);

        if (currentGroup == "Added Associations" || currentGroup == "Default Applications") {
            QStringList &currentApps = m_mimeCache[mime];

            for (int i = apps.size() - 1; i >= 0; --i) {
                QString app = apps.at(i).trimmed();
                if (app.isEmpty()) continue;

                currentApps.removeAll(app);
                currentApps.prepend(app);
            }
        }
        else if (currentGroup == "Removed Associations") {
            QStringList &currentApps = m_mimeCache[mime];
            for (const QString &app : std::as_const(apps)) {
                currentApps.removeAll(app.trimmed());
            }
        }
    }
}

void MainWindow::setMainViewMode(ViewMode index) {
    if (m_viewStack->currentIndex() == index) {
        return;
    }

    m_viewStack->setCurrentIndex(index);

    if (m_viewStack->currentWidget() == m_tableView) {
        updateColumns();

        QItemSelectionModel *selectionModel = m_tableView->selectionModel();
        if (selectionModel && selectionModel->hasSelection()) {
            QModelIndexList currentSel = selectionModel->selectedIndexes();

            QItemSelection rowSelection;
            for (const QModelIndex &idx : std::as_const(currentSel)) {
                if (idx.column() == 0) {
                    rowSelection.select(idx, idx);
                }
            }

            selectionModel->select(rowSelection, QItemSelectionModel::Select | QItemSelectionModel::Rows);
        }
    }

    if (m_viewStack->currentWidget() == m_listView) {
        m_abstractModel->setModelViewMode(ViewMode::List);
    }
    else if (m_viewStack->currentWidget() == m_tableView) {
        m_abstractModel->setModelViewMode(ViewMode::Detail);
    }
    else if (m_viewStack->currentWidget() == m_thumbnailView) {
        m_abstractModel->setModelViewMode(ViewMode::Thumbnail);
    }
    else {
        m_abstractModel->setModelViewMode(ViewMode::List);
    }

    m_timerUpdateIcons->start(20);
}

bool MainWindow::showDeleteConfirmationDialog(const QStringList &pathList, bool bRecycleOnly) {

    QString sTitle;
    QString sText;
    QString sWarning;
    QMessageBox::Icon iIcon;

    if (bRecycleOnly) {
        iIcon = QMessageBox::Question;
        sWarning = "";
        if (pathList.size() == 1) {
            sTitle = tr("Delete File");
            sText = tr("Do you really want to move this file into the recycle bin?");
        } else {
            sTitle = tr("Delete multiple elements");
            sText = QString(tr("Do you really want to move these %1 files into the recycle bin?")).arg(pathList.size());
        }
    } else {
        iIcon = QMessageBox::Warning;
        sWarning = "<p style='color: red;'><i>" + tr("This process cannot be undone.") + "</i></p>";
        if (pathList.size() == 1) {
            sTitle = tr("Delete File");
            sText = tr("Are you sure you want to delete this file permanently?");
        } else {
            sTitle = tr("Delete multiple elements");
            sText = QString(tr("Are you sure you want to delete these %1 files permanently?")).arg(pathList.size());
        }
    }

    QMessageBox msgBox(this);
    msgBox.setWindowTitle(sTitle);
    msgBox.setIcon(iIcon);

    if (pathList.size() == 1) {
        QFileInfo fileInfo(pathList.first());
        QString fileName = fileInfo.fileName();
        QString size = formatAdaptiveSize(fileInfo.size());
        QString lastModified = fileInfo.lastModified().toString("yyyy-MM-dd  HH:mm:ss");

        QIcon icon = m_iconProvider.icon(fileInfo);
        QPixmap pix = icon.pixmap(QSize(48, 48));
        if (!hasIconExt(fileInfo)) {
            QPixmap thumb = generateThumbnail(fileInfo);
            if (!thumb.isNull()) {
                pix = thumb;
            }
        }

        QByteArray ba;
        QBuffer bu(&ba);
        pix.save(&bu, "PNG");
        QString imgBase64 = ba.toBase64();

        msgBox.setText(QString("<h3>%1</h3>").arg(sText));

        const QString htmlTemplate = QStringLiteral(R"(
            <table width='100%' cellspacing='0' cellpadding='0'>
                <tr>
                    <td rowspan='4' width='48' valign='top' style='padding-right: 10px;'>
                        <img src='data:image/png;base64,%1'>
                    </td>
                    <td style='color: #555; padding: 2px 8px;' width='1%'>%2</td>
                    <td style='color: #555; padding: 2px 8px;'>%3</td>
                </tr>
                <tr>
                    <td style='color: #555; padding: 2px 8px;'>%4</td>
                    <td style='color: #555; padding: 2px 8px;'>%5</td>
                </tr>
                <tr>
                    <td style='color: #555; padding: 2px 8px;'>%6</td>
                    <td style='color: #555; padding: 2px 8px;'>%7</td>
                </tr>
                <tr>
                    <td colspan='2' style='padding: 8px 8px 2px 8px;'>%8</td>
                </tr>
            </table>
            )");


        msgBox.setInformativeText(htmlTemplate.arg(
            imgBase64,
            tr("Name:"),
            fileName,
            tr("Size:"),
            size,
            tr("Date:"),
            lastModified,
            sWarning
            ));
    } else {
        msgBox.setText(QString("<h3>%1</h3>").arg(sText));
        msgBox.setInformativeText(sWarning);
    }

    QPushButton *deleteButton = msgBox.addButton(tr("Delete"), QMessageBox::AcceptRole);
    msgBox.addButton(tr("Cancel"), QMessageBox::RejectRole);
    msgBox.setDefaultButton(deleteButton);
    deleteButton->setStyleSheet("QPushButton { font-weight: bold; min-width: 80px; }");

    msgBox.exec();

    if (msgBox.clickedButton() != deleteButton) {
        return false;
    }

    return true;
}

void MainWindow::selectAllItems() {
    auto *activeView = qobject_cast<QAbstractItemView*>(m_viewStack->currentWidget());
    if (!activeView || !m_proxyModel || m_proxyModel->rowCount() == 0) {
        return;
    }
    // Auswahl direkt auf dem Proxy-Modell erzeugen
    QModelIndex topLeft = m_proxyModel->index(0, 0);
    QModelIndex bottomRight = m_proxyModel->index(m_proxyModel->rowCount() - 1, m_proxyModel->columnCount() - 1);
    QItemSelection selection(topLeft, bottomRight);
    activeView->selectionModel()->select(selection, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
}

QPixmap MainWindow::generateThumbnailIcon(const QFileInfo &fileInfo) {
    QIcon trueIcon = m_iconProvider.icon(fileInfo);
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

// Async version must return QImage: QPixmap would NOT be thread save!
QImage MainWindow::generateThumbnailAsync(const QFileInfo &fileInfo) {

    QImageReader reader(fileInfo.absoluteFilePath());
    reader.setAutoTransform(true); // Wichtig für EXIF-Rotationen von Smartphones

    if (reader.canRead()) {
        // 1. Die echte Dimension des Bildes auslesen (kostet kaum Performance)
        QSize originalSize = reader.size();

        // 2. Proportionale Größe berechnen, die in eine 96x96 Box passt
        // Aus z.B. 1920x1080 wird hier automatisch 96x54
        QSize scaledSize = originalSize.scaled(QSize(96, 96), Qt::KeepAspectRatio);

        // 3. Dem Reader die proportionale Größe mitteilen
        reader.setScaledSize(scaledSize);

        QImage img = reader.read();
        if (!img.isNull()) {
            return img;
        }
    }

    return QImage();
}

//######################################################################################
// Functions to receive data from other applications

// This belongs to a Public Slot!
// Can be called over DBus to hand over a path to browse to.
// It serves the same purpose as the WM_COPYDATA mechanism in nativeEvent() on Windows.
void MainWindow::openPath(const QString &path) {
    if (path.isEmpty()) return;

    QDir targetDir(path);
    if (targetDir.exists()) {
        browseFolder(path);
    }
}

#ifdef Q_OS_WIN
bool MainWindow::nativeEvent(const QByteArray &eventType, void *message, qintptr *result) {
    if (eventType == "windows_generic_MSG") {
        MSG *msg = static_cast<MSG*>(message);

        if (msg->message == WM_COPYDATA) {
            COPYDATASTRUCT *cds = reinterpret_cast<COPYDATASTRUCT*>(msg->lParam);
            if (cds && cds->lpData && cds->cbData > 0) {

                int maxCharCount = cds->cbData / sizeof(wchar_t);

                // Read complete string. May contain garbage.
                QString receivedData = QString::fromWCharArray(reinterpret_cast<const wchar_t*>(cds->lpData), maxCharCount);

                // Remove everything starting with first zero terminator
                int nullPos = receivedData.indexOf(QChar('\0'));
                if (nullPos != -1) {
                    receivedData.truncate(nullPos);
                }

                if (!receivedData.isEmpty()) {
                    QStringList parts = receivedData.split('|');
                    if (parts.size() >= 2) {
                        QString targetPath = QDir::cleanPath(parts.at(0));
                        QString focusFile  = parts.at(1);

                        targetPath = QDir::cleanPath(targetPath);

                        if (targetPath == "::{20d04fe0-3aea-1069-a2d8-08002b30309d}") {
                            browseFolder("drives://", QString());
                            *result = TRUE;
                            return true;
                        }

                        QDir targetDir(targetPath);
                        if (targetDir.exists()) {
                            QString focusPath;
                            if (!focusFile.isEmpty()) {
                                focusPath = targetDir.filePath(focusFile);

                                // Optionaler Sicherheits-Check: Verhindern, dass focusPath
                                // aus dem targetDir ausbricht (z.B. durch "../..")
                                focusPath = QDir::cleanPath(focusPath);
                                if (!focusPath.startsWith(targetDir.absolutePath())) {
                                    focusPath.clear();
                                }
                            }

                            browseFolder(targetPath, focusPath);
                        }
                    }
                }

                *result = TRUE;
                return true;
            }
        }
    }
    return QMainWindow::nativeEvent(eventType, message, result);
}
#endif

void MainWindow::closeEvent(QCloseEvent *event) {
    // Wenn du QtConcurrent nutzt, kannst du den globalen Threadpool zwingen,
    // nicht auf ausstehende Aufgaben zu warten (Vorsicht: Laufende Threads werden trotzdem beendet):
    QThreadPool::globalInstance()->clear();
    event->accept();
}

// Installed on qApp
bool MainWindow::eventFilter(QObject *obj, QEvent *event) {
    // Part 1/3 of mitigation for left click on focused item in inactive windows accidentally triggering rename
    if (event->type() == QEvent::ActivationChange) {
        if (obj == this && isActiveWindow()) {
            m_lastActivationTime = QDateTime::currentMSecsSinceEpoch();
        }
    }

    if (event->type() == QEvent::Resize) {
        if (obj == m_tableView->viewport() || obj == m_listView->viewport() || obj == m_thumbnailView->viewport()) {
            if (obj == m_tableView->viewport()) {
                QTimer::singleShot(0, this, [this]() {
                    updateColumns();
                });
            }

            m_scrollToDebounceTimer->start(100);

            m_timerUpdateIcons->start(20);
        }
    }
    else if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonDblClick) {
        if (obj == m_listView->viewport() || obj == m_tableView->viewport() || obj == m_thumbnailView->viewport()) {
            auto *mouseEvent = static_cast<QMouseEvent*>(event);

            // Part 2/3 of mitigation for left click on focused item in inactive windows accidentally triggering rename
            if (mouseEvent->button() == Qt::LeftButton) {
                qint64 currentTime = QDateTime::currentMSecsSinceEpoch();

                // Case A (Windows/X11): Click received AFTER Qt processes activation.
                bool isActivatingClick = (currentTime - m_lastActivationTime < 200);

                // Case B (Wayland): Click received before BEFORE Qt processes activation.
                if (!isActiveWindow() || QApplication::activeWindow() != this) {
                    isActivatingClick = true;
                }

                if (isActivatingClick) {
                    m_activationClickActive = true;

                    // Bei ALLEN Views das "SelectedClicked" bitweise entfernen
                    m_tableView->setEditTriggers(m_tableView->editTriggers() & ~QAbstractItemView::SelectedClicked);
                    m_listView->setEditTriggers(m_listView->editTriggers() & ~QAbstractItemView::SelectedClicked);
                    m_thumbnailView->setEditTriggers(m_thumbnailView->editTriggers() & ~QAbstractItemView::SelectedClicked);
                }
            }
            else if (mouseEvent->button() == Qt::RightButton) {
                auto *targetView = qobject_cast<QAbstractItemView*>(obj->parent());
                QPoint pos = mouseEvent->pos();

                if (targetView) {
                    if (!targetView->indexAt(pos).isValid()) {
                        targetView->clearSelection();
                    }
                }

                if (!m_settings.menuOnMouseUp) {
                    // Use Lambda to trigger menu after button event has finished processing
                    // This is a workaround. Calling onShowContextMenu() directly would block the default function of focusing the item below the mouse cursor.
                    QTimer::singleShot(0, this, [this, targetView, pos]() {
                        onShowContextMenu(targetView, pos);
                    });
                }

                return false;
            }
            else if (mouseEvent->button() == Qt::MiddleButton) {
                navigateUp();
                return true;
            }
        }
    }
    else if (event->type() == QEvent::MouseButtonRelease) {
        if (obj == m_listView->viewport() || obj == m_tableView->viewport() || obj == m_thumbnailView->viewport()) {
            auto *mouseEvent = static_cast<QMouseEvent*>(event);

            // Part 3/3 of mitigation for left click on focused item in inactive windows accidentally triggering rename
            if (mouseEvent->button() == Qt::LeftButton && m_activationClickActive) {
                m_activationClickActive = false;

                QTimer::singleShot(0, this, [this]() {
                    m_tableView->setEditTriggers(m_tableView->editTriggers() | QAbstractItemView::SelectedClicked);
                    m_listView->setEditTriggers(m_listView->editTriggers() | QAbstractItemView::SelectedClicked);
                    m_thumbnailView->setEditTriggers(m_thumbnailView->editTriggers() | QAbstractItemView::SelectedClicked);
                });
            }
            else if (mouseEvent->button() == Qt::RightButton && m_settings.menuOnMouseUp) {
                auto *targetView = qobject_cast<QAbstractItemView*>(obj->parent());
                onShowContextMenu(targetView, mouseEvent->pos());
                return true;
            }
        }
    }
    else if (event->type() == QEvent::KeyPress) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);

        QWidget *targetWidget = qobject_cast<QWidget*>(obj);
        if (!targetWidget || targetWidget->window() != this) {
            return QObject::eventFilter(obj, event); // Not our window -> hand off
        }

        //qDebug() << "[KeyPress pos 1] key:" << keyEvent->key() << "isAutoRepeat:" << keyEvent->isAutoRepeat() << "obj:" << obj << "targetWidget:" << targetWidget << "targetWidget->parent():" << targetWidget->parent();

        // Window-wide hotkeys
        if (keyEvent->modifiers() == (Qt::ControlModifier | Qt::ShiftModifier)) {
            if (keyEvent->key() == Qt::Key_F) {
                openFileListWithHandler(m_settings.searchTool, { m_currentDirectory });
                return true;
            }
        }
        else if (keyEvent->modifiers() == Qt::ControlModifier) {
            if (keyEvent->key() == Qt::Key_1) {
                action_ViewModeList();
                return true;
            }
            else if (keyEvent->key() == Qt::Key_2) {
                action_ViewModeDetails();
                return true;
            }
            else if (keyEvent->key() == Qt::Key_3) {
                action_ViewModeThumbs();
                return true;
            }
            else if (keyEvent->key() == Qt::Key_D) {
                duplicateInstance();
                return true;
            }
            else if (keyEvent->key() == Qt::Key_F) {
                if (m_LineEdit1->hasFocus()) {
                    m_LineEdit1->clear();
                    m_topControlsContainerWidget->hide();
                } else {
                    m_topControlsContainerWidget->show();
                    m_LineEdit1->setFocus();
                    m_LineEdit1->selectAll();
                }
                return true;
            }
            else if (keyEvent->key() == Qt::Key_H) {
                m_bShowHiddenFiles = !m_bShowHiddenFiles;
                m_proxyModel->setShowHiddenFiles(m_bShowHiddenFiles);
                return true;
            }
            else if (keyEvent->key() == Qt::Key_W) {
                elevateInstance();
                return true;
            }
        }
        else if (keyEvent->modifiers() == Qt::AltModifier) {
            if (keyEvent->key() == Qt::Key_Left) {
                navigateBack();
                return true;
            }
            else if (keyEvent->key() == Qt::Key_Right) {
                navigateForward();
                return true;
            }
        }


        // Wenn in der View gerade ein Editor offen ist (z.B. Dateiname umbenennen),
        // dürfen wir Tasten wie "Entf", "Backspace" oder "Enter" NICHT abfangen!
        if (qobject_cast<QLineEdit*>(targetWidget)) {
            if (targetWidget == m_LineEdit1 && keyEvent->key() == Qt::Key_Escape) {
                m_LineEdit1->clear();
                m_topControlsContainerWidget->hide();
                return true;
            }
            //if (keyEvent->key() == Qt::Key_Enter || keyEvent->key() == Qt::Key_Return) qDebug() << "[KeyPress pos 2a] key:" << keyEvent->key() << "isAutoRepeat:" << keyEvent->isAutoRepeat() << "obj:" << obj << "targetWidget:" << targetWidget << "targetWidget->parent():" << targetWidget->parent();

            return QObject::eventFilter(obj, event);
        }
        else if (qobject_cast<QLineEdit*>(targetWidget->parent())) {
            //qDebug() << "[KeyPress pos 2b] key:" << keyEvent->key() << "isAutoRepeat:" << keyEvent->isAutoRepeat() << "obj:" << obj << "targetWidget:" << targetWidget << "targetWidget->parent():" << targetWidget->parent();
            return QObject::eventFilter(obj, event);
        }
        else {
            //if (keyEvent->key() == Qt::Key_Enter || keyEvent->key() == Qt::Key_Return) qDebug() << "[KeyPress pos 2c] key:" << keyEvent->key() << "isAutoRepeat:" << keyEvent->isAutoRepeat() << "obj:" << obj << "targetWidget:" << targetWidget << "targetWidget->parent():" << targetWidget->parent();
        }

        if ((targetWidget == m_tableView || targetWidget->parent() == m_tableView) ||
            (targetWidget == m_listView  || targetWidget->parent() == m_listView)  ||
            (targetWidget == m_thumbnailView || targetWidget->parent() == m_thumbnailView)) {

            if (keyEvent->modifiers() == (Qt::ControlModifier | Qt::ShiftModifier)) {
                if (keyEvent->key() == Qt::Key_C) {
                    action_ListViewCopyPaths();
                    return true;
                }
            }
            else if (keyEvent->modifiers() == Qt::ControlModifier) {
                if (keyEvent->key() == Qt::Key_A) {
                    selectAllItems();
                    return true;
                }
                else if (keyEvent->key() == Qt::Key_C) {
                    action_ListViewCopyFiles();
                    return true;
                }
                else if (keyEvent->key() == Qt::Key_E) {
                    action_ListViewEditFiles();
                    return true;
                }
                else if (keyEvent->key() == Qt::Key_F) {
                    m_topControlsContainerWidget->show();
                    m_LineEdit1->setFocus();
                    m_LineEdit1->selectAll();
                    return true;
                }
                else if (keyEvent->key() == Qt::Key_I) {
                    action_ListViewFileProperties();
                    return true;
                }
                else if (keyEvent->key() == Qt::Key_N) {
                    action_ListViewNewFolder();
                    return true;
                }
                else if (keyEvent->key() == Qt::Key_P) {
                    navigateToClipboardPath();
                    return true;
                }
                else if (keyEvent->key() == Qt::Key_V) {
                    action_ListViewPasteFiles();
                    return true;
                }
                else if (keyEvent->key() == Qt::Key_X) {
                    action_ListViewCutFiles();
                    return true;
                }
            }
            else {
                if (keyEvent->key() == Qt::Key_F2) {
                    action_ListViewRenameFiles();
                    return true;
                } else if (keyEvent->key() == Qt::Key_F5) {
                    reloadDirectory();
                    return true;
                }
                else if (keyEvent->key() == Qt::Key_Escape) {
                    const QMimeData* mimeData = QApplication::clipboard()->mimeData();
                    if (mimeData->hasFormat(m_privateTokenName) && mimeData->data(m_privateTokenName) == m_currentClipboardToken) {
                        QApplication::clipboard()->clear();
                        return true;
                    }
                }
                else if (keyEvent->key() == Qt::Key_Backspace) {
                    navigateUp();
                    return true;
                }
                else if (keyEvent->key() == Qt::Key_Delete) {
                    if (keyEvent->modifiers() & Qt::ShiftModifier) {
                        action_ListViewDeleteFiles(false);
                    } else {
                        action_ListViewDeleteFiles(true); // to recycle bin
                    }
                    return true;
                }
                else if (keyEvent->key() == Qt::Key_Enter || keyEvent->key() == Qt::Key_Return) {
                    // Note: QLineEdit does not consume the enter/return key, so we'll get an echo of it here.
                    // Since the QLineEdit doesn't get deleted from memory right away (only in next event loop pass),
                    // it will still exist as child of the viewport, so we can check for it here.
                    if (targetWidget->findChild<QLineEdit*>() || (targetWidget->parentWidget() && targetWidget->parentWidget()->findChild<QLineEdit*>())) {
                        return true;
                    }

                    action_ListViewOpenFiles();
                    return true;
                }
            }

            if (targetWidget == m_tableView || targetWidget->parent() == m_tableView) {
                if (keyEvent->key() == Qt::Key_Home || keyEvent->key() == Qt::Key_End) {
                    if (m_tableView->model() && m_tableView->model()->rowCount() > 0) {

                        int targetRow = (keyEvent->key() == Qt::Key_Home) ? 0 : m_tableView->model()->rowCount() - 1;
                        int targetCol = m_tableView->currentIndex().isValid() ? m_tableView->currentIndex().column() : 0;

                        // Ignore hidden columns
                        if (m_tableView->isColumnHidden(targetCol)) {
                            for (int c = 0; c < m_tableView->model()->columnCount(); ++c) {
                                if (!m_tableView->isColumnHidden(c)) {
                                    targetCol = c;
                                    break;
                                }
                            }
                        }

                        QModelIndex targetIndex = m_tableView->model()->index(targetRow, targetCol);
                        if (targetIndex.isValid()) {

                            if (keyEvent->modifiers() & Qt::ShiftModifier) {
                                QVariant anchorVar = m_tableView->property("selectionAnchor");
                                QModelIndex anchorIndex;
                                if (anchorVar.isValid()) {
                                    anchorIndex = anchorVar.value<QModelIndex>();
                                }

                                if (!anchorIndex.isValid() || anchorIndex.model() != m_tableView->model()) {
                                    anchorIndex = m_tableView->currentIndex();
                                    if (!anchorIndex.isValid()) anchorIndex = m_tableView->model()->index(0, targetCol);
                                    m_tableView->setProperty("selectionAnchor", anchorIndex);
                                }

                                int startRow = anchorIndex.row();
                                QModelIndex topLeft = m_tableView->model()->index(qMin(startRow, targetRow), 0);
                                QModelIndex bottomRight = m_tableView->model()->index(qMax(startRow, targetRow), m_tableView->model()->columnCount() - 1);
                                QItemSelection selection(topLeft, bottomRight);

                                QItemSelectionModel::SelectionFlags flags = QItemSelectionModel::ClearAndSelect;
                                if (m_tableView->selectionBehavior() == QAbstractItemView::SelectRows) {
                                    flags |= QItemSelectionModel::Rows;
                                }

                                m_tableView->selectionModel()->select(selection, flags);
                                m_tableView->selectionModel()->setCurrentIndex(targetIndex, QItemSelectionModel::NoUpdate);
                            }
                            else {
                                m_tableView->setCurrentIndex(targetIndex);
                            }

                            m_tableView->scrollTo(targetIndex);
                        }
                    }
                    return true;
                }
            }
        }
    }

    return QObject::eventFilter(obj, event);
}


void MainWindow::action_ListViewNewFolder() {
    if (m_currentDirectory.isEmpty() || m_currentDirectory == "drives://") return;

    // 1. Create unique dummy name
    QDir dir(m_currentDirectory);
    QString baseName = tr("New Folder");
    QString folderName = baseName;
    int counter = 2;

    while (dir.exists(folderName)) {
        folderName = QString("%1 (%2)").arg(baseName).arg(counter++);
    }

    // Wir sagen dem Watcher-Slot, dass er das nächste Signal ignorieren soll
    m_ignoreNextWatcherSignal = true;

    // 2. Den Dummy-Ordner physisch auf der Festplatte erstellen
    if (!dir.mkdir(folderName)) {
        m_ignoreNextWatcherSignal = false;
        return;
    }

    auto *activeView = qobject_cast<QAbstractItemView*>(m_viewStack->currentWidget());
    if (!activeView) {
        // Fallback/Sicherheit, falls im Stack mal kein valides View-Widget liegt
        m_ignoreNextWatcherSignal = false;
        return;
    }

    // 3. Modell anweisen, das Verzeichnis neu zu laden
    m_abstractModel->populateModel_mkFolderWidget(m_currentDirectory);

    // 4. Den QModelIndex des neuen Ordners in der AKTIVEN View finden
    // WICHTIG: Wir holen das Modell der View, die der Nutzer gerade sieht!
    QAbstractItemModel *currentViewModel = activeView->model();
    if (!currentViewModel) return;

    QModelIndex targetIndex;
    QModelIndexList matches = currentViewModel->match(currentViewModel->index(0, 0), Qt::EditRole, folderName, 1, Qt::MatchExactly | Qt::MatchWrap);

    if (!matches.isEmpty()) {
        targetIndex = matches.first();
    }

    // 5. Die AKTIVE View auf den neuen Ordner ansetzen und das Inline-Editing starten
    if (targetIndex.isValid()) {
        activeView->scrollTo(targetIndex, QAbstractItemView::EnsureVisible);
        activeView->setCurrentIndex(targetIndex);

        // Öffnet die Edit-Box in der View, in der der Nutzer sich gerade befindet
        activeView->edit(targetIndex);
    }
}

void MainWindow::action_ListViewNewTextFile() {
    if (m_currentDirectory.isEmpty() || m_currentDirectory == "drives://") return;

    QString baseName = tr("New Text Document");
    QString extension = ".txt";

    QDir dir(m_currentDirectory);
    QString finalFileName = baseName + extension;
    int counter = 2;

    while (dir.exists(finalFileName)) {
        finalFileName = QString("%1 (%2)%3").arg(baseName).arg(counter).arg(extension);
        counter++;
    }

    QString fullPath = dir.filePath(finalFileName);

    m_ignoreNextWatcherSignal = true;

    QFile file(fullPath);
    if (file.open(QIODevice::WriteOnly)) {
#ifdef Q_OS_WIN
        file.write("\xEF\xBB\xBF", 3);  // UTF-8 BOM only on Windows
#endif
        file.close();
    } else {
        qWarning() << "Konnte Textdatei nicht erstellen:" << file.errorString();
        m_ignoreNextWatcherSignal = false;
        return;
    }

    auto *activeView = qobject_cast<QAbstractItemView*>(m_viewStack->currentWidget());
    if (!activeView) {
        m_ignoreNextWatcherSignal = false;
        return;
    }

    m_abstractModel->populateModel_mkFolderWidget(m_currentDirectory);

    QAbstractItemModel *currentViewModel = activeView->model();
    if (!currentViewModel) return;

    QModelIndex targetIndex;
    QModelIndexList matches = currentViewModel->match(currentViewModel->index(0, 0), Qt::EditRole, finalFileName, 1, Qt::MatchExactly | Qt::MatchWrap);

    if (!matches.isEmpty()) {
        targetIndex = matches.first();
    }

    if (targetIndex.isValid()) {
        activeView->scrollTo(targetIndex, QAbstractItemView::EnsureVisible);
        activeView->setCurrentIndex(targetIndex);
        activeView->edit(targetIndex);
    }
}

void MainWindow::navigateToClipboardPath() {
    QClipboard *clipboard = QApplication::clipboard();
    if (!clipboard) return;

    QString text = clipboard->text().trimmed();
    if (text.isEmpty()) return;

    if (text.startsWith('"') && text.endsWith('"')) {
        text = text.mid(1, text.length() - 2);
    }

    if (text.startsWith("file:///")) {
        text = QUrl(text).toLocalFile();
    }

    QFileInfo fileInfo(text);
    if (!fileInfo.exists()) return;

    if (fileInfo.isDir()) {
        browseFolder(fileInfo.absoluteFilePath(), QString());
    } else {
        browseFolder(fileInfo.absolutePath(), fileInfo.absoluteFilePath());
    }

    return;
}

void MainWindow::duplicateInstance() {
    QString appPath = QCoreApplication::applicationFilePath();
    QStringList arguments;
    arguments << "-X" << QString::number(this->x() + 40)
              << "-Y" << QString::number(this->y() + 40)
              << "-W" << QString::number(this->width())
              << "-H" << QString::number(this->height())
              << "-p" << m_currentDirectory;

    QString focusedFile = getActiveViewCurrentItemPath();
    if (!focusedFile.isEmpty()) {
        arguments << "-f" << focusedFile;
    }

    QProcess::startDetached(appPath, arguments);
}

void MainWindow::elevateInstance() {
    if (isCurrentProcessElevated()) return;

    QString appPath = QCoreApplication::applicationFilePath();
    QStringList arguments;
    arguments << "-X" << QString::number(this->x() + 40)
              << "-Y" << QString::number(this->y() + 40)
              << "-W" << QString::number(this->width())
              << "-H" << QString::number(this->height())
              << "-p" << m_currentDirectory;

    QString focusedFile = getActiveViewCurrentItemPath();
    if (!focusedFile.isEmpty()) {
        arguments << "-f" << focusedFile;
    }

    bool bSuccess = false;
#ifdef Q_OS_WIN
    bSuccess = startProcessElevatedWin(appPath, argumentsToWinString(arguments));
#elif defined(Q_OS_LINUX)
    QStringList pkexecArgs;
    pkexecArgs << "env"
               // Wir holen die Display-Infos des aktuellen Nutzers und übergeben sie an Root
               << QString("DISPLAY=%1").arg(QString::fromLocal8Bit(qgetenv("DISPLAY")))
               << QString("XAUTHORITY=%1").arg(QString::fromLocal8Bit(qgetenv("XAUTHORITY")))
               // WICHTIG: Zwingt Qt zu X11/XWayland, da Wayland Root-GUIs blockiert
               << "QT_QPA_PLATFORM=xcb"
               // Erst JETZT folgt deine App und deine Argumente
               << appPath
               << arguments;
    qint64 pid;
    bSuccess = QProcess::startDetached("pkexec", pkexecArgs, QString(), &pid);
#endif
    if (bSuccess) {
        QMetaObject::invokeMethod(qApp, "closeAllWindows", Qt::QueuedConnection);
    }
}

void MainWindow::navigateUp() {
    if (m_currentDirectory.isEmpty() || m_currentDirectory == "drives://") return;

    QDir currentDir(m_currentDirectory);
    if (currentDir.isRoot()) {
#ifdef Q_OS_WIN
        QString focusPath = currentDir.absolutePath();
        browseFolder("drives://", focusPath);
#endif
    } else {
        QString focusPath = currentDir.absolutePath();
        currentDir.cdUp();
        QString parentPath = currentDir.absolutePath();

        browseFolder(parentPath, focusPath);
    }
}

void MainWindow::navigateBack() {
    if (m_backHistory.isEmpty()) return;

    // Letzten Pfad aus dem Verlauf holen und entfernen
    QString targetPath = m_backHistory.takeLast();

    // Aktuellen Pfad in den Vorwärts-Verlauf schieben
    if (!m_currentDirectory.isEmpty()) {
        m_forwardHistory.append(m_currentDirectory);
    }

    browseFolder(targetPath, QString(), true);
}

void MainWindow::navigateForward() {
    if (m_forwardHistory.isEmpty()) return;

    // Letzten Pfad aus dem Vorwärts-Verlauf holen
    QString targetPath = m_forwardHistory.takeLast();

    // Aktuellen Pfad wieder zurück in den normalen Verlauf
    if (!m_currentDirectory.isEmpty()) {
        m_backHistory.append(m_currentDirectory);
    }

    browseFolder(targetPath, QString(), true);
}

