#include "mainwindow.h"
#include "customitemdelegates.h"
#include "customproxystyle.h"
#include "filepropertiesdialog.h"
#include "stylesheets.h"

#include <QActionGroup>
#include <QApplication>
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
#include <QKeyEvent>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMimeDatabase>
#include <QMimeType>
#include <QObject>
#include <QProcess>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSettings>
#include <QShortcut>
#include <QSize>
#include <QStandardPaths>
#include <QStringList>
#include <QStringView>
#include <QStyleHints>
#include <QThread>
#include <QUrl>

#include <algorithm>
#include <utility> // For std::as_const

#include <KFileItem>
#include <KFileItemListProperties>
#include <KFileItemActions>
#include <KApplicationTrader>
#include <KService>
#include <KIO/ApplicationLauncherJob>
#include <KOpenWithDialog>


MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    // Note on other flags:
    //    Qt::Tool  Advantage: no taskbar icon. Problem: Window can't be minimized. Window can't be hidden.
    setWindowFlags(windowFlags() | Qt::FramelessWindowHint);

    setWindowTitle("mkLauncher");
    setWindowIcon(QIcon(":/icons/app.ico"));

    m_processIsElevated = Helpers::isCurrentProcessElevated();

    loadHistory();

    m_centralWidget = new QWidget(this);
    setCentralWidget(m_centralWidget);
    m_centralWidget->setMinimumWidth(745);

    m_mainLayout = new QVBoxLayout(m_centralWidget);
    m_mainLayout->setContentsMargins(3, 3, 3, 3);     // margins: distance between edge of layout box to contents
    m_mainLayout->setSpacing(3);                      // spacing: distance between elements within layout

    m_tableLineHeight = 18;

    // --------------------------------------------------------------------
    // Container for editbox controls

    m_topControlsContainerWidget = new QWidget();
    m_topControlsHBoxLayout = new QHBoxLayout(m_topControlsContainerWidget);
    m_topControlsHBoxLayout->setContentsMargins(7, 5, 7, 5);
    m_topControlsHBoxLayout->setSpacing(1);

    /*
    NOTE:
        padding: distance from text to border
        margin: distance from border to other elements in layout
    */
    m_LineEdit1 = new QLineEdit();
    m_topControlsHBoxLayout->addWidget(m_LineEdit1, 8); // second parameter is stretch factor

    m_LineEdit2 = new QLineEdit();
    m_topControlsHBoxLayout->addWidget(m_LineEdit2, 1);

    if (m_settings.showPlaceholderText == true) {
        m_LineEdit1->setPlaceholderText(tr("(search terms)"));
        m_LineEdit2->setPlaceholderText(tr("(action)"));
    }

    m_mainLayout->addWidget(m_topControlsContainerWidget);

    // --------------------------------------------------------------------

    m_abstractModel = new CustomTableModel(&m_settings, 7, this);   // mkLauncher uses 7 columns: Name, NewName (hidden), Path, Size, Changed, Type, Rating [unused: Count, CRC]

    // --------------------------------------------------------------------

    m_proxyModel = new FileSortProxyModel(this);
    m_proxyModel->setSourceModel(m_abstractModel);
    m_proxyModel->setDynamicSortFilter(true);

    // --------------------------------------------------------------------

    m_tableView = new CustomTableView(this);
    m_tableView->setModel(m_proxyModel);
    m_tableView->setSortingEnabled(true);   // Unlike with TableWidget, this doesn't need turning on and off for speed optimization.
    m_proxyModel->sort(CustomTableModel::eColName, Qt::AscendingOrder);

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
    m_tableView->setColumnHidden(CustomTableModel::eColNewName, true);

    m_tableView->verticalHeader()->setVisible(false);
    m_tableView->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    m_tableView->verticalHeader()->setMinimumSectionSize(0);
    m_tableView->verticalHeader()->setDefaultSectionSize(m_tableLineHeight);

    m_tableView->horizontalHeader()->setSortIndicator(CustomTableModel::eColName, Qt::AscendingOrder);
    m_tableView->horizontalHeader()->setSectionsMovable(true);
    m_tableView->horizontalHeader()->setHighlightSections(false);
    m_tableView->horizontalHeader()->setFixedHeight(22);

    m_tableView->setDragEnabled(true);
    m_tableView->setAcceptDrops(true);
    m_tableView->setDropIndicatorShown(true);
    m_tableView->setDragDropMode(QAbstractItemView::DragDrop);
    m_tableView->setDefaultDropAction(Qt::MoveAction);

    // --------------------------------------------------------------------

    m_listView = new CustomListView(this);
    m_listView->setModel(m_proxyModel);
    ListItemDelegate *listItemDelegate = new ListItemDelegate(m_tableView);
    m_listView->setThumbnailMode(false); // custom function!
    m_listView->setItemDelegate(listItemDelegate);
    CustomProxyStyle *listViewStyle = new CustomProxyStyle();
    listViewStyle->setSquareItemViewPanels(true); // no rounded hover/selection/focus rects in the list view
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

    // We take the selection model of the table and force the list to use the same one!
    m_selectionModel = m_tableView->selectionModel();
    m_listView->setSelectionModel(m_selectionModel);

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

    // --------------------------------------------------------------------

    m_bHeaderVisible = false;
    updateColumns();   // will hide the header for now and update column sizes
    m_listView->setMinimumHeight(m_tableLineHeight * 14);
    m_tableView->setMinimumHeight(m_tableLineHeight * 14);
    m_thumbnailView->setMinimumHeight(m_tableLineHeight * 14);

    m_viewStack = new QStackedWidget(this);
    m_viewStack->addWidget(m_listView);
    m_viewStack->addWidget(m_tableView);
    m_viewStack->addWidget(m_thumbnailView);
    m_mainLayout->addWidget(m_viewStack);

    m_viewStack->setCurrentIndex(1);
    m_abstractModel->setModelViewMode(ViewMode::Detail);

    if (m_settings.allowLauncherShrink) {
        m_viewStack->hide();
    }

    m_mainLayout->activate();

    // --------------------------------------------------------------------

    updateWidgetStyles();

    // --------------------------------------------------------------------
    // Shortcuts: Whole Window

    QShortcut *WindowShortcutN = new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Comma), this);
    WindowShortcutN->setContext(Qt::WindowShortcut);
    connect(WindowShortcutN, &QShortcut::activated, this, &MainWindow::action_EditSettingsFile);

    QShortcut *WindowShortcutH = new QShortcut(QKeySequence("Ctrl+H"), this);
    WindowShortcutH->setContext(Qt::WindowShortcut);
    connect(WindowShortcutH, &QShortcut::activated, this, &MainWindow::onToggleListViewHeader);

    QShortcut *WindowShortcutCtrlBackSpace = new QShortcut(QKeySequence("Ctrl+Backspace"), this);
    WindowShortcutCtrlBackSpace->setContext(Qt::WindowShortcut);
    connect(WindowShortcutCtrlBackSpace, &QShortcut::activated, this, [this]() { m_abstractModel->clear(); m_LineEdit2->clear(); m_LineEdit1->clear(); m_LineEdit1->setFocus(); });

    // --------------------------------------------------------------------
    // Context menu Actions

    m_actionListViewOpenFiles = new QAction(tr("Open"), this);
    connect(m_actionListViewOpenFiles, &QAction::triggered, this, &MainWindow::action_ListViewOpenFiles);

    m_actionListViewRun = new QAction(tr("Run"), this);
    m_actionListViewRun->setIcon(QIcon::fromTheme("system-run"));
    connect(m_actionListViewRun, &QAction::triggered, this, &MainWindow::action_ListViewRun);

    m_actionListViewRunInTerminal = new QAction(tr("Run in Terminal"), this);
    m_actionListViewRunInTerminal->setIcon(QIcon::fromTheme("utilities-terminal"));
    connect(m_actionListViewRunInTerminal, &QAction::triggered, this, &MainWindow::action_ListViewRunInTerminal);

    m_actionListViewBrowseToFile = new QAction(tr("Show in folder"),this);
    m_actionListViewBrowseToFile->setIcon(QIcon::fromTheme("folder-open"));
    m_actionListViewBrowseToFile->setShortcut(QKeySequence("Ctrl+L"));
    connect(m_actionListViewBrowseToFile, &QAction::triggered, this, &MainWindow::action_ListViewBrowseToFile);

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

    m_actionViewModeRefresh = new QAction(tr("Refresh"),this);
    m_actionViewModeRefresh->setShortcut(QKeySequence("F5"));
    connect(m_actionViewModeRefresh, &QAction::triggered, this, &MainWindow::updateSearch);

    //-----------------------------------------------------------

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
        m_actionListViewRun->setIconVisibleInMenu(false);
        m_actionListViewRunInTerminal->setIconVisibleInMenu(false);
        m_actionListViewBrowseToFile->setIconVisibleInMenu(false);
        m_actionListViewCopyPaths->setIconVisibleInMenu(false);
        m_actionListViewCutFiles->setIconVisibleInMenu(false);
        m_actionListViewCopyFiles->setIconVisibleInMenu(false);
        m_actionListViewDeleteFiles->setIconVisibleInMenu(false);
        m_actionListViewRenameFiles->setIconVisibleInMenu(false);
        m_actionListViewFileProperties->setIconVisibleInMenu(false);
        m_actionViewModeList->setIconVisibleInMenu(false);
        m_actionViewModeDetails->setIconVisibleInMenu(false);
        m_actionViewModeThumbs->setIconVisibleInMenu(false);
    }

    if (m_settings.showShortcutsInMenu == false) {
        m_actionListViewOpenFiles->setShortcutVisibleInContextMenu(false);
        m_actionListViewBrowseToFile->setShortcutVisibleInContextMenu(false);
        m_actionListViewCopyPaths->setShortcutVisibleInContextMenu(false);
        m_actionListViewCutFiles->setShortcutVisibleInContextMenu(false);
        m_actionListViewCopyFiles->setShortcutVisibleInContextMenu(false);
        m_actionListViewDeleteFiles->setShortcutVisibleInContextMenu(false);
        m_actionListViewRenameFiles->setShortcutVisibleInContextMenu(false);
        m_actionListViewFileProperties->setShortcutVisibleInContextMenu(false);
        m_actionViewModeList->setShortcutVisibleInContextMenu(false);
        m_actionViewModeDetails->setShortcutVisibleInContextMenu(false);
        m_actionViewModeThumbs->setShortcutVisibleInContextMenu(false);
        m_actionViewModeRefresh->setShortcutVisibleInContextMenu(false);
    }

    // --------------------------------------------------------------------

    if (QDBusConnection::sessionBus().registerService("org.mkLauncher")) {
        QDBusConnection::sessionBus().registerObject("/Main", this, QDBusConnection::ExportScriptableSlots | QDBusConnection::ExportScriptableSignals | QDBusConnection::ExportScriptableProperties);
    }

    // --------------------------------------------------------------------

    // Note: any single event of any widget will first flow through this filter before reaching the target widget
    qApp->installEventFilter(this);

    // --------------------------------------------------------------------

    m_timerUpdateIcons = new QTimer(this);
    m_timerUpdateIcons->setSingleShot(true);
    connect(m_timerUpdateIcons, &QTimer::timeout, this, &MainWindow::onTimedUpdateIcons);

    m_scrollToDebounceTimer = new QTimer(this);
    m_scrollToDebounceTimer->setSingleShot(true);
    connect(m_scrollToDebounceTimer, &QTimer::timeout, this, &MainWindow::scrollToCurrentItem);

    m_themeUpdateDebounceTimer = new QTimer(this);
    m_themeUpdateDebounceTimer->setSingleShot(true);
    connect(m_themeUpdateDebounceTimer, &QTimer::timeout, this, &MainWindow::updateWidgetStyles);

    // --------------------------------------------------------------------

    connect(m_tableView, &QTableView::doubleClicked, this, &MainWindow::onListItemDoubleClicked);
    connect(m_listView, &QListView::doubleClicked, this, &MainWindow::onListItemDoubleClicked);
    connect(m_thumbnailView, &QListView::doubleClicked, this, &MainWindow::onListItemDoubleClicked);

    connect(m_tableView->verticalScrollBar(), &QScrollBar::valueChanged, this, &MainWindow::onVerticalBarScrollChange);
    connect(m_listView->horizontalScrollBar(), &QScrollBar::valueChanged, this, &MainWindow::onHorizontalBarScrollChange);
    connect(m_thumbnailView->verticalScrollBar(), &QScrollBar::valueChanged, this, &MainWindow::onVerticalBarScrollChange);

    connect(m_tableView->horizontalHeader(), &QHeaderView::sectionClicked, this, &MainWindow::onListViewHeaderClicked);
    connect(QApplication::clipboard(), &QClipboard::dataChanged, this, &MainWindow::onClipboardChanged);

    connect(m_LineEdit1, &QLineEdit::textChanged, this, &MainWindow::handleTextChange);

    // part of mitigation for Shift+Pos1 / Shift+End not working in tableView
    connect(m_tableView->selectionModel(), &QItemSelectionModel::currentChanged, this, &MainWindow::onTableCurrentChanged);

    connect(m_abstractModel, &CustomTableModel::searchFinished, this, &MainWindow::onSearchFinished);

}

MainWindow::~MainWindow() = default;

void MainWindow::onTableCurrentChanged(const QModelIndex &current, const QModelIndex &previous) {
    Q_UNUSED(previous);

    // The anchor follows the focus only while SHIFT is NOT pressed.
    // This catches mouse clicks, arrow keys etc. perfectly and without a timer!
    if (!(QApplication::keyboardModifiers() & Qt::ShiftModifier) && current.isValid()) {
        m_tableView->setProperty("selectionAnchor", current);
    }
}

void MainWindow::onVerticalBarScrollChange() {
    m_timerUpdateIcons->start(20);
}

void MainWindow::onHorizontalBarScrollChange() {
    m_timerUpdateIcons->start(20);
}

void MainWindow::onListViewHeaderClicked() {
    m_timerUpdateIcons->start(20);
}

void MainWindow::onToggleListViewHeader() {
    m_bHeaderVisible = !m_bHeaderVisible;
    updateColumns();
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

void MainWindow::handleTextChange(const QString &text) {
    if (m_bSearchActive.load()) {
        m_abstractModel->abort();
    } else {
        startSearch();
    }
}

void MainWindow::startSearch() {
    QString InputBox1Text = m_LineEdit1->text();

    if (InputBox1Text.trimmed().isEmpty()) {
        m_abstractModel->clear();

        if (m_settings.allowLauncherShrink) {
            if (!m_viewStack->isHidden()) {
                m_viewStack->hide();
                m_mainLayout->activate();
                this->adjustSize();
                this->resize(this->width(), this->layout()->minimumSize().height());
            }
        }

        m_bSearchActive.store(false);
        return;
    }

    m_bSearchActive.store(true);
    m_BenchmarkTimer.start();

    if (m_selectionModel) {
        m_selectionModel->clear();
    }

    m_abstractModel->populateModel_mkLauncher(m_settings.searchFolders, InputBox1Text, m_recentOpenList);
}

void MainWindow::onSearchFinished(uint iItemsFound, uint iNameMatched, uint iContentMatched, bool bSearchInterrupted) {
    qDebug() << "finalizeUI() entry point. m_BenchmarkTimer:" << m_BenchmarkTimer.elapsed() << " ms elapsed since start of search.  bSearchInterrupted =" << bSearchInterrupted;

    if (bSearchInterrupted == true) {
        m_bSearchActive.store(false);

        if (bSearchInterrupted == true) {
            QTimer::singleShot(0, this, [this]() {
                startSearch();
            });
        }

        return;
    }

    m_tableView->setRootIndex(QModelIndex());
    m_listView->setRootIndex(QModelIndex());
    m_thumbnailView->setRootIndex(QModelIndex());

    if (iNameMatched == 0) {
        m_abstractModel->clear();

        if (m_settings.allowLauncherShrink) {
            if (!m_viewStack->isHidden()) {
                m_viewStack->hide();
                m_mainLayout->activate();
                this->adjustSize();
                this->resize(this->width(), this->layout()->minimumSize().height());
            }
        }

        m_bSearchActive.store(false);
        return;
    } else {
        m_tableView->sortByColumn(CustomTableModel::eColQuality, Qt::AscendingOrder);
    }

    if (m_viewStack->isHidden()) {
        m_viewStack->show();
        m_mainLayout->activate();
        this->adjustSize();
        this->resize(this->width(), this->layout()->minimumSize().height());
    }

    if (m_viewStack->currentWidget() == m_tableView) {
        updateColumns();
    }

    if (iNameMatched > 0) {
        auto *activeView = qobject_cast<QAbstractItemView*>(m_viewStack->currentWidget());
        if (activeView) {
            QModelIndex firstProxyIndex = m_proxyModel->index(0, 0);
            activeView->setCurrentIndex(firstProxyIndex);
        }
    }

    m_bSearchActive.store(false);

    qDebug() << "finalizeUI() exit point. m_BenchmarkTimer:" << m_BenchmarkTimer.elapsed() << " ms elapsed since start of search. Items:" << iNameMatched;
    m_timerUpdateIcons->start(20);
}

void MainWindow::updateColumns() {
    if (m_bHeaderVisible) {
        m_tableView->horizontalHeader()->setVisible(true);
        
        m_tableView->setColumnHidden(CustomTableModel::eColSize, false);
        m_tableView->setColumnHidden(CustomTableModel::eColDate, false);
        m_tableView->setColumnHidden(CustomTableModel::eColType, false);
        m_tableView->setColumnHidden(CustomTableModel::eColQuality, false);

        m_tableView->horizontalHeader()->setSectionResizeMode(CustomTableModel::eColSize, QHeaderView::ResizeToContents);
        m_tableView->horizontalHeader()->setSectionResizeMode(CustomTableModel::eColDate, QHeaderView::ResizeToContents);
        m_tableView->horizontalHeader()->setSectionResizeMode(CustomTableModel::eColType, QHeaderView::ResizeToContents);
        m_tableView->horizontalHeader()->setSectionResizeMode(CustomTableModel::eColQuality, QHeaderView::ResizeToContents);

        m_tableView->horizontalHeader()->setSectionResizeMode(CustomTableModel::eColName, QHeaderView::Stretch);
        m_tableView->horizontalHeader()->setSectionResizeMode(CustomTableModel::eColPath, QHeaderView::Stretch);

        m_tableView->horizontalHeader()->doItemsLayout();

        int eColNameWidth = m_tableView->columnWidth(CustomTableModel::eColName);
        int eColPathWidth = m_tableView->columnWidth(CustomTableModel::eColPath);

        m_tableView->horizontalHeader()->setSectionResizeMode(CustomTableModel::eColName, QHeaderView::Interactive);
        m_tableView->horizontalHeader()->setSectionResizeMode(CustomTableModel::eColPath, QHeaderView::Interactive);

        m_tableView->setColumnWidth(CustomTableModel::eColName, eColNameWidth);
        m_tableView->setColumnWidth(CustomTableModel::eColPath, eColPathWidth);
    } else {
        m_tableView->horizontalHeader()->setVisible(false);

        m_tableView->setColumnHidden(CustomTableModel::eColSize, true);
        m_tableView->setColumnHidden(CustomTableModel::eColDate, true);
        m_tableView->setColumnHidden(CustomTableModel::eColType, true);
        m_tableView->setColumnHidden(CustomTableModel::eColQuality, true);

        m_tableView->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

        m_tableView->horizontalHeader()->doItemsLayout();

        int eColNameWidth = m_tableView->columnWidth(CustomTableModel::eColName);
        int eColPathWidth = m_tableView->columnWidth(CustomTableModel::eColPath);

        m_tableView->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);

        m_tableView->setColumnWidth(CustomTableModel::eColName, eColNameWidth);
        m_tableView->setColumnWidth(CustomTableModel::eColPath, eColPathWidth);
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
        subMenuView->addSeparator();
        subMenuView->addAction(m_actionViewModeRefresh);

        mainMenu.addSeparator(); //-----------------------------------------

        int currentColumn = m_proxyModel->sortColumn();

        if      (currentColumn == CustomTableModel::eColName) m_actionSortByName->setChecked(true);
        else if (currentColumn == CustomTableModel::eColSize) m_actionSortBySize->setChecked(true);
        else if (currentColumn == CustomTableModel::eColDate) m_actionSortByDate->setChecked(true);
        else if (currentColumn == CustomTableModel::eColType) m_actionSortByType->setChecked(true);

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
    } else {
        QStringList selectedPaths = getActiveViewPathList(); // Used by KDE SERVICE-ACTIONS

        const KService::Ptr openService = updateOpenActionText(selectedPaths);
        if (Helpers::isSingleExecutable(selectedPaths)) {
            // Single executable file (program, script): run it, optionally in a terminal window to interact with it.
            // Like in Dolphin, opening it with an application is offered in the "Open with" menu.
            mainMenu.addAction(m_actionListViewRun);
            mainMenu.addAction(m_actionListViewRunInTerminal);
            mainMenu.setDefaultAction(m_actionListViewRun);
        } else {
            mainMenu.addAction(m_actionListViewOpenFiles);
            mainMenu.setDefaultAction(m_actionListViewOpenFiles);
        }

        // Native KDE Open with menu
        mainMenu.addSeparator(); //-----------------------------------------

        if (!selectedPaths.isEmpty()) {
            // 1.+2. Applications that can open all selected files (intersection over their MIME types)
            int mimeTypeCount = 0;
            KService::List commonServices = Helpers::commonServicesForPaths(selectedPaths, &mimeTypeCount);

            // 3. The application of the "Open" action is not repeated in the submenu (like in Dolphin)
            const bool showOtherApp = (mimeTypeCount == 1) || !commonServices.isEmpty();
            if (openService) {
                commonServices.removeIf([&openService](const KService::Ptr &service) {
                    return service->storageId() == openService->storageId();
                });
            }

            auto addOtherAppAction = [this, selectedPaths](QMenu *menu, const QString &text) {
                QAction *otherAppAction = menu->addAction(QIcon::fromTheme("system-run"), text);
                connect(otherAppAction, &QAction::triggered, this, [selectedPaths, this]() {
                    QList<QUrl> urls;
                    urls.reserve(selectedPaths.size());
                    for (const QString &path : selectedPaths) {
                        urls.append(QUrl::fromLocalFile(path));
                    }

                    auto *dialog = new KOpenWithDialog(urls, this);
                    dialog->show();
                });
            };

            if (!commonServices.isEmpty()) {
                // 4. Create the "Open with" submenu and add the remaining applications (intersection)
                QMenu *openWithMenu = mainMenu.addMenu(tr("Open with"));
                if (m_settings.showIconsInMenu) {
                    openWithMenu->setIcon(QIcon::fromTheme("system-run"));
                }

                for (const KService::Ptr &service : std::as_const(commonServices)) {
                    QAction *action = openWithMenu->addAction(QIcon::fromTheme(service->icon()), service->name());

                    connect(action, &QAction::triggered, this, [service, selectedPaths]() {
                        QList<QUrl> urls;
                        urls.reserve(selectedPaths.size());
                        for (const QString &path : selectedPaths) {
                            urls.append(QUrl::fromLocalFile(path));
                        }

                        auto *job = new KIO::ApplicationLauncherJob(service);
                        job->setUrls(urls);
                        job->start();
                    });
                }

                // 5. Add "Other Application..."
                openWithMenu->addSeparator();
                addOtherAppAction(openWithMenu, tr("Other Application..."));
            } else if (showOtherApp) {
                // No further applications: offer the dialog directly instead of a submenu with a single entry
                addOtherAppAction(&mainMenu, tr("Open with..."));
            }
        }
        mainMenu.addSeparator(); //-----------------------------------------
        mainMenu.addAction(m_actionListViewBrowseToFile);
        mainMenu.addAction(m_actionListViewCopyPaths);
        mainMenu.addAction(m_actionListViewCutFiles);
        mainMenu.addAction(m_actionListViewCopyFiles);
        mainMenu.addAction(m_actionListViewRenameFiles);
        mainMenu.addSeparator(); //-----------------------------------------
        mainMenu.addAction(m_actionListViewDeleteFiles);
        // KDE SERVICE-ACTIONS
        if (!selectedPaths.isEmpty()) {
            KFileItemList fileItemList;
            fileItemList.reserve(selectedPaths.size());
            for (const QString &path : std::as_const(selectedPaths)) {
                fileItemList.append(KFileItem(QUrl::fromLocalFile(path)));
            }

            KFileItemListProperties itemProperties(fileItemList);

            // Create KFileItemActions with &mainMenu as parent
            auto *serviceActions = new KFileItemActions(&mainMenu);
            serviceActions->setParentWidget(this);
            serviceActions->setItemListProperties(itemProperties);

            mainMenu.addSeparator(); //-----------------------------------------
            serviceActions->addActionsTo(&mainMenu);
        }
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
        if (proxyIndex.column() == CustomTableModel::eColName) {
            QModelIndex sourceIndex = m_proxyModel->mapToSource(proxyIndex);
            QString path = m_abstractModel->filePath(sourceIndex);
            if (!path.isEmpty()) {
                pathList << path;
            }
        }
    }

    return pathList;
}

void MainWindow::onListItemDoubleClicked(const QModelIndex &proxyIndex) {
    // We put this in a timer, so we can finish processing this double click event before the folder change fires.
    // This is to prevent the glitch that the focused file in the new folder is set in name edit mode if our mouse happens to hover over it.
    QTimer::singleShot(0, this, [this]() {
        action_ListViewOpenFiles();
    });
}

// Middle click (on release) on an item, modeled after Dolphin:
// - Folder: open it in the configured file manager (mkFolderWidget by default)
// - File: open it with the 2nd preferred application, Shift + middle click with the 3rd
void MainWindow::onListItemMiddleClicked(const QModelIndex &proxyIndex, Qt::KeyboardModifiers modifiers) {
    const QString path = m_abstractModel->filePath(m_proxyModel->mapToSource(proxyIndex));
    if (path.isEmpty()) return;

    QFileInfo fileInfo(path);
    if (fileInfo.isDir()) {
        Helpers::browseToFile(fileInfo.absoluteFilePath(), m_settings.fileManager);
        RecentOpenList_Add(fileInfo.absoluteFilePath());
        guiHideConditional();
        return;
    }
    if (!fileInfo.exists()) return;

    if (Helpers::openFileWithPreferredApp(fileInfo.absoluteFilePath(), (modifiers & Qt::ShiftModifier) ? 2 : 1)) {
        RecentOpenList_Add(fileInfo.absoluteFilePath());
        guiHideConditional();
    }
}

// Labels the "Open" action like Dolphin: "Open with <app>" if all selected files are opened with the same application.
// Returns that application, or nullptr.
KService::Ptr MainWindow::updateOpenActionText(const QStringList &pathList) {
    const KService::Ptr service = Helpers::openActionService(pathList);
    if (service) {
        m_actionListViewOpenFiles->setText(tr("Open with %1").arg(service->name().replace('&', QLatin1String("&&"))));
        m_actionListViewOpenFiles->setIcon(QIcon::fromTheme(service->icon()));
    } else {
        m_actionListViewOpenFiles->setText(tr("Open"));
        m_actionListViewOpenFiles->setIcon(QIcon());
    }
    return service;
}

void MainWindow::action_ListViewRun() {
    const QStringList pathList = getActiveViewPathList();
    if (!Helpers::isSingleExecutable(pathList)) return;

    RecentOpenList_Add(pathList.first());
    if (Helpers::executeFile(pathList.first(), this)) {
        guiHideConditional();
    }
}

void MainWindow::action_ListViewRunInTerminal() {
    const QStringList pathList = getActiveViewPathList();
    if (!Helpers::isSingleExecutable(pathList)) return;

    RecentOpenList_Add(pathList.first());
    if (Helpers::executeInTerminal(pathList.first(), this)) {
        guiHideConditional();
    }
}

void MainWindow::action_ListViewOpenFiles() {
    QStringList pathList = getActiveViewPathList();
    if (pathList.isEmpty()) return;

    if (pathList.size() == 1) {
        RecentOpenList_Add(pathList.first());
    }

    QList<QUrl> urlsToOpen;

    for (const QString &path : std::as_const(pathList)) {
        QFileInfo fileInfo(path);
        if (!fileInfo.exists()) continue;

        switch (Helpers::openMethod(fileInfo)) {
        case Helpers::OpenMethod::DesktopEntry:
            Helpers::launchDesktopFile(Helpers::getDesktopEntry(fileInfo));
            break;
        case Helpers::OpenMethod::Execute:
            // Like in Dolphin, only a single file is executed, several ones are opened with their preferred application
            if (pathList.size() == 1) {
                Helpers::executeFile(path, this);
            } else {
                urlsToOpen.append(QUrl::fromLocalFile(fileInfo.absoluteFilePath()));
            }
            break;
        case Helpers::OpenMethod::PreferredApp:
            urlsToOpen.append(QUrl::fromLocalFile(fileInfo.absoluteFilePath()));
            break;
        }
    }

    if (!urlsToOpen.isEmpty()) {
        Helpers::openUrlsWithKIO(urlsToOpen);
    }

    guiHideConditional();
}

void MainWindow::action_ListViewEditFiles() {
    QStringList pathList = getActiveViewPathList();
    if (pathList.isEmpty()) return;

    // Same applications as in the "Open with" submenu of the context menu: use the 2nd one (the 1st is the default
    // application used by "Open"), or the 1st one if there is no 2nd.
    const KService::List services = Helpers::commonServicesForPaths(pathList);
    if (services.isEmpty()) return;

    const KService::Ptr service = (services.size() >= 2) ? services.at(1) : services.first();
    // entryPath() is the absolute path of the .desktop file, which openFileListWithHandler() accepts directly
    Helpers::openFileListWithHandler(service->entryPath(), pathList);

    guiHideConditional();
}

void MainWindow::action_ListViewCopyPaths() {
    QStringList pathList = getActiveViewPathList();
    if (pathList.isEmpty()) return;

    QStringList pathListNative;
    for (const QString &path : std::as_const(pathList)) {
        pathListNative << path;
    }

    QString sClipboardList = pathListNative.join("\n");

    QGuiApplication::clipboard()->setText(sClipboardList);
}

void MainWindow::action_ListViewDeleteFiles(bool bRecycleOnly) {
    QStringList pathList = getActiveViewPathList();
    if (pathList.isEmpty()) {
        return;
    }

    if (!Helpers::showDeleteConfirmationDialog(pathList, bRecycleOnly, this)) {
        return;
    }

    // Special case: delete directly instead of handing over to mkTransactionHandler
    if (Helpers::hasOnlyFiles(pathList)) {
        QSet<QString> successfullyDeletedPaths;

        for (const QString &path : std::as_const(pathList)) {
            bool success = false;
            if (bRecycleOnly) {
                success = QFile::moveToTrash(path);
            } else {
                success = QFile::remove(path);
            }

            if (success) {
                successfullyDeletedPaths.insert(path);
            } else {
                qDebug() << "Could not delete:" << path;
            }
        }

        if (!successfullyDeletedPaths.isEmpty()) {
            m_abstractModel->removeFilePaths(successfullyDeletedPaths);
        }

        return;
    }

    QList<QUrl> urlFileList;
    for (const QString &path : std::as_const(pathList)) {
        if (!path.isEmpty()) {
            urlFileList << QUrl::fromLocalFile(path);
        }
    }

    if (bRecycleOnly) {
        Helpers::startFileOperation(OperationType::Recycle, urlFileList, "", false, m_currentDirectory, this);
    } else {
        Helpers::startFileOperation(OperationType::Delete, urlFileList, "", false, m_currentDirectory, this);
    }

	// Trigger update of the view
    startSearch();
}

void MainWindow::action_ListViewCutFiles() {
    QStringList pathList = getActiveViewPathList();
    if (pathList.isEmpty()) return;

    m_abstractModel->setCutMarkers(pathList);
    setupClipboardForCopyOrCut(pathList, true);
}

void MainWindow::action_ListViewCopyFiles() {
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

    // For Linux on GNOME-based Desktops (Nautilus, PCManFM, etc.)
    // Format: "cut" or "copy", then a line break, then all URLs (also separated by \n)
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

    Helpers::browseToFile(path, m_settings.fileManager);
    guiHideConditional();
}

void MainWindow::action_ListViewRenameFiles() {
    auto *activeView = qobject_cast<QAbstractItemView*>(m_viewStack->currentWidget());
    if (!activeView) return;

    const QStringList pathList = getActiveViewPathList();
    if (pathList.isEmpty()) return;

    if (pathList.size() > 1) {
        Helpers::launchRenameTool(QString(), pathList, m_settings.renameTool, this);
        return;
    }

    QModelIndex proxyIndex = activeView->currentIndex();
    if (!proxyIndex.isValid()) return;

    proxyIndex = proxyIndex.siblingAtColumn(CustomTableModel::eColName);

    activeView->setFocus();
    activeView->setCurrentIndex(proxyIndex);

    activeView->edit(proxyIndex);
}

void MainWindow::action_ListViewFileProperties() {
    QStringList pathList = getActiveViewPathList();
    if (pathList.isEmpty()) {
        if (m_currentDirectory.isEmpty()) {
            return;
        }
        pathList = { m_currentDirectory };
    }

    if (pathList.size() > 1) {
        Helpers::showKdePropertiesDialog(pathList, this);
        return;
    }

    auto *dialog = new FilePropertiesDialog(pathList);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
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
    m_proxyModel->sort(CustomTableModel::eColName, Qt::AscendingOrder);
    m_tableView->horizontalHeader()->setSortIndicator(CustomTableModel::eColName, Qt::AscendingOrder);
    m_timerUpdateIcons->start(20);
}

void MainWindow::action_SortBySize() {
    m_proxyModel->sort(CustomTableModel::eColSize, Qt::DescendingOrder);
    m_tableView->horizontalHeader()->setSortIndicator(CustomTableModel::eColSize, Qt::DescendingOrder);
    m_timerUpdateIcons->start(20);
}

void MainWindow::action_SortByDate() {
    m_proxyModel->sort(CustomTableModel::eColDate, Qt::DescendingOrder);
    m_tableView->horizontalHeader()->setSortIndicator(CustomTableModel::eColDate, Qt::DescendingOrder);
    m_timerUpdateIcons->start(20);
}

void MainWindow::action_SortByType() {
    m_proxyModel->sort(CustomTableModel::eColType, Qt::AscendingOrder);
    m_tableView->horizontalHeader()->setSortIndicator(CustomTableModel::eColType, Qt::AscendingOrder);
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
    Helpers::openWithSystemDefault(QUrl::fromLocalFile(m_settings.getSettingsPath()));
}

void MainWindow::onTimedUpdateIcons() {
    if (!m_proxyModel || m_proxyModel->rowCount() == 0) return;
    if (!m_abstractModel) return;

    // 1. Find out which view is currently visible
    auto *activeView = qobject_cast<QAbstractItemView*>(m_viewStack->currentWidget());
    if (!activeView) return;

    int firstVisible = 0;
    int lastVisible = m_proxyModel->rowCount() - 1;

    // --- VIEW-SPEZIFISCHE ERMITTLUNG DER SICHTBARKEIT ---

    if (auto *tableView = qobject_cast<QTableView*>(activeView)) {
        // Case A: Table (scrolls classically from top to bottom)
        firstVisible = tableView->rowAt(0);
        lastVisible = tableView->rowAt(tableView->viewport()->height() - 1);

        if (firstVisible == -1) firstVisible = 0;
        if (lastVisible == -1) lastVisible = m_proxyModel->rowCount() - 1;

    } else if (auto *listView = qobject_cast<QListView*>(activeView)) {
        // Case B: Your wrapping list (wraps at the bottom, new columns to the right)

        // 1. Determine the very first visible file at the top left
        QModelIndex firstIdx = listView->indexAt(QPoint(5, 5));
        firstVisible = firstIdx.isValid() ? firstIdx.row() : 0;

        QRect viewportRect = listView->viewport()->rect();
        lastVisible = firstVisible;

        // 2. We walk forward through the files starting at 'firstVisible'.
        // Since the files are laid out column by column from left to right,
        // we can stop the loop as soon as a file lies too far to the right!
        for (int i = firstVisible; i < m_proxyModel->rowCount(); ++i) {
            QModelIndex idx = m_proxyModel->index(i, 0);
            QRect itemRect = listView->visualRect(idx);

            // Check whether the file lies within the visible rectangle
            if (viewportRect.intersects(itemRect)) {
                lastVisible = i; // Found a valid visible file
            }
            // IMPORTANT EXIT: Is the left edge of the file already to the right, outside of the visible viewport?
            else if (itemRect.left() > viewportRect.right()) {
                break;
            }
        }
    }

    // Safety net for the indexes
    firstVisible = qMax(0, firstVisible);
    lastVisible  = qMin(m_proxyModel->rowCount() - 1, lastVisible);

    const int MAX_ICONS_PER_BATCH = 5; // Prevents the GUI from freezing
    int iconsProcessedInThisBatch = 0;
    bool processMoreLater = false;

    bool isThumbnailMode = (m_viewStack->currentWidget() == m_thumbnailView);

    // 3. Iterate only over the currently visible rows
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
            // Check whether the image exists in the cache AND is still up to date
            if (m_abstractModel->isThumbnailUpToDate(fullPath, fileInfo.lastModified())) {
                continue;
            }

            if (m_loadingThumbnails.contains(fullPath)) {
                continue;
            }

            if (iconsProcessedInThisBatch >= 2) { // Smaller limit for thumbnails!
                processMoreLater = true;
                break;
            }

            // Cache the supported formats statically (created only once on the very first call)
            static const QSet<QByteArray> supportedFormats = []() {
                auto formats = QImageReader::supportedImageFormats();
                return QSet<QByteArray>(formats.begin(), formats.end());
            }();

            if (supportedFormats.contains(fileInfo.suffix().toLower().toUtf8()) && !Helpers::hasIconExt(fileInfo)) {
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
            // Symlinks get a fixed icon directly from the model's general cache
            if (fileInfo.isSymbolicLink()) {
                continue;
            }

            // Check whether the icon exists in the cache AND is still up to date
            if (m_abstractModel->isPathIconUpToDate(fullPath, fileInfo.lastModified())) {
                continue;
            }

            // When we reach our batch limit for this frame, we stop and remember that we are not done yet!
            if (iconsProcessedInThisBatch >= MAX_ICONS_PER_BATCH) {
                processMoreLater = true;
                break;
            }

            // 5. Determine whether an individual icon is needed

            const bool needsTrueIcon = fileInfo.isDir() ||
                                       fileInfo.isExecutable() ||
                                       fullPath.endsWith(".desktop", Qt::CaseInsensitive);

            if (needsTrueIcon) {
                QIcon trueIcon = m_iconProvider.icon(fileInfo);

                // addPathIcon stores it in the cache AND triggers the repaint of the row
                // IMPORTANT: We pass sourceIndex.row() so that the model
                // updates the correct row in the std::vector!

                m_abstractModel->addPathIcon(fullPath, trueIcon, fileInfo.lastModified(), sourceIndex.row());

                // We only count icons that were really *loaded*, since those cost CPU time
                iconsProcessedInThisBatch++;
            }
        }
    }

    // If the loop was interrupted because icons are still missing,
    // we request the next batch IMMEDIATELY (0 ms).
    // Qt uses the 0 ms to briefly check whether the user clicked or scrolled!
    if (processMoreLater) {
        m_timerUpdateIcons->start(0);
    }
}

void MainWindow::launchAction() {
    QString input1 = m_LineEdit1->text().trimmed();
    QString input2 = m_LineEdit2->text().trimmed();

    if (input1.isEmpty()) return;

    RecentInputList_Add(input1);

    // --- FALL 1: URL ERKENNUNG (http, ftp, www...) ---
    static const QRegularExpression urlRegex(R"(^((ht|f)tp(s?)\:\/\/).*|^www\.)", QRegularExpression::CaseInsensitiveOption);
    if (urlRegex.match(input1).hasMatch()) {
        if (!input1.contains("://")) {
            input1 = "http://" + input1;
        }

        if (QDesktopServices::openUrl(QUrl(input1))) {
            guiHideConditional();
            return;
        }
    }

    if (input2.isEmpty()) {
        // --- FALL 2: E-MAIL ERKENNUNG ---
        static const QRegularExpression emailRegex(R"(^[a-zA-Z0-9.!#$%&'*+/=?^_`{|}~-]+@[a-zA-Z0-9](?:[a-zA-Z0-9-]*[a-zA-Z0-9])?(?:\.[a-zA-Z0-9](?:[a-zA-Z0-9-]*[a-zA-Z0-9])?)*$)");
        if (emailRegex.match(input1).hasMatch()) {
            if (QDesktopServices::openUrl(QUrl("mailto:" + input1))) {
                qDebug() << "(QDesktopServices::openUrl(QUrl(mailto:" + input1 + ")))  sollte ausgelöst haben";
                guiHideConditional();
                return;
            } else {
                qDebug() << "(QDesktopServices::openUrl(QUrl(mailto:" + input1 + ")))  failed";
            }
        }

        if (QDir::isAbsolutePath(input1) || input1.startsWith('~')) {
            QString possiblePath = Helpers::expandPath(input1);

            if (!possiblePath.isEmpty()) {
                QFileInfo checkFile(possiblePath);
                if (checkFile.exists()) {
                    Helpers::browseToFile(possiblePath, m_settings.fileManager);
                    guiHideConditional();
                    return;
                }
            }
        }
    } else {
        // --- Case 3: search modifiers (m_LineEdit2 not empty) ---

        // allow only letters
        QString cleanOp;
        for(auto c : std::as_const(input2)) { if(c.isLetter()) cleanOp += c; }

        if (cleanOp.isEmpty()) return;
        if (cleanOp != input2) {
            m_LineEdit2->setText(cleanOp);
            input2 = cleanOp;
        }

        QString searchText = QUrl::toPercentEncoding(input1);
        QString url;

             if (input2 == "a")   url = "http://www.amazon.de/gp/search/?keywords=" + searchText;
        else if (input2 == "auk") url = "http://www.amazon.co.uk/gp/search/?keywords=" + searchText;
        else if (input2 == "aus") url = "http://www.amazon.com/gp/search/?keywords=" + searchText;
        else if (input2 == "d")   url = "https://duckduckgo.com/?q=" + searchText + "&kp=-2";
        else if (input2 == "g")   url = "http://www.google.com/search?q=" + searchText + "&udm=14&tbs=li:1";
        else if (input2 == "gd")  url = "http://www.google.de/search?q=" + searchText + "&udm=14&tbs=li:1";
        else if (input2 == "i")   url = "http://www.imdb.com/find?q=" + searchText;
        else if (input2 == "m")   url = "http://maps.google.de?q=" + searchText;
        else if (input2 == "u")   url = "http://www.urbandictionary.com/define.php?term=" + searchText;
        else if (input2 == "w")   url = "http://en.wikipedia.org/wiki/Special:Search?search=" + searchText;
        else if (input2 == "wd")  url = "http://de.wikipedia.org/wiki/Special:Search?search=" + searchText;
        else if (input2 == "y")   url = "http://www.youtube.com/results?search_query=" + searchText;

        if (!url.isEmpty()) {
            qDebug() << url;
            if (QDesktopServices::openUrl(QUrl(url))) {
                guiHideConditional();
                return;
            }
        }
        return;
    }

    // default open
    action_ListViewOpenFiles();
}


// ~/.local/state/mkProjects/mkLauncher_state.conf
static QString stateFilePath() {
    const QString stateDir = QDir(QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation)).filePath("mkProjects");
    QDir().mkpath(stateDir);
    return QDir(stateDir).filePath("mkLauncher_state.conf");
}

void MainWindow::loadHistory() {
    QSettings state(stateFilePath(), QSettings::IniFormat);
    m_recentInputList = state.value("History/InputList").toStringList();
    m_recentOpenList  = state.value("History/OpenList").toStringList();
}

// Writes only if something changed
void MainWindow::saveHistory() {
    QSettings state(stateFilePath(), QSettings::IniFormat);
    if (state.value("History/InputList").toStringList() != m_recentInputList) {
        state.setValue("History/InputList", m_recentInputList);
    }
    if (state.value("History/OpenList").toStringList() != m_recentOpenList) {
        state.setValue("History/OpenList", m_recentOpenList);
    }
}

void MainWindow::RecentOpenList_Add(const QString &filePath) {
    // If already in list, delete and prepend again in next step
    m_recentOpenList.removeAll(filePath);

    // Insert at front (index 0)
    m_recentOpenList.prepend(filePath);

    while (m_recentOpenList.size() > 64) {
        m_recentOpenList.removeLast();
    }
}


void MainWindow::RecentInputList_Add(QString searchTerm) {
    // Remove pipe characters (like in the original)
    searchTerm.remove('|');
    if (searchTerm.trimmed().isEmpty()) return;

    // If already in list, delete and prepend again in next step
    m_recentInputList.removeAll(searchTerm);

    // Insert at front (index 0)
    m_recentInputList.prepend(searchTerm);

    while (m_recentInputList.size() > 32) {
        m_recentInputList.removeLast();
    }
}

QString MainWindow::RecentInputList_GetPrevious(const QString &currentText) {
    if (m_recentInputList.isEmpty()) {
        return currentText;
    }

    int idx = m_recentInputList.indexOf(currentText);

    // If the current text is not in the list -> return the newest one
    if (idx == -1) {
        QString sFirst = m_recentInputList.first();
        RecentInputList_Add(currentText);
        return sFirst;
    }

    // If we are not at the end of the list yet -> get the next (older) one
    if (idx + 1 < m_recentInputList.size()) {
        return m_recentInputList.at(idx + 1);
    }

    return currentText; // We have reached the end of the history
}

QString MainWindow::RecentInputList_GetNext(const QString &currentText) {
    int idx = m_recentInputList.indexOf(currentText);

    // If we are not in the list or already at the newest entry (index 0)
    if (idx <= 0) {
        return currentText;
    }

    // One step forward (towards index 0)
    return m_recentInputList.at(idx - 1);
}

void MainWindow::guiHideConditional() {
    m_LineEdit2->clear();
    QTimer::singleShot(50, this, [this]() {
    this->hide();
    });
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

void MainWindow::selectAllItems() {
    auto *activeView = qobject_cast<QAbstractItemView*>(m_viewStack->currentWidget());
    if (!activeView || !m_proxyModel || m_proxyModel->rowCount() == 0) {
        return;
    }
    // Create the selection directly on the proxy model
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

        // IMPORTANT QT QUIRK:
        // For historical reasons, with a 1px pen drawRect(x, y, w, h) in Qt draws
        // a rectangle that is w+1 pixels wide and h+1 pixels high.
        // For the border to lie exactly on pixels 0 to 95, we have to subtract 1.
        painter.drawRect(0, 0, canvas.width() - 1, canvas.height() - 1);

        // The painter is closed and saved automatically at the end of the {} scope
    }

    return canvas;
}

// Async version must return QImage: QPixmap would NOT be thread save!
QImage MainWindow::generateThumbnailAsync(const QFileInfo &fileInfo) {

    QImageReader reader(fileInfo.absoluteFilePath());
    reader.setAutoTransform(true); // Important for EXIF rotations from smartphones

    if (reader.canRead()) {
        // 1. Read the real dimensions of the image (costs hardly any performance)
        QSize originalSize = reader.size();

        // 2. Compute a proportional size that fits into a 96x96 box
        // e.g. 1920x1080 automatically becomes 96x54 here
        QSize scaledSize = originalSize.scaled(QSize(96, 96), Qt::KeepAspectRatio);

        // 3. Tell the reader the proportional size
        reader.setScaledSize(scaledSize);

        QImage img = reader.read();
        if (!img.isNull()) {
            return img;
        }
    }

    return QImage();
}

void MainWindow::updateSearch() {
    handleTextChange(QString());
}

void MainWindow::updateWidgetStyles() {
    static const QString lineEdit1StyleDark = QStringLiteral(
        /* 1. Normal state (unfocused) */
        "QLineEdit {"
        "  border: 1px solid #555555;"
        "  border-top-left-radius: 5px;"
        "  border-bottom-left-radius: 5px;"
        "  padding: 4px;"
        "  padding-left: 7px;"
        "  padding-bottom: 5px;"
        "  background-color: #1a1a1a;"
        "  color: #ffffff;"
        "}"

        /* 2. Hover state (mouse moves over it) */
        "QLineEdit:hover {"
        "  border: 1px solid #666666;"
        "}"

        /* 3. Active focus state (control is selected / keyboard input) */
        "QLineEdit:focus {"
        "  border: 1px solid palette(Accent);"
        "}"

        /* 4. Inactive / disabled (optional) */
        "QLineEdit:disabled {"
        "  border: 1px solid #222222;"
        "  color: #666666;"
        "}"
        );

    static const QString lineEdit1StyleLight = QStringLiteral(
        /* 1. Normal state (unfocused) */
        "QLineEdit {"
        "  border: 1px solid palette(mid);"
        "  border-top-left-radius: 5px;"
        "  border-bottom-left-radius: 5px;"
        "  padding: 4px;"
        "  padding-left: 7px;"
        "  padding-bottom: 5px;"
        "  background-color: palette(base);"
        "  color: palette(text);"
        "}"

        /* 2. Hover state (mouse moves over it) */
        "QLineEdit:hover {"
        "  border: 1px solid palette(Accent);"
        "}"

        /* 3. Active focus state (control is selected / keyboard input) */
        "QLineEdit:focus {"
        "  border: 1px solid palette(Accent);"
        "}"

        /* 4. Inactive / disabled (optional) */
        "QLineEdit:disabled {"
        "  border: 1px solid palette(light);"
        "  background-color: palette(midlight);"
        "  color: palette(dark);"
        "}"
        );

    static const QString lineEdit2StyleDark = QStringLiteral(
        /* 1. Normal state (unfocused) */
        "QLineEdit {"
        "  border: 1px solid #555555;"
        "  border-top-right-radius: 5px;"
        "  border-bottom-right-radius: 5px;"
        "  padding: 4px;"
        "  padding-left: 7px;"
        "  padding-bottom: 5px;"
        "  background-color: #1a1a1a;"
        "  color: #ffffff;"
        "}"

        /* 2. Hover state (mouse moves over it) */
        "QLineEdit:hover {"
        "  border: 1px solid #666666;"
        "}"

        /* 3. Active focus state (control is selected / keyboard input) */
        "QLineEdit:focus {"
        "  border: 1px solid palette(Accent);"
        "}"

        /* 4. Inactive / disabled (optional) */
        "QLineEdit:disabled {"
        "  border: 1px solid #222222;"
        "  color: #666666;"
        "}"
        );

    static const QString lineEdit2StyleLight = QStringLiteral(
        /* 1. Normal state (unfocused) */
        "QLineEdit {"
        "  border: 1px solid palette(mid);"
        "  border-top-right-radius: 5px;"
        "  border-bottom-right-radius: 5px;"
        "  padding: 4px;"
        "  padding-left: 7px;"
        "  padding-bottom: 5px;"
        "  background-color: palette(base);"
        "  color: palette(text);"
        "}"

        /* 2. Hover state (mouse moves over it) */
        "QLineEdit:hover {"
        "  border: 1px solid palette(Accent);"
        "}"

        /* 3. Active focus state (control is selected / keyboard input) */
        "QLineEdit:focus {"
        "  border: 1px solid palette(Accent);"
        "}"

        /* 4. Inactive / disabled (optional) */
        "QLineEdit:disabled {"
        "  border: 1px solid palette(light);"
        "  background-color: palette(midlight);"
        "  color: palette(dark);"
        "}"
        );

    bool isDark = (QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark);

    StyleState targetState;
    if (m_processIsElevated) {
        targetState = StyleState::Elevated;
    } else {
        targetState = isDark ? StyleState::Dark : StyleState::Light;
    }

    QPalette currentPalette = QGuiApplication::palette();

    if (m_currentStyleState == targetState && m_StyleLastPalette == currentPalette) {
        return;
    }

    m_currentStyleState = targetState;
    m_StyleLastPalette = currentPalette;

    if (targetState == StyleState::Dark) {
        this->setStyleSheet(
            "QMainWindow { background-color: palette(window); }"
            "QHeaderView::section { background-color: palette(window); color: palette(window-text); }"
            );

        m_LineEdit1->setStyleSheet(lineEdit1StyleDark);
        m_LineEdit2->setStyleSheet(lineEdit2StyleDark);
    } else {
        // IMPORTANT: Clear the stylesheet when the system switches to light mode!
        // This makes Qt switch back to the light default Breeze design.
        this->setStyleSheet("");

        m_LineEdit1->setStyleSheet(lineEdit1StyleLight);
        m_LineEdit2->setStyleSheet(lineEdit2StyleLight);
    }

    if (targetState == StyleState::Elevated) {
        m_tableView->setStyleSheet(Styles::tableViewColorsElevated % Styles::tableViewHoverElevated);
        m_listView->setStyleSheet(Styles::listViewItemHeight % Styles::listViewColorsElevated);
        m_thumbnailView->setStyleSheet(Styles::listViewColorsElevated);
    } else {
        m_listView->setStyleSheet(Styles::listViewItemHeight);
    }
}

//######################################################################################
// Functions to receive data from other applications

// This belongs to a Public Slot!
void MainWindow::focusEditBox() {
    m_LineEdit1->setFocus();
    m_LineEdit1->selectAll();
}

void MainWindow::winHide() {
    this->hide();
}

void MainWindow::winUnhide() {
    focusEditBox();
    this->show();
}

//######################################################################################
// Protected Overrides

void MainWindow::closeEvent(QCloseEvent *event) {
    saveHistory();
    event->accept();
}

// This happens only once, on creating the window
void MainWindow::showEvent(QShowEvent *event) {
    QMainWindow::showEvent(event);
    updateColumns();
}

void MainWindow::changeEvent(QEvent *event) {
    QMainWindow::changeEvent(event);

    switch (event->type()) {
        case QEvent::ApplicationPaletteChange:
        case QEvent::PaletteChange:
        case QEvent::StyleChange:
        case QEvent::ThemeChange:
            if (m_themeUpdateDebounceTimer) {
                m_themeUpdateDebounceTimer->start(50);
            }
            break;

        default:
            break;
    }
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

                    // Remove "SelectedClicked" bitwise from ALL views
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
                // Only remember the item, the action follows on release (see MouseButtonRelease).
                // Consuming the press keeps selection and current item untouched, like in Dolphin.
                auto *targetView = qobject_cast<QAbstractItemView*>(obj->parent());
                m_middlePressedIndex = targetView ? QPersistentModelIndex(targetView->indexAt(mouseEvent->pos())) : QPersistentModelIndex();
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
            else if (mouseEvent->button() == Qt::MiddleButton) {
                // Act only if the button is released over the same item it was pressed on
                auto *targetView = qobject_cast<QAbstractItemView*>(obj->parent());
                const QModelIndex releasedIndex = targetView ? targetView->indexAt(mouseEvent->pos()) : QModelIndex();
                const QModelIndex pressedIndex = m_middlePressedIndex;
                m_middlePressedIndex = QPersistentModelIndex();

                if (releasedIndex.isValid() && pressedIndex.isValid() && releasedIndex.row() == pressedIndex.row()) {
                    onListItemMiddleClicked(releasedIndex, mouseEvent->modifiers());
                }
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

        // Window-wide hotkeys
        if (keyEvent->modifiers() == (Qt::ControlModifier | Qt::ShiftModifier)) {
            if (keyEvent->key() == Qt::Key_C) {
                action_ListViewCopyPaths();
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
            else if (keyEvent->key() == Qt::Key_E) {
                action_ListViewEditFiles();
                return true;
            }
            else if (keyEvent->key() == Qt::Key_I) {
                action_ListViewFileProperties();
                return true;
            }
            else if (keyEvent->key() == Qt::Key_L) {
                action_ListViewBrowseToFile();
                return true;
            }
            else if (keyEvent->key() == Qt::Key_Y) {
                if (targetWidget == m_LineEdit1) {
                    QString s = RecentInputList_GetNext(m_LineEdit1->text());
                    m_LineEdit1->setText(s);
                    return true;
                }
            }
            else if (keyEvent->key() == Qt::Key_Z) {
                if (targetWidget == m_LineEdit1) {
                    QString s = RecentInputList_GetPrevious(m_LineEdit1->text());
                    m_LineEdit1->setText(s);
                    return true;
                }
            }
        }
        else {
            if (keyEvent->key() == Qt::Key_F2) {
                action_ListViewRenameFiles();
                return true;
            }
            else if (keyEvent->key() == Qt::Key_F5) {
                updateSearch();
                return true;
            }
            else if (keyEvent->key() == Qt::Key_Backspace) {
                if (targetWidget == m_LineEdit2) {
                    if (m_LineEdit2->text().isEmpty()) {
                        m_LineEdit1->setFocus();
                        m_LineEdit1->setCursorPosition(m_LineEdit1->text().length());
                        return true;
                    }
                }
            }
            else if (keyEvent->key() == Qt::Key_Tab) {
                if (targetWidget == m_LineEdit1) {
                    m_LineEdit2->setFocus();
                    return true;
                } else if (targetWidget == m_LineEdit2) {
                    m_LineEdit1->setFocus();
                    return true;
                }
            }
            else if (keyEvent->key() == Qt::Key_Enter || keyEvent->key() == Qt::Key_Return) {
                if (targetWidget == m_LineEdit1 || targetWidget == m_LineEdit2) {
                    launchAction();
                    return true;
                }
            }
            else if (keyEvent->key() == Qt::Key_Up || keyEvent->key() == Qt::Key_Down || keyEvent->key() == Qt::Key_PageUp || keyEvent->key() == Qt::Key_PageDown) {
                if (targetWidget == m_LineEdit1) {
                    auto *activeView = qobject_cast<QAbstractItemView*>(m_viewStack->currentWidget());
                    if (activeView) {
                        QCoreApplication::sendEvent(activeView, keyEvent);
                        return true;
                    }
                }
            }
        }


        // If an editor is currently open in the view (e.g. renaming a file),
        // we must NOT intercept keys like "Del", "Backspace" or "Enter"!
        if (qobject_cast<QLineEdit*>(targetWidget)) {

            return QObject::eventFilter(obj, event);
        }
        else if (qobject_cast<QLineEdit*>(targetWidget->parent())) {
            return QObject::eventFilter(obj, event);
        }
        else {
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
                else if (keyEvent->key() == Qt::Key_I) {
                    action_ListViewFileProperties();
                    return true;
                }
                else if (keyEvent->key() == Qt::Key_L) {
                    action_ListViewBrowseToFile();
                    return true;
                }
                else if (keyEvent->key() == Qt::Key_R) {
                    Helpers::launchRenameTool(QString(), getActiveViewPathList(), m_settings.renameTool, this);
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
                }
                else if (keyEvent->key() == Qt::Key_Escape) {
                    const QMimeData* mimeData = QApplication::clipboard()->mimeData();
                    if (mimeData->hasFormat(m_privateTokenName) && mimeData->data(m_privateTokenName) == m_currentClipboardToken) {
                        QApplication::clipboard()->clear();
                        return true;
                    }
                }
                else if (keyEvent->key() == Qt::Key_Delete) {
                    if (keyEvent->modifiers() & Qt::ShiftModifier) {
                        action_ListViewDeleteFiles(false);
                    } else {
                        action_ListViewDeleteFiles(true); // to recycle bin
                    }
                    return true;
                }
                else if (keyEvent->key() == Qt::Key_Tab) {
                    m_LineEdit1->setFocus();
                    return true;
                }
                else if (keyEvent->key() == Qt::Key_Enter || keyEvent->key() == Qt::Key_Return) {
                    // Note: QLineEdit does not consume the enter/return key, so we'll get an echo of it here.
                    // Since the QLineEdit doesn't get deleted from memory right away (only in next event loop pass),
                    // it will still exist as child of the viewport, so we can check for it here.
                    if (targetWidget->findChild<QLineEdit*>() || (targetWidget->parentWidget() && targetWidget->parentWidget()->findChild<QLineEdit*>())) {
                        return true;
                    }

                    launchAction();
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
