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

MainWindow::MainWindow(const QString &targetDirectory, const QString &focusPath, QWidget *parent)
    : QMainWindow(parent), m_currentDirectory(targetDirectory)
{
    this->setMinimumSize(1, 1);
    setWindowTitle(m_currentDirectory);
    // Icon of the current icon theme, the embedded copy (from dolphin) only if the theme lacks it
    setWindowIcon(QIcon::fromTheme(QStringLiteral("org.kde.dolphin"), QIcon(QStringLiteral(":/icons/org.kde.dolphin.svg"))));
    resize(728, 545);

    m_processIsElevated = Helpers::isCurrentProcessElevated();

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
    if (m_settings.showPlaceholderText) {
        m_LineEdit1->setPlaceholderText(tr("(filter terms)"));
    }
    topControlsHBoxLayout->addWidget(m_LineEdit1);

    m_mainLayout->addWidget(m_topControlsContainerWidget);
    m_topControlsContainerWidget->hide();

    // --------------------------------------------------------------------

    m_abstractModel = new CustomTableModel(&m_settings, 6, this);   // mkFolderWidget uses 6 columns: Name, NewName (hidden), Path (hidden), Size, Changed, Type [unused: Rating, Count, CRC]

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
    m_tableView->setColumnHidden(CustomTableModel::eColPath, true);

    m_tableView->verticalHeader()->setVisible(false);
    m_tableView->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    m_tableView->verticalHeader()->setMinimumSectionSize(0);
    m_tableView->verticalHeader()->setDefaultSectionSize(18);

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

    m_viewStack = new QStackedWidget(this);
    m_viewStack->addWidget(m_listView);
    m_viewStack->addWidget(m_tableView);
    m_viewStack->addWidget(m_thumbnailView);
    m_mainLayout->addWidget(m_viewStack);

    m_viewStack->setCurrentIndex(0);
    m_abstractModel->setModelViewMode(ViewMode::List);

    // --------------------------------------------------------------------

    updateWidgetStyles();

    // --------------------------------------------------------------------
    // Shortcuts: Whole Window

    QShortcut *WindowShortcutN = new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Comma), this);
    WindowShortcutN->setContext(Qt::WindowShortcut);
    connect(WindowShortcutN, &QShortcut::activated, this, &MainWindow::action_EditSettingsFile);

    // --------------------------------------------------------------------
    // Context menu Actions

    m_actionListViewOpenFiles = new QAction(tr("Open"), this);
    connect(m_actionListViewOpenFiles, &QAction::triggered, this, &MainWindow::action_ListViewOpenFiles);

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
    m_actionListViewNewFolder->setIcon(QIcon::fromTheme("folder-new"));
    connect(m_actionListViewNewFolder, &QAction::triggered, this, &MainWindow::action_ListViewNewFolder);


    m_actionListViewNewTextFile = new QAction(tr("Text File"),this);
    m_actionListViewNewTextFile->setIcon(QIcon::fromTheme("document-new"));
    connect(m_actionListViewNewTextFile, &QAction::triggered, this, &MainWindow::action_ListViewNewTextFile);

    //-----------------------------------------------------------

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

    m_actionViewModeShowHidden = new QAction(tr("Show hidden"),this);
    m_actionViewModeShowHidden->setIcon(QIcon::fromTheme("view-hidden-symbolic"));
    m_actionViewModeShowHidden->setCheckable(true);
    m_actionViewModeShowHidden->setShortcut(QKeySequence("Ctrl+H"));
    connect(m_actionViewModeShowHidden, &QAction::triggered, this, &MainWindow::action_toggleShowHidden);

    m_actionViewModeRefresh = new QAction(tr("Refresh"),this);
    m_actionViewModeRefresh->setIcon(QIcon::fromTheme("view-refresh-symbolic"));
    m_actionViewModeRefresh->setShortcut(QKeySequence("F5"));
    connect(m_actionViewModeRefresh, &QAction::triggered, this, &MainWindow::reloadDirectory);

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

    //-----------------------------------------------------------

    m_actionNavigateUp = new QAction(tr("Up"),this);
    m_actionNavigateUp->setShortcut(QKeySequence(Qt::Key_Backspace));
    connect(m_actionNavigateUp, &QAction::triggered, this, &MainWindow::navigateUp);

    m_actionNavigateBack = new QAction(tr("Back"),this);
    m_actionNavigateBack->setShortcut(QKeySequence("Alt+Left"));
    connect(m_actionNavigateBack, &QAction::triggered, this, &MainWindow::navigateBack);

    m_actionNavigateForward = new QAction(tr("Forward"),this);
    m_actionNavigateForward->setShortcut(QKeySequence("Alt+Right"));
    connect(m_actionNavigateForward, &QAction::triggered, this, &MainWindow::navigateForward);

    m_actionNavigateSiblingPrevious = new QAction(tr("Previous sibling folder"),this);
    m_actionNavigateSiblingPrevious->setShortcut(QKeySequence("Ctrl+Up"));
    connect(m_actionNavigateSiblingPrevious, &QAction::triggered, this, &MainWindow::navigateSiblingPrevious);

    m_actionNavigateSiblingNext = new QAction(tr("Next sibling folder"),this);
    m_actionNavigateSiblingNext->setShortcut(QKeySequence("Ctrl+Down"));
    connect(m_actionNavigateSiblingNext, &QAction::triggered, this, &MainWindow::navigateSiblingNext);

    m_actionNavigateClipboardPath = new QAction(tr("To clipboard path"),this);
    m_actionNavigateClipboardPath->setShortcut(QKeySequence("Ctrl+P"));
    connect(m_actionNavigateClipboardPath, &QAction::triggered, this, &MainWindow::navigateToClipboardPath);

    m_actionNavigateDuplicate = new QAction(tr("Duplicate view"),this);
    m_actionNavigateDuplicate->setShortcut(QKeySequence("Ctrl+D"));
    connect(m_actionNavigateDuplicate, &QAction::triggered, this, &MainWindow::duplicateInstance);

    //-----------------------------------------------------------

    if (m_settings.showIconsInMenu == false) {
        m_actionListViewOpenFiles->setIconVisibleInMenu(false);
        m_actionListViewCopyPaths->setIconVisibleInMenu(false);
        m_actionListViewCutFiles->setIconVisibleInMenu(false);
        m_actionListViewCopyFiles->setIconVisibleInMenu(false);
        m_actionListViewDeleteFiles->setIconVisibleInMenu(false);
        m_actionListViewRenameFiles->setIconVisibleInMenu(false);
        m_actionListViewFileProperties->setIconVisibleInMenu(false);
        m_actionListViewPasteFiles->setIconVisibleInMenu(false);
        m_actionViewModeList->setIconVisibleInMenu(false);
        m_actionViewModeDetails->setIconVisibleInMenu(false);
        m_actionViewModeThumbs->setIconVisibleInMenu(false);
        m_actionViewModeShowHidden->setIconVisibleInMenu(false);
        m_actionViewModeRefresh->setIconVisibleInMenu(false);
    }

    if (m_settings.showShortcutsInMenu == false) {
        m_actionListViewOpenFiles->setShortcutVisibleInContextMenu(false);
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
        m_actionViewModeShowHidden->setShortcutVisibleInContextMenu(false);
        m_actionViewModeRefresh->setShortcutVisibleInContextMenu(false);
        m_actionNavigateUp->setShortcutVisibleInContextMenu(false);
        m_actionNavigateBack->setShortcutVisibleInContextMenu(false);
        m_actionNavigateForward->setShortcutVisibleInContextMenu(false);
        m_actionNavigateSiblingPrevious->setShortcutVisibleInContextMenu(false);
        m_actionNavigateSiblingNext->setShortcutVisibleInContextMenu(false);
        m_actionNavigateClipboardPath->setShortcutVisibleInContextMenu(false);
        m_actionNavigateDuplicate->setShortcutVisibleInContextMenu(false);
    }

    // --------------------------------------------------------------------

    qint64 pid = QCoreApplication::applicationPid();
    QString serviceName = QString("org.mkFolderWidget.pid%1").arg(pid);

    if (QDBusConnection::sessionBus().registerService(serviceName)) {
        QDBusConnection::sessionBus().registerObject("/Main", this, QDBusConnection::ExportScriptableSlots | QDBusConnection::ExportScriptableSignals | QDBusConnection::ExportScriptableProperties );
        qDebug() << "DBus-Service erfolgreich registriert:" << serviceName;
    } else {
        qWarning() << "Konnte DBus-Service nicht registrieren:" << serviceName;
    }

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
    connect(m_fileSystemWatcher, &QFileSystemWatcher::directoryChanged, this, &MainWindow::onDirectoryChangedOnDisk);
    connect(m_LineEdit1, &QLineEdit::textChanged, this, &MainWindow::onFilterBoxChange);

    connect(m_abstractModel, &CustomTableModel::filesDropped, this, &MainWindow::onFilesDropped);
    connect(m_listView, &CustomListView::filesDropped, this, &MainWindow::onFilesDropped);
    connect(m_thumbnailView, &CustomListView::filesDropped, this, &MainWindow::onFilesDropped);
    connect(m_tableView, &CustomTableView::filesDropped, this, &MainWindow::onFilesDropped);

    // part of mitigation for Shift+Pos1 / Shift+End not working in tableView
    connect(m_tableView->selectionModel(), &QItemSelectionModel::currentChanged, this, &MainWindow::onTableCurrentChanged);

    // Signals to disable the watcher while the rename edit box is active
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

    setVisibilityBasedOnPath(targetDirectory);
    browseFolder(targetDirectory, focusPath);
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

void MainWindow::onFilterBoxChange(const QString &text) {
    if (text != m_activeFilterTerms) {
        m_activeFilterTerms = text;
        m_proxyModel->setFilterTerms(m_activeFilterTerms);
        m_timerUpdateIcons->start(50);
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

void MainWindow::onDirectoryChangedOnDisk(const QString &path) {
    Q_UNUSED(path);

    qDebug() << "QFileSystemWatcher::directoryChanged:" << path << "m_isEditingFile:" << m_isEditingFile << "m_refreshPendingWhileEditing:" << m_refreshPendingWhileEditing << "m_ignoreNextWatcherSignal:" << m_ignoreNextWatcherSignal;
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
    m_BenchmarkTimer.start();

    // Handle non-existing paths
    QDir dir(directoryPath);
    while (!dir.exists()) {
        if (!dir.cdUp()) {
            directoryPath = QDir::rootPath();
            break;
        }
        directoryPath = dir.absolutePath();
    }

    // Permission check (read permission available?)
    QFileInfo dirInfo(directoryPath);
    if (!dirInfo.isReadable()) {
        QMessageBox::warning(
            this,
            tr("Access denied"),
            tr("You don't have the required permissions to access this folder:<br><br>%1")
                .arg(directoryPath)
            );

        if (m_currentDirectory.isEmpty()) {
            browseFolder(QDir::homePath(), QString(), isHistoryNavigation);
        }

        return;
    }

    // Save history
    if (!isHistoryNavigation && !m_currentDirectory.isEmpty() && m_currentDirectory != directoryPath) {
        m_backHistory.append(m_currentDirectory);   // Push the old folder onto the back stack

        m_forwardHistory.clear();                   // As soon as the user navigates somewhere manually, the forward history is cleared
    }

    bool isSameDirectory = (directoryPath == m_currentDirectory);

    auto *activeView = qobject_cast<QAbstractItemView*>(m_viewStack->currentWidget());

    // --- STORE FOCUS AND SELECTION ---
    QStringList selectedPathsToRestore;
    QString focusedPathToRestore = focusPath;
    QString pathBelowLastSelectedToRestore;
    int lastSelectedProxyRow = -1;

    if (isSameDirectory && m_selectionModel) {
        // A) Remember selected rows & determine the highest row number
        QModelIndexList selectedProxyIndexes = m_selectionModel->selectedIndexes();
        for (const QModelIndex &proxyIdx : std::as_const(selectedProxyIndexes)) {
            if (proxyIdx.column() != 0) {
                continue;
            }

            if (proxyIdx.row() > lastSelectedProxyRow) {
                lastSelectedProxyRow = proxyIdx.row();
            }

            QModelIndex sourceIndex = m_proxyModel->mapToSource(proxyIdx);
            QString path = m_abstractModel->filePath(sourceIndex);
            if (!path.isEmpty()) {
                selectedPathsToRestore.append(path);
            }
        }

        // B) Remember the focused (current) item (unless one was forced via focusPath)
        if (focusedPathToRestore.isEmpty() && activeView) {
            QModelIndex currentProxyIdx = activeView->currentIndex();
            if (currentProxyIdx.isValid()) {
                if (lastSelectedProxyRow == -1) {
                    lastSelectedProxyRow = currentProxyIdx.row();
                }
                QModelIndex sourceIndex = m_proxyModel->mapToSource(currentProxyIdx);
                focusedPathToRestore = m_abstractModel->filePath(sourceIndex);
            }
        }

        // C) Remember the path of the item DIRECTLY BELOW THE LAST selected/focused item
        if (lastSelectedProxyRow != -1) {
            int rowBelow = lastSelectedProxyRow + 1;
            if (rowBelow < m_proxyModel->rowCount()) {
                QModelIndex proxyIdxBelow = m_proxyModel->index(rowBelow, 0);
                QModelIndex sourceIdxBelow = m_proxyModel->mapToSource(proxyIdxBelow);
                pathBelowLastSelectedToRestore = m_abstractModel->filePath(sourceIdxBelow);
            }
        }
    }
    if (!isSameDirectory && focusPath.isEmpty()) {
        if (m_selectionModel) m_selectionModel->clear();
    }

    // --- LOAD NEW FOLDER ---

    m_abstractModel->populateModel_mkFolderWidget(directoryPath);

    m_tableView->setRootIndex(QModelIndex());
    m_listView->setRootIndex(QModelIndex());
    m_thumbnailView->setRootIndex(QModelIndex());

    m_currentDirectory = directoryPath;
    m_ignoreNextWatcherSignal = false;

    // --- UPDATE FILE SYSTEM WATCHER ---
    QStringList watchedPaths = m_fileSystemWatcher->directories();
    if (!watchedPaths.isEmpty()) {
        m_fileSystemWatcher->removePaths(watchedPaths);
    }
    if (!directoryPath.isEmpty() && QFileInfo(directoryPath).isDir()) {
        m_fileSystemWatcher->addPath(directoryPath);
    }

    // --- DISABLE SIGNALS FOR SPEEDUP ---
    if (m_selectionModel) {
        m_selectionModel->blockSignals(true);
    }

    // LOAD PREVIOUS FOCUS AND SELECTION
    int rowCount = m_proxyModel->rowCount();
    QItemSelection restoreSelection;

    QModelIndex proxyIndexToFocus;
    QModelIndex firstStillExistingSelectedProxyIndex;
    QModelIndex indexBelowLastSelectedProxy;

    const QSet<QString> selectedSet(selectedPathsToRestore.begin(), selectedPathsToRestore.end()); // O(n) instead of O(n²)

    if (rowCount > 0 && m_selectionModel) {
        int colCount = m_proxyModel->columnCount();
        for (int i = 0; i < rowCount; ++i) {
            QModelIndex proxyIdx = m_proxyModel->index(i, 0);
            QModelIndex sourceIndex = m_proxyModel->mapToSource(proxyIdx);
            QString currentPath = m_abstractModel->filePath(sourceIndex);

            // Check whether this row was selected
            if (selectedSet.contains(currentPath)) {
                QModelIndex topLeft = proxyIdx;
                QModelIndex bottomRight = m_proxyModel->index(i, colCount - 1);
                restoreSelection.select(topLeft, bottomRight);

                // Remember for prio 2 (in case the original focus is gone but a selection exists)
                if (!firstStillExistingSelectedProxyIndex.isValid()) {
                    firstStillExistingSelectedProxyIndex = proxyIdx;
                }
            }

            // Prio 1:  Check whether this row had (or should have) the focus
            if (!focusedPathToRestore.isEmpty() && currentPath == focusedPathToRestore) {
                proxyIndexToFocus = proxyIdx;
            }

            // Prio 3: Check whether this is the item below the former selection
            if (!pathBelowLastSelectedToRestore.isEmpty() && currentPath == pathBelowLastSelectedToRestore) {
                indexBelowLastSelectedProxy = proxyIdx;
            }
        }

        // --- DOLPHIN FOCUS EVALUATION ---
        if (!proxyIndexToFocus.isValid()) {
            // Prio 2: Set focus on a remaining selected item
            if (firstStillExistingSelectedProxyIndex.isValid()) {
                proxyIndexToFocus = firstStillExistingSelectedProxyIndex;
            }
            // Prio 3: Set focus on the item that used to be BELOW the last selected one
            else if (indexBelowLastSelectedProxy.isValid()) {
                proxyIndexToFocus = indexBelowLastSelectedProxy;
            }
            // Prio 4: Fall back to the relative position (row index) before the reload
            else if (lastSelectedProxyRow >= 0) {
                int targetRow = std::min(lastSelectedProxyRow, rowCount - 1);
                targetRow = std::max(0, targetRow);
                proxyIndexToFocus = m_proxyModel->index(targetRow, 0);
            }
            // Prio 5: First item in the folder
            else {
                proxyIndexToFocus = m_proxyModel->index(0, 0);
            }
        }

        // Apply the focus
        if (proxyIndexToFocus.isValid() && activeView) {
            activeView->setCurrentIndex(proxyIndexToFocus);
        }

        // Selection model signals are blocked here, so currentChanged() never reaches the views.
        // Re-sync the Shift range selection anchors manually (for all views, as they share the selection model).
        if (proxyIndexToFocus.isValid()) {
            m_listView->setSelectionAnchor(proxyIndexToFocus);
            m_thumbnailView->setSelectionAnchor(proxyIndexToFocus);
            m_tableView->setProperty("selectionAnchor", QVariant::fromValue(QModelIndex(proxyIndexToFocus)));
        }

        // Apply the selection
        if (!restoreSelection.isEmpty() && m_selectionModel) {
            m_selectionModel->select(restoreSelection, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        }
    }

    // --- RE-ENABLE SIGNALS ---
    if (m_selectionModel) {
        m_selectionModel->blockSignals(false);
    }

    setWindowTitle(m_currentDirectory);
    QFileInfo fileInfo(m_currentDirectory);
    setWindowIcon(m_iconProvider.icon(fileInfo));

    // --- Update columns of tableView ---
    if (m_viewStack->currentWidget() == m_tableView) {
        updateColumns();
    }

    if (activeView) {
        QModelIndex currentIdx = activeView->currentIndex();
        if (currentIdx.isValid()) {
            activeView->scrollTo(currentIdx, QAbstractItemView::EnsureVisible);
        }
    }

    m_timerUpdateIcons->start(20);
}

void MainWindow::updateColumns() {
    if (m_bHeaderVisible) {
        m_tableView->horizontalHeader()->setSectionResizeMode(CustomTableModel::eColSize, QHeaderView::ResizeToContents);
        m_tableView->horizontalHeader()->setSectionResizeMode(CustomTableModel::eColDate, QHeaderView::ResizeToContents);
        m_tableView->horizontalHeader()->setSectionResizeMode(CustomTableModel::eColType, QHeaderView::ResizeToContents);
        m_tableView->horizontalHeader()->setSectionResizeMode(CustomTableModel::eColName, QHeaderView::Stretch);
        m_tableView->horizontalHeader()->doItemsLayout();
        constexpr int kMinNameWidth = 100;   // Stretch may squeeze Name below this in narrow windows
        int eColNameWidth = qMax(m_tableView->columnWidth(CustomTableModel::eColName), kMinNameWidth);
        m_tableView->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
        m_tableView->setColumnWidth(CustomTableModel::eColName, eColNameWidth);
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
        const QMimeData *mimeData = QApplication::clipboard()->mimeData();  // Note: This CAN be slow if acquiring the clipboard is delayed.

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
        subMenuView->addAction(m_actionViewModeShowHidden);
        m_actionViewModeShowHidden->setChecked(m_bShowHiddenFiles);
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

        mainMenu.addSeparator(); //-----------------------------------------

        QMenu *subMenuNavigate = mainMenu.addMenu(tr("Navigate"));
        subMenuNavigate->addAction(m_actionNavigateUp);
        subMenuNavigate->addSeparator();
        subMenuNavigate->addAction(m_actionNavigateBack);
        subMenuNavigate->addAction(m_actionNavigateForward);
        subMenuNavigate->addSeparator();
        subMenuNavigate->addAction(m_actionNavigateSiblingPrevious);
        subMenuNavigate->addAction(m_actionNavigateSiblingNext);
        subMenuNavigate->addSeparator();
        subMenuNavigate->addAction(m_actionNavigateClipboardPath);
        bool bClipboardHasPath = false;
        if (mimeData && mimeData->hasText()) {
            QString possiblePath = Helpers::getPathFromClipboard(mimeData->text());
            bClipboardHasPath = !possiblePath.isEmpty() && QFile::exists(possiblePath);
        }
        m_actionNavigateClipboardPath->setEnabled(bClipboardHasPath);
        subMenuNavigate->addAction(m_actionNavigateDuplicate);

        mainMenu.addSeparator(); //-----------------------------------------

        mainMenu.addAction(m_actionListViewPasteFiles);
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
    } else {
        QStringList selectedPaths = getActiveViewPathList(); // Used by KDE SERVICE-ACTIONS

        const KService::Ptr openService = updateOpenActionText(selectedPaths);
        mainMenu.addAction(m_actionListViewOpenFiles);
        mainMenu.setDefaultAction(m_actionListViewOpenFiles);

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

QSet<int> MainWindow::getActiveViewRowSet() {
    QSet<int> rowSet;

    // 1. Get all selected cells/indexes
    QModelIndexList selectedIndexes = m_selectionModel->selectedIndexes();  // we use selectedIndexes() instead of selectedRows() because the latter only list rows in which all columns are selected!
    if (selectedIndexes.isEmpty()) return rowSet;

    for (const QModelIndex &proxyIndex : std::as_const(selectedIndexes)) {
        if (proxyIndex.column() == CustomTableModel::eColName) {
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

// Middle click (on release) on an item, modeled after Dolphin:
// - Folder: open it in a new instance
// - File: open it with the 2nd preferred application, Shift + middle click with the 3rd
void MainWindow::onListItemMiddleClicked(const QModelIndex &proxyIndex, Qt::KeyboardModifiers modifiers) {
    const QString path = m_abstractModel->filePath(m_proxyModel->mapToSource(proxyIndex));
    if (path.isEmpty()) return;

    QFileInfo fileInfo(path);
    if (fileInfo.isDir()) {
        launchNewInstance(fileInfo.absoluteFilePath());
        return;
    }
    if (!fileInfo.exists()) return;

    Helpers::openFileWithPreferredApp(fileInfo.absoluteFilePath(), (modifiers & Qt::ShiftModifier) ? 2 : 1);
}

// Labels the "Open" action like Dolphin: "Open with <app>" if all selected files are opened with the same application.
// Returns that application, or nullptr.
KService::Ptr MainWindow::updateOpenActionText(const QStringList &pathList) {
    const KService::Ptr service = Helpers::openActionService(pathList, m_settings);
    if (service) {
        m_actionListViewOpenFiles->setText(tr("Open with %1").arg(service->name().replace('&', QLatin1String("&&"))));
        m_actionListViewOpenFiles->setIcon(QIcon::fromTheme(service->icon()));
    } else {
        m_actionListViewOpenFiles->setText(tr("Open"));
        m_actionListViewOpenFiles->setIcon(QIcon());
    }
    return service;
}

void MainWindow::action_ListViewOpenFiles() {
    QStringList pathList = getActiveViewPathList();
    if (pathList.isEmpty()) return;

    if (pathList.size() == 1) {
        QFileInfo singleFileInfo(pathList.first());
        if (singleFileInfo.isDir()) {
            if (QGuiApplication::keyboardModifiers() & Qt::ControlModifier) {
                launchNewInstance(singleFileInfo.absoluteFilePath());
            } else {
                browseFolder(singleFileInfo.absoluteFilePath());
            }
            return;
        }
    }

    QList<QUrl> urlsToOpen;

    for (const QString &path : std::as_const(pathList)) {
        QFileInfo fileInfo(path);
        if (!fileInfo.exists()) continue;

        switch (Helpers::openMethod(fileInfo, m_settings)) {
        case Helpers::OpenMethod::DesktopEntry:
            Helpers::launchDesktopFile(Helpers::getDesktopEntry(fileInfo));
            break;
        case Helpers::OpenMethod::Execute:
            QProcess::startDetached(path, QStringList(), fileInfo.absolutePath());
            break;
        case Helpers::OpenMethod::PreferredApp:
            urlsToOpen.append(QUrl::fromLocalFile(fileInfo.absoluteFilePath()));
            break;
        }
    }

    if (!urlsToOpen.isEmpty()) {
        Helpers::openUrlsWithKIO(urlsToOpen);
    }
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

// FileSystemWatcher will trigger the update of the view
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
}

void MainWindow::action_ListViewRenameFiles() {
    auto *activeView = qobject_cast<QAbstractItemView*>(m_viewStack->currentWidget());
    if (!activeView) return;

    const QStringList pathList = getActiveViewPathList();
    if (pathList.isEmpty()) return;

    if (pathList.size() > 1) {
        Helpers::launchRenameTool(m_currentDirectory, pathList, m_settings.renameTool, this);
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

void MainWindow::action_ListViewPasteFiles() {
    const QMimeData *mimeData = QApplication::clipboard()->mimeData();
    if (!mimeData || !mimeData->hasUrls()) return;

    QList<QUrl> urlFileList = mimeData->urls();

    OperationType operationType = OperationType::Copy;

    // Check for the KDE cut flag
    if (mimeData->hasFormat("application/x-kde-cutselection")) {
        if (mimeData->data("application/x-kde-cutselection") == "1") {
            operationType = OperationType::Move;
        }
    }
    // Check for the GNOME cut flag (in case it was cut by Nautilus or similar)
    else if (mimeData->hasFormat("x-special/gnome-copied-files")) {
        QString gnomeData = QString::fromUtf8(mimeData->data("x-special/gnome-copied-files"));
        if (gnomeData.startsWith("cut")) {
            operationType = OperationType::Move;
        }
    }

    Helpers::startFileOperation(operationType, urlFileList, m_currentDirectory, true, m_currentDirectory, this);
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

    Helpers::startFileOperation(operationType, urlList, targetDir, false, m_currentDirectory, this);
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

void MainWindow::action_toggleShowHidden() {
    m_bShowHiddenFiles = !m_bShowHiddenFiles;
    m_proxyModel->setShowHiddenFiles(m_bShowHiddenFiles);
}

void MainWindow::updateWidgetStyles() {

        static const QString lineEditStyleDark = QStringLiteral(
        /* 1. Normal state (unfocused) */
        "QLineEdit {"
        "  border: 1px solid #555555;"
        "  padding: 0px;"
        "  padding-left: 4px;"
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

    static const QString lineEditStyleLight = QStringLiteral(
        /* 1. Normal state (unfocused) */
        "QLineEdit {"
        "  border: 1px solid palette(mid);"
        "  padding: 0px;"
        "  padding-left: 4px;"
        "  background-color: palette(window);"
        "  color: palette(window-text);"
        "}"

        /* 2. Hover state (mouse moves over it) */
        "QLineEdit:hover {"
        "  border: 1px solid palette(Accent);"
        "}"

        /* 3. Active focus state (control is selected / keyboard input) */
        "QLineEdit:focus {"
        "  border: 1px solid #ff0000;"
        "}"

        /* 4. Inactive / disabled (optional) */
        "QLineEdit:disabled {"
        "  border: 1px solid #222222;"
        "  color: #666666;"
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
        m_LineEdit1->setStyleSheet(lineEditStyleDark);
    } else {
        m_LineEdit1->setStyleSheet(lineEditStyleLight);
    }

    if (targetState == StyleState::Elevated) {
        m_tableView->setStyleSheet(Styles::tableViewColorsElevated % Styles::tableViewHoverElevated);
        m_listView->setStyleSheet(Styles::listViewItemHeight % Styles::listViewColorsElevated);
        m_thumbnailView->setStyleSheet(Styles::listViewColorsElevated);
    } else {
        m_listView->setStyleSheet(Styles::listViewItemHeight);
    }

}

void MainWindow::setVisibilityBasedOnPath(const QString &path) {
    if (path.isEmpty()) return;

    bool isHiddenPath = false;

    // Split the path into its parts (ignores double slashes)
    QDir dir(path);
    QStringList pathComponents = dir.absolutePath().split('/', Qt::SkipEmptyParts);

    for (const QString &component : std::as_const(pathComponents)) {
        if (component.startsWith('.') && component != "." && component != "..") {
            isHiddenPath = true;
            break;
        }
    }

    m_bShowHiddenFiles = isHiddenPath;
    m_proxyModel->setShowHiddenFiles(m_bShowHiddenFiles);
    m_abstractModel->setShowFileExtensions(m_bShowHiddenFiles);
}

//######################################################################################
// Functions to receive data from other applications

// This belongs to a Public Slot!
// Can be called over DBus to hand over a path to browse to.
void MainWindow::openPath(const QString &path) {
    if (path.isEmpty()) return;

    QDir targetDirectory(path);
    if (targetDirectory.exists()) {
        setVisibilityBasedOnPath(path);
        browseFolder(path);
    }
}

void MainWindow::closeEvent(QCloseEvent *event) {
    // If you use QtConcurrent, you can force the global thread pool
    // not to wait for pending tasks (caution: running threads are terminated anyway):
    QThreadPool::globalInstance()->clear();
    event->accept();
}

void MainWindow::changeEvent(QEvent *event) {
    QMainWindow::changeEvent(event);

    // Reacts to system palette changes (e.g. changing the accent color in KDE)
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

                // Case A (X11): Click received AFTER Qt processes activation.
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
            else if (mouseEvent->button() == Qt::BackButton) {
                if (!m_topControlsContainerWidget->isHidden()) {
                    m_LineEdit1->clear();
                    m_topControlsContainerWidget->hide();
                }
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
            if (keyEvent->key() == Qt::Key_F) {
                if (!m_currentDirectory.isEmpty() && !m_settings.searchTool.isEmpty()) {
                    Helpers::openFileListWithHandler(m_settings.searchTool, { m_currentDirectory });
                }
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
                if (m_topControlsContainerWidget->isHidden()) {
                    m_topControlsContainerWidget->show();
                }
                if (!m_LineEdit1->hasFocus()) {
                    m_LineEdit1->setFocus();
                }
                m_LineEdit1->selectAll();
                return true;
            }
            else if (keyEvent->key() == Qt::Key_H) {
                action_toggleShowHidden();
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


        // If an editor is currently open in the view (e.g. renaming a file),
        // we must NOT intercept keys like "Del", "Backspace" or "Enter"!
        if (qobject_cast<QLineEdit*>(targetWidget)) {
            if (targetWidget == m_LineEdit1) {
                if (keyEvent->key() == Qt::Key_Escape) {
                    m_LineEdit1->clear();
                    m_topControlsContainerWidget->hide();
                    return true;
                }
                else if (keyEvent->key() == Qt::Key_Backspace) {
                    if (keyEvent->modifiers() == Qt::ControlModifier) {
                        if (m_LineEdit1->text().isEmpty()) {
                            m_topControlsContainerWidget->hide();
                        } else {
                            m_LineEdit1->clear();
                        }
                        return true;
                    } else {
                        return QObject::eventFilter(obj, event);
                    }
                }
            }

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
                else if (keyEvent->key() == Qt::Key_R) {
                    Helpers::launchRenameTool(m_currentDirectory, getActiveViewPathList(), m_settings.renameTool, this);
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
                else if (keyEvent->key() == Qt::Key_Up) {
                    navigateSiblingPrevious();
                    return true;
                }
                else if (keyEvent->key() == Qt::Key_Down) {
                    navigateSiblingNext();
                    return true;
                }
                else if (keyEvent->key() == Qt::Key_Backspace) {
                    if (!m_topControlsContainerWidget->isHidden()) {
                        m_LineEdit1->setFocus();
                        m_LineEdit1->selectAll();
                    }
                    return true;
                }
            }
            else {
                if (keyEvent->key() == Qt::Key_F2) {
                    action_ListViewRenameFiles();
                    return true;
                }
                else if (keyEvent->key() == Qt::Key_F5) {
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
                    if (!m_topControlsContainerWidget->isHidden()) {
                         m_LineEdit1->setFocus();
                    } else {
                        navigateUp();
                    }
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
    if (m_currentDirectory.isEmpty()) return;

    // 1. Create unique dummy name
    QDir dir(m_currentDirectory);
    QString baseName = tr("New Folder");
    QString folderName = baseName;
    int counter = 2;

    while (dir.exists(folderName)) {
        folderName = QString("%1 (%2)").arg(baseName).arg(counter++);
    }

    // We tell the watcher slot to ignore the next signal
    m_ignoreNextWatcherSignal = true;

    // 2. Physically create the dummy folder on disk
    if (!dir.mkdir(folderName)) {
        m_ignoreNextWatcherSignal = false;
        return;
    }

    auto *activeView = qobject_cast<QAbstractItemView*>(m_viewStack->currentWidget());
    if (!activeView) {
        // Fallback/safety in case there is no valid view widget in the stack
        m_ignoreNextWatcherSignal = false;
        return;
    }

    // 3. Tell the model to reload the directory
    m_abstractModel->populateModel_mkFolderWidget(m_currentDirectory);

    // 4. Find the QModelIndex of the new folder in the ACTIVE view
    // IMPORTANT: We take the model of the view the user is currently looking at!
    QAbstractItemModel *currentViewModel = activeView->model();
    if (!currentViewModel) return;

    QModelIndex targetIndex;
    QModelIndexList matches = currentViewModel->match(currentViewModel->index(0, 0), Qt::EditRole, folderName, 1, Qt::MatchExactly | Qt::MatchWrap);

    if (!matches.isEmpty()) {
        targetIndex = matches.first();
    }

    // 5. Point the ACTIVE view to the new folder and start inline editing
    if (targetIndex.isValid()) {
        activeView->scrollTo(targetIndex, QAbstractItemView::EnsureVisible);
        activeView->setCurrentIndex(targetIndex);

        // Opens the edit box in the view the user is currently in
        activeView->edit(targetIndex);
    }
}

void MainWindow::action_ListViewNewTextFile() {
    if (m_currentDirectory.isEmpty()) return;

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

    QString possiblePath = Helpers::expandPath(clipboard->text());

    if (!possiblePath.isEmpty()) {
        QFileInfo fileInfo(possiblePath);
        if (!fileInfo.exists()) return;

        if (fileInfo.isDir()) {
            browseFolder(fileInfo.absoluteFilePath(), QString());
        } else {
            browseFolder(fileInfo.absolutePath(), fileInfo.absoluteFilePath());
        }
    }
}

void MainWindow::duplicateInstance() {
    launchNewInstance(m_currentDirectory, getActiveViewCurrentItemPath());
}

// Opens directoryPath in a new mkFolderWidget process, placed slightly offset from this window with the same size
void MainWindow::launchNewInstance(const QString &directoryPath, const QString &focusPath) {
    QString appPath = QCoreApplication::applicationFilePath();
    QStringList arguments;
    arguments << "-X" << QString::number(this->x() + 40)
              << "-Y" << QString::number(this->y() + 40)
              << "-W" << QString::number(this->width())
              << "-H" << QString::number(this->height())
              << directoryPath;

    if (!focusPath.isEmpty()) {
        arguments << "-f" << focusPath;
    }

    QProcess::startDetached(appPath, arguments);
}

void MainWindow::elevateInstance() {
    if (m_processIsElevated) return;

    QString appPath = QCoreApplication::applicationFilePath();
    QStringList arguments;
    arguments << "-X" << QString::number(this->x() + 40)
              << "-Y" << QString::number(this->y() + 40)
              << "-W" << QString::number(this->width())
              << "-H" << QString::number(this->height())
              << m_currentDirectory;

    QString focusedFile = getActiveViewCurrentItemPath();
    if (!focusedFile.isEmpty()) {
        arguments << "-f" << focusedFile;
    }

    bool bSuccess = false;
    QStringList pkexecArgs;
    pkexecArgs << "env"
               // We take the display info of the current user and pass it on to root
               << QString("DISPLAY=%1").arg(QString::fromLocal8Bit(qgetenv("DISPLAY")))
               << QString("XAUTHORITY=%1").arg(QString::fromLocal8Bit(qgetenv("XAUTHORITY")))
               // IMPORTANT: Forces Qt to X11/XWayland, since Wayland blocks root GUIs
               << "QT_QPA_PLATFORM=xcb"
               // Only NOW follow your app and your arguments
               << appPath
               << arguments;
    qint64 pid;
    bSuccess = QProcess::startDetached("pkexec", pkexecArgs, QString(), &pid);
    if (bSuccess) {
        QMetaObject::invokeMethod(qApp, "closeAllWindows", Qt::QueuedConnection);
    }
}

void MainWindow::navigateUp() {
    if (m_currentDirectory.isEmpty()) return;

    QDir currentDir(m_currentDirectory);
    if (currentDir.isRoot()) {
    } else {
        QString focusPath = currentDir.absolutePath();
        currentDir.cdUp();
        QString parentPath = currentDir.absolutePath();

        browseFolder(parentPath, focusPath);
    }
}

void MainWindow::navigateBack() {
    if (m_backHistory.isEmpty()) return;

    // Take the last path from the history and remove it
    QString targetPath = m_backHistory.takeLast();

    // Push the current path onto the forward history
    if (!m_currentDirectory.isEmpty()) {
        m_forwardHistory.append(m_currentDirectory);
    }

    browseFolder(targetPath, QString(), true);
}

void MainWindow::navigateForward() {
    if (m_forwardHistory.isEmpty()) return;

    // Take the last path from the forward history
    QString targetPath = m_forwardHistory.takeLast();

    // Put the current path back into the normal history
    if (!m_currentDirectory.isEmpty()) {
        m_backHistory.append(m_currentDirectory);
    }

    browseFolder(targetPath, QString(), true);
}

void MainWindow::navigateSiblingNext() {
    QString siblingPath = Helpers::getSiblingPath(m_currentDirectory, false);
    if (!siblingPath.isEmpty()) {
        browseFolder(siblingPath, QString(), false);
    }
}

void MainWindow::navigateSiblingPrevious() {
    QString siblingPath = Helpers::getSiblingPath(m_currentDirectory, true);
    if (!siblingPath.isEmpty()) {
        browseFolder(siblingPath, QString(), false);
    }
}
