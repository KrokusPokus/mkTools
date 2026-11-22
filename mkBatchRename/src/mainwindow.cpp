#include "mainwindow.h"
#include "customitemdelegates.h"
#include "customproxystyle.h"
#include "filepropertiesdialog.h"
#include "stylesheets.h"

#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QtConcurrent>
#include <QDateTime>
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
#include <QStyleOptionViewItem>
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

MainWindow::MainWindow(QString targetDirectory, QStringList externalPathList, QWidget *parent)
    : QMainWindow(parent)
    , m_currentDirectory(std::move(targetDirectory))
{
    if (!m_currentDirectory.isEmpty()) {
        setWindowTitle(m_currentDirectory);
    } else {
        setWindowTitle("mkBatchRename");
    }
    setWindowIcon(QIcon(":/icons/app.ico"));

    m_processIsElevated = Helpers::isCurrentProcessElevated();

    m_centralWidget = new QWidget(this);
    setCentralWidget(m_centralWidget);

    // --------------------------------------------------------------------

    m_settings.showFileExtensions = true; // mkBatchRename always shows the file extension
    m_abstractModel = new CustomTableModel(&m_settings, 6, this);   // mkBatchRename uses 6 columns: Name, NewName, Path, Size, Changed, Type [unused: Rating, Count, CRC]

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

    updateColumns();
    m_viewStack = new QStackedWidget(this);
    m_viewStack->addWidget(m_listView);
    m_viewStack->addWidget(m_tableView);
    m_viewStack->addWidget(m_thumbnailView);
    m_viewStack->setCurrentIndex(1);
    m_abstractModel->setModelViewMode(ViewMode::Detail);

    // --------------------------------------------------------------------
    // --------------------------------------------------------------------

    QLabel *titleLabel13 = new QLabel(tr("Apply To"));
    titleLabel13->setStyleSheet("font-size: 12pt; margin-top: 10px;");
    m_groupBox13 = new QGroupBox("", this);
    m_groupBox13_RadioButton1 = new QRadioButton(tr("Name"));
    m_groupBox13_RadioButton2 = new QRadioButton(tr("Extension"));
    m_groupBox13_RadioButton3 = new QRadioButton(tr("Full"));
    m_groupBox13_RadioButton1->setChecked(true);
    QHBoxLayout *groupBox13Layout = new QHBoxLayout();
    groupBox13Layout->addWidget(m_groupBox13_RadioButton1);
    groupBox13Layout->addWidget(m_groupBox13_RadioButton2);
    groupBox13Layout->addWidget(m_groupBox13_RadioButton3);
    groupBox13Layout->addStretch();
    m_groupBox13->setLayout(groupBox13Layout);

    QLabel *titleLabel3 = new QLabel(tr("Replace"));
    titleLabel3->setStyleSheet("font-size: 12pt; margin-top: 10px;");
    m_groupBox3 = new QGroupBox("", this);
    QLabel *groupBox3_Label1 = new QLabel(tr("Find:"));
    m_groupBox3_LineEdit1 = new QLineEdit();
    m_groupBox3_CheckBox1 = new QCheckBox(tr("Match Case"));
    m_groupBox3_CheckBox1->setChecked(true);
    m_groupBox3_CheckBox2 = new QCheckBox(tr("RegEx"));
    m_groupBox3_CheckBox2->setChecked(false);
    QLabel *groupBox3_Label2 = new QLabel(tr("Replace with:"));
    m_groupBox3_LineEdit2 = new QLineEdit();
    QGridLayout *groupBox3Layout = new QGridLayout();
    groupBox3Layout->addWidget(groupBox3_Label1, 1, 0);
    groupBox3Layout->addWidget(m_groupBox3_LineEdit1, 1, 1);
    groupBox3Layout->addWidget(m_groupBox3_LineEdit2, 2, 1, 1, 3);
    groupBox3Layout->addWidget(m_groupBox3_CheckBox1, 1, 2);
    groupBox3Layout->addWidget(m_groupBox3_CheckBox2, 1, 3);
    groupBox3Layout->addWidget(groupBox3_Label2, 2, 0);
    m_groupBox3->setLayout(groupBox3Layout);

    QLabel *titleLabel5 = new QLabel(tr("Remove"));
    titleLabel5->setStyleSheet("font-size: 12pt; margin-top: 10px;");
    m_groupBox5 = new QGroupBox("", this);
    QLabel *groupBox5_Label1 = new QLabel(tr("First:"));
    m_groupBox5_SpinBox1 = new ZeroOnEmptySpinBox();
    m_groupBox5_SpinBox1->setRange(0, 255);
    QLabel *groupBox5_Label2 = new QLabel(tr("Last:"));
    m_groupBox5_SpinBox2 = new ZeroOnEmptySpinBox();
    m_groupBox5_SpinBox2->setRange(0, 255);
    QLabel *groupBox5_Label3 = new QLabel(tr("From:"));
    m_groupBox5_SpinBox3 = new ZeroOnEmptySpinBox();
    m_groupBox5_SpinBox3->setRange(0, 255);
    QLabel *groupBox5_Label4 = new QLabel(tr("Count:"));
    m_groupBox5_SpinBox4 = new ZeroOnEmptySpinBox();
    m_groupBox5_SpinBox4->setRange(0, 255);
    QGridLayout *groupBox5Layout = new QGridLayout();
    groupBox5Layout->addWidget(groupBox5_Label1,     0, 0);
    groupBox5Layout->addWidget(m_groupBox5_SpinBox1, 0, 1);
    groupBox5Layout->addWidget(groupBox5_Label3,     1, 0);
    groupBox5Layout->addWidget(m_groupBox5_SpinBox3, 1, 1);
    groupBox5Layout->addWidget(groupBox5_Label4,     1, 2);
    groupBox5Layout->addWidget(m_groupBox5_SpinBox4, 1, 3);
    groupBox5Layout->addWidget(groupBox5_Label2,     2, 0);
    groupBox5Layout->addWidget(m_groupBox5_SpinBox2, 2, 1);
    groupBox5Layout->setColumnStretch(0, 0);
    groupBox5Layout->setColumnStretch(1, 0);
    groupBox5Layout->setColumnStretch(2, 0);
    groupBox5Layout->setColumnStretch(3, 0);
    groupBox5Layout->setColumnStretch(4, 1);  // The very last column gets stretch 1
    m_groupBox5->setLayout(groupBox5Layout);

    QLabel *titleLabel7 = new QLabel(tr("Add"));
    titleLabel7->setStyleSheet("font-size: 12pt; margin-top: 10px;");
    m_groupBox7 = new QGroupBox("", this);
    QLabel *groupBox7_Label1 = new QLabel(tr("Prefix:"));
    m_groupBox7_LineEdit1 = new QLineEdit();
    QLabel *groupBox7_Label2 = new QLabel(tr("Insert:"));
    m_groupBox7_LineEdit2 = new QLineEdit();
    QLabel *groupBox7_Label3 = new QLabel(tr("at pos.:"));
    m_groupBox7_SpinBox = new ZeroOnEmptySpinBox();
    m_groupBox7_SpinBox->setRange(0, 255);
    QLabel *groupBox7_Label4 = new QLabel(tr("Suffix:"));
    m_groupBox7_LineEdit3 = new QLineEdit();
    QGridLayout *groupBox7Layout = new QGridLayout();
    groupBox7Layout->addWidget(groupBox7_Label1, 0, 0);
    groupBox7Layout->addWidget(m_groupBox7_LineEdit1, 0, 1, 1, 3);
    groupBox7Layout->addWidget(groupBox7_Label2, 1, 0);
    groupBox7Layout->addWidget(m_groupBox7_LineEdit2, 1, 1);
    groupBox7Layout->addWidget(groupBox7_Label3, 1, 2);
    groupBox7Layout->addWidget(m_groupBox7_SpinBox, 1, 3);
    groupBox7Layout->addWidget(groupBox7_Label4, 2, 0);
    groupBox7Layout->addWidget(m_groupBox7_LineEdit3, 2, 1, 1, 3);
    m_groupBox7->setLayout(groupBox7Layout);

    QLabel *titleLabel9 = new QLabel(tr("Add Folder Name"));
    titleLabel9->setStyleSheet("font-size: 12pt; margin-top: 10px;");
    m_groupBox9 = new QGroupBox("", this);
    m_groupBox9_ComboBox = new QComboBox();
    m_groupBox9_ComboBox->addItems({tr("None"), tr("Prefix"), tr("Suffix")});
    QLabel *groupBox9_Label1 = new QLabel(tr("Separator:"));
    m_groupBox9_LineEdit = new QLineEdit();
    m_groupBox9_LineEdit->setFixedWidth(24);
    m_groupBox9_LineEdit->setText("_");
    QLabel *groupBox9_Label2 = new QLabel(tr("Levels:"));
    m_groupBox9_SpinBox = new QSpinBox();
    m_groupBox9_SpinBox->setRange(1, 255);
    m_groupBox9_SpinBox->setValue(1);
    QHBoxLayout *groupBox9Layout = new QHBoxLayout;
    groupBox9Layout->addWidget(m_groupBox9_ComboBox);
    groupBox9Layout->addWidget(groupBox9_Label2);
    groupBox9Layout->addWidget(m_groupBox9_SpinBox);
    groupBox9Layout->addWidget(groupBox9_Label1);
    groupBox9Layout->addWidget(m_groupBox9_LineEdit);
    groupBox9Layout->addStretch();
    m_groupBox9->setLayout(groupBox9Layout);

    QLabel *titleLabel2 = new QLabel(tr("Add Numbering"));
    titleLabel2->setStyleSheet("font-size: 12pt; margin-top: 10px;");
    m_groupBox2 = new QGroupBox("", this);
    m_groupBox2_ComboBox = new QComboBox();
    m_groupBox2_ComboBox->addItems({tr("None"), tr("Prefix"), tr("Suffix")});
    QLabel *groupBox2_Label1 = new QLabel(tr("Start at:"));
    m_groupBox2_SpinBox1 = new ZeroOnEmptySpinBox();
    m_groupBox2_SpinBox1->setRange(0, 100000);
    m_groupBox2_SpinBox1->setValue(1);
    QLabel *groupBox2_Label2 = new QLabel(tr("Step:"));
    m_groupBox2_SpinBox2 = new QSpinBox();
    m_groupBox2_SpinBox2->setRange(1, 100000);
    m_groupBox2_SpinBox2->setValue(1);
    QLabel *groupBox2_Label3 = new QLabel(tr("Separator:"));
    m_groupBox2_LineEdit = new QLineEdit();
    m_groupBox2_LineEdit->setFixedWidth(24);
    m_groupBox2_LineEdit->setText("_");
    QHBoxLayout *groupBox2Layout = new QHBoxLayout;
    groupBox2Layout->addWidget(m_groupBox2_ComboBox);
    groupBox2Layout->addWidget(groupBox2_Label1);
    groupBox2Layout->addWidget(m_groupBox2_SpinBox1);
    groupBox2Layout->addWidget(groupBox2_Label2);
    groupBox2Layout->addWidget(m_groupBox2_SpinBox2);
    groupBox2Layout->addWidget(groupBox2_Label3);
    groupBox2Layout->addWidget(m_groupBox2_LineEdit);
    groupBox2Layout->addStretch();
    m_groupBox2->setLayout(groupBox2Layout);

    QLabel *titleLabel10 = new QLabel(tr("Number Manipulation"));
    titleLabel10->setStyleSheet("font-size: 12pt; margin-top: 10px;");
    m_groupBox10 = new QGroupBox("", this);
    QLabel *groupBox10_Label0 = new QLabel(tr("Group:"));
    m_groupBox10_SpinBox0 = new QSpinBox();
    m_groupBox10_SpinBox0->setRange(1, 99);
    m_groupBox10_SpinBox0->setValue(1);
    m_groupBox10_CheckBox1 = new QCheckBox(tr("Leading Zeroes"));
    QLabel *groupBox10_Label1 = new QLabel(tr("Digits:"));
    m_groupBox10_SpinBox1 = new QSpinBox();
    m_groupBox10_SpinBox1->setRange(1, 255);
    m_groupBox10_SpinBox1->setValue(3);
    m_groupBox10_CheckBox2 = new QCheckBox(tr("Renumber"));
    QLabel *groupBox10_Label2 = new QLabel(tr("Start at:"));
    m_groupBox10_SpinBox2 = new ZeroOnEmptySpinBox();
    m_groupBox10_SpinBox2->setRange(0, 100000);
    m_groupBox10_SpinBox2->setValue(1);
    QLabel *groupBox10_Label3 = new QLabel(tr("Step:"));
    m_groupBox10_SpinBox3 = new QSpinBox();
    m_groupBox10_SpinBox3->setRange(1, 100000);
    m_groupBox10_SpinBox3->setValue(1);
    QGridLayout *groupBox10Layout = new QGridLayout();
    groupBox10Layout->addWidget(groupBox10_Label0,      0, 1, Qt::AlignRight | Qt::AlignVCenter);
    groupBox10Layout->addWidget(m_groupBox10_SpinBox0,  0, 2);
    groupBox10Layout->addWidget(m_groupBox10_CheckBox1, 1, 0);
    groupBox10Layout->addWidget(groupBox10_Label1,      1, 1, Qt::AlignRight | Qt::AlignVCenter);
    groupBox10Layout->addWidget(m_groupBox10_SpinBox1,  1, 2);
    groupBox10Layout->addWidget(m_groupBox10_CheckBox2, 2, 0);
    groupBox10Layout->addWidget(groupBox10_Label2,      2, 1, Qt::AlignRight | Qt::AlignVCenter);
    groupBox10Layout->addWidget(m_groupBox10_SpinBox2,  2, 2);
    groupBox10Layout->addWidget(groupBox10_Label3,      2, 3, Qt::AlignRight | Qt::AlignVCenter);
    groupBox10Layout->addWidget(m_groupBox10_SpinBox3,  2, 4);
    groupBox10Layout->setColumnStretch(0, 0);
    groupBox10Layout->setColumnStretch(1, 0);
    groupBox10Layout->setColumnStretch(2, 0);
    groupBox10Layout->setColumnStretch(3, 0);
    groupBox10Layout->setColumnStretch(4, 0);
    groupBox10Layout->setColumnStretch(5, 1);  // The very last column gets stretch 1
    m_groupBox10->setLayout(groupBox10Layout);

    QLabel *titleLabel11 = new QLabel(tr("Case"));
    titleLabel11->setStyleSheet("font-size: 12pt; margin-top: 10px;");
    m_groupBox11 = new QGroupBox("", this);
    m_groupBox11_ComboBox = new QComboBox();
    m_groupBox11_ComboBox->addItems({tr("Same"), tr("Lower"), tr("Upper"), tr("Title"), tr("Sentence")});
    QHBoxLayout *groupBox11Layout = new QHBoxLayout;
    groupBox11Layout->addWidget(m_groupBox11_ComboBox);
    m_groupBox11->setLayout(groupBox11Layout);

    m_groupBox12_CheckBox1 = new QCheckBox(tr("Files"));
    m_groupBox12_CheckBox1->setChecked(true);
    m_groupBox12_CheckBox2 = new QCheckBox(tr("Folders"));
    m_groupBox12_CheckBox2->setChecked(true);
    m_groupBox12_CheckBox3 = new QCheckBox(tr("Recursive"));
    m_groupBox12_CheckBox3->setChecked(false);
    QLabel *groupBox12_Label = new QLabel(tr("Filter:"));
    m_groupBox12_LineEdit = new QLineEdit();
    m_groupBox12_CheckBox4 = new QCheckBox(tr("Match case"));
    m_groupBox12_CheckBox4->setChecked(false);
    QHBoxLayout *groupBox12Layout = new QHBoxLayout();
    groupBox12Layout->setContentsMargins(10, 10, 10, 10);
    groupBox12Layout->setSpacing(10);
    groupBox12Layout->addWidget(m_groupBox12_CheckBox1);
    groupBox12Layout->addWidget(m_groupBox12_CheckBox2);
    groupBox12Layout->addWidget(m_groupBox12_CheckBox3);
    groupBox12Layout->addSpacing(32);
    groupBox12Layout->addWidget(groupBox12_Label);
    groupBox12Layout->addWidget(m_groupBox12_LineEdit);
    groupBox12Layout->addWidget(m_groupBox12_CheckBox4);

    QHBoxLayout *buttonBoxLayout = new QHBoxLayout;
    m_buttonDoReset = new QPushButton(tr("Reset Rules"), this);
    m_buttonDoReset->setMinimumHeight(50);
    m_buttonDoReset->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    m_buttonDoRename = new QPushButton(tr("Start batch"), this);
    m_buttonDoRename->setMinimumHeight(50);
    m_buttonDoRename->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    m_buttonDoRename->setEnabled(false);

    buttonBoxLayout->addWidget(m_buttonDoReset);
    buttonBoxLayout->addStretch();
    buttonBoxLayout->addWidget(m_buttonDoRename);

    QVBoxLayout *controlsLayout = new QVBoxLayout();
    controlsLayout->setContentsMargins(10, 10, 10, 10);
    controlsLayout->setSpacing(10);

    controlsLayout->addWidget(titleLabel13);
    controlsLayout->addWidget(m_groupBox13); // Apply To
    controlsLayout->addWidget(titleLabel3);
    controlsLayout->addWidget(m_groupBox3);  // Replace
    controlsLayout->addWidget(titleLabel5);
    controlsLayout->addWidget(m_groupBox5);  // Remove
    controlsLayout->addWidget(titleLabel7);
    controlsLayout->addWidget(m_groupBox7);  // Add
    controlsLayout->addWidget(titleLabel9);
    controlsLayout->addWidget(m_groupBox9);  // Add Folder Name
    controlsLayout->addWidget(titleLabel2);
    controlsLayout->addWidget(m_groupBox2);  // Add Numbering
    controlsLayout->addWidget(titleLabel10);
    controlsLayout->addWidget(m_groupBox10); // Number Padding
    controlsLayout->addWidget(titleLabel11);
    controlsLayout->addWidget(m_groupBox11); // Case
    controlsLayout->addStretch();
    controlsLayout->addLayout(buttonBoxLayout);

    // --------------------------------------------------------------------

    QVBoxLayout *rightSideLayout = new QVBoxLayout();
    rightSideLayout->addLayout(groupBox12Layout); // Filters
    rightSideLayout->addWidget(m_viewStack);

    m_mainLayout = new QHBoxLayout(m_centralWidget);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
    m_mainLayout->setSpacing(0);
    m_mainLayout->addLayout(controlsLayout,  0);
    m_mainLayout->addLayout(rightSideLayout, 1);

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

    connect(m_groupBox3_LineEdit1, &QLineEdit::textChanged, this, &MainWindow::validateInputBoxRegex);
    connect(m_buttonDoRename, &QPushButton::clicked, this, [this]() {
        QString errorMessage;
        if (!m_abstractModel->applyBatchRename(&errorMessage)) {
            QMessageBox::warning(this, tr("Batch rename"), errorMessage);
        }
        onRenameRulesChanged(); // force recalculation of newName
    });
    connect(m_abstractModel, &CustomTableModel::renameConflictsChanged, this, &MainWindow::updateRenameButtonState);

    // Name/New Name/Path are sized by their content: re-measure whenever their text can change
    // (folder loaded, rows added/removed, rename rules or rename selection changed, files renamed).
    // Listens to the proxy, not the source model: only visible rows count, and toggling hidden files
    // (beginFilterChange()/endFilterChange()) arrives here as rowsInserted/rowsRemoved.
    connect(m_proxyModel, &QAbstractItemModel::modelReset, this, &MainWindow::scheduleMeasureFlexibleColumns);
    connect(m_proxyModel, &QAbstractItemModel::rowsInserted, this, &MainWindow::scheduleMeasureFlexibleColumns);
    connect(m_proxyModel, &QAbstractItemModel::rowsRemoved, this, &MainWindow::scheduleMeasureFlexibleColumns);
    connect(m_proxyModel, &QAbstractItemModel::dataChanged, this,
            [this](const QModelIndex &topLeft, const QModelIndex &bottomRight, const QList<int> &roles) {
        auto touches = [&](int col) { return topLeft.column() <= col && col <= bottomRight.column(); };
        const bool touchesMeasuredText = touches(CustomTableModel::eColName)
                                      || touches(CustomTableModel::eColNewName)
                                      || touches(CustomTableModel::eColPath);
        if (touchesMeasuredText && (roles.isEmpty() || roles.contains(Qt::DisplayRole))) {
            scheduleMeasureFlexibleColumns();
        }
    });
    connect(m_buttonDoReset, &QPushButton::clicked, this, [this]() {
        onResetRules();
    });
    connect(m_tableView->selectionModel(), &QItemSelectionModel::selectionChanged, this, &MainWindow::updateRenameRowsFromSelection);

    // part of mitigation for Shift+Pos1 / Shift+End not working in tableView
    connect(m_tableView->selectionModel(), &QItemSelectionModel::currentChanged, this, &MainWindow::onTableCurrentChanged);

    setupRenameRuleSignals();

    onRenameRulesChanged(); // read out controls once and send rules to custom model
    showFolder(m_currentDirectory, externalPathList);
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

void MainWindow::showFolder(const QString &directoryPath, const QStringList &externalPathList) {
    if (!directoryPath.isEmpty()) {
        // Handle non-existing paths
        QDir dir(directoryPath);
        if (!dir.exists()) {
            return;
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
            return;
        }
    }

    auto *activeView = qobject_cast<QAbstractItemView*>(m_viewStack->currentWidget());

    // --- DISABLE SIGNALS FOR SPEEDUP ---
    if (m_selectionModel) {
        m_selectionModel->blockSignals(true);
    }

    // --- STORE FOCUS AND SELECTION ---
    QStringList selectedPathsToRestore;
    QString focusedPathToRestore;
    QString pathBelowLastSelectedToRestore;
    int lastSelectedProxyRow = -1;

    if (m_selectionModel) {
        if (externalPathList.isEmpty()) {
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
        } else {
            selectedPathsToRestore = externalPathList;
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

    if (m_selectionModel) {
        m_selectionModel->clear();
    }

    // --- LOAD NEW FOLDER ---
    m_abstractModel->populateModel_mkBatchRename(directoryPath, externalPathList);

    m_tableView->setRootIndex(QModelIndex());
    m_listView->setRootIndex(QModelIndex());
    m_thumbnailView->setRootIndex(QModelIndex());

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
            }
        }

        // Apply the focus
        if (proxyIndexToFocus.isValid() && activeView) {
            activeView->setCurrentIndex(proxyIndexToFocus);
        }
        // Apply the selection
        if (!restoreSelection.isEmpty() && m_selectionModel) {
            qDebug() << "restoreSelection:" << restoreSelection;
            m_selectionModel->blockSignals(false); // Need to do this here so signal QItemSelectionModel::selectionChanged gets processed and setRenameRows() gets called
            m_selectionModel->select(restoreSelection, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
            updateRenameRowsFromSelection();
        }
    }

    // --- RE-ENABLE SIGNALS ---
    if (m_selectionModel) {
        m_selectionModel->blockSignals(false);
    }

    // --- Update columns of tableView ---
    measureFlexibleColumns();
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

void MainWindow::updateRenameRowsFromSelection() {
    qDebug() << "updateRenameRowsFromSelection()";
    if (!m_selectionModel || !m_proxyModel || !m_abstractModel)
        return;

    QModelIndexList selectedProxyIndexes = m_selectionModel->selectedIndexes();

    std::sort(selectedProxyIndexes.begin(), selectedProxyIndexes.end(),
              [](const QModelIndex &a, const QModelIndex &b) {
                  return a.row() < b.row();
              });

    QList<int> orderedSourceRows;
    QSet<int> seenSourceRows;

    for (const QModelIndex &proxyIndex : std::as_const(selectedProxyIndexes)) {
        if (proxyIndex.column() == CustomTableModel::eColNewName) {
            QModelIndex sourceIndex = m_proxyModel->mapToSource(proxyIndex);
            int sourceRow = sourceIndex.row();

            if (!seenSourceRows.contains(sourceRow)) {
                seenSourceRows.insert(sourceRow);
                orderedSourceRows.append(sourceRow);
            }
        }
    }

    m_abstractModel->setRenameRows(seenSourceRows, orderedSourceRows);
    qDebug() << "updateRenameRowsFromSelection() orderedSourceRows:" << orderedSourceRows;
    updateRenameButtonState();
}

// The rename button is only enabled if rows are selected AND there is no name conflict
void MainWindow::updateRenameButtonState() {
    if (!m_buttonDoRename || !m_abstractModel) return;

    const int conflicts = m_abstractModel->renameConflictCount();
    m_buttonDoRename->setEnabled(m_abstractModel->hasRenameRows() && conflicts == 0);
    m_buttonDoRename->setToolTip(conflicts > 0
        ? tr("Renaming is blocked: %n name conflict(s), see the entries marked red.", nullptr, conflicts)
        : QString());
}

// Coalesces bursts of model signals (e.g. one dataChanged per keystroke plus a modelReset) into a single measurement.
void MainWindow::scheduleMeasureFlexibleColumns() {
    if (m_measureFlexibleColumnsPending) {
        return;
    }
    m_measureFlexibleColumnsPending = true;
    QTimer::singleShot(0, this, [this]() {
        m_measureFlexibleColumnsPending = false;
        measureFlexibleColumns();
        if (m_viewStack->currentWidget() == m_tableView) {
            updateColumns();
        }
    });
}

// Measures the content width of Name, New Name and Path (same approach as mkFileSearch's measureFlexibleColumns()).
void MainWindow::measureFlexibleColumns() {
    m_nameColWidthNeeded = 0;
    m_newNameColWidthNeeded = 0;
    m_pathColWidthNeeded = 0;

    const int rowCount = m_proxyModel->rowCount();
    if (rowCount == 0) {
        return;
    }

    // Prefilter by character count (cheap), then measure pixel widths only for the longest candidates.
    // With a proportional font the longest string is not always the widest, but it is practically always among the top N.
    constexpr int kCandidates = 64;

    auto widestText = [&](int column) -> int {
        std::vector<std::pair<qsizetype, int>> lengths;   // (length, row)
        lengths.reserve(rowCount);
        for (int row = 0; row < rowCount; ++row) {
            lengths.emplace_back(m_proxyModel->index(row, column).data(Qt::DisplayRole).toString().size(), row);
        }

        const auto nth = lengths.begin() + std::min<qsizetype>(kCandidates, static_cast<qsizetype>(lengths.size()));
        std::partial_sort(lengths.begin(), nth, lengths.end(), [](const auto &a, const auto &b) { return a.first > b.first; });

        const QFontMetrics fm(m_tableView->font());
        int maxWidth = 0;
        for (auto it = lengths.begin(); it != nth; ++it) {
            const QString text = m_proxyModel->index(it->second, column).data(Qt::DisplayRole).toString();
            maxWidth = std::max(maxWidth, fm.horizontalAdvance(text));
        }
        return maxWidth;
    };

    // Let the style add icon, spacing and margins exactly like QStyledItemDelegate::sizeHint() would.
    QStyle *style = m_tableView->style();
    const int iconExtent = style->pixelMetric(QStyle::PM_SmallIconSize, nullptr, m_tableView);

    auto cellWidth = [&](int textWidth, bool hasIcon) -> int {
        QStyleOptionViewItem opt;
        opt.initFrom(m_tableView);
        opt.widget = m_tableView;
        opt.font = m_tableView->font();
        opt.fontMetrics = QFontMetrics(opt.font);
        opt.features = QStyleOptionViewItem::HasDisplay;
        if (hasIcon) {
            opt.features |= QStyleOptionViewItem::HasDecoration;
            opt.decorationSize = m_tableView->iconSize().isValid() ? m_tableView->iconSize() : QSize(iconExtent, iconExtent);
        }
        // Ask the style for a one-space cell and subtract the space: what remains is icon + spacing + margins.
        opt.text = QStringLiteral(" ");
        const QSize spaceSize = style->sizeFromContents(QStyle::CT_ItemViewItem, &opt, QSize(), m_tableView);
        const int overhead = spaceSize.width() - opt.fontMetrics.horizontalAdvance(opt.text);
        return textWidth + overhead + (m_tableView->showGrid() ? 1 : 0);
    };

    m_nameColWidthNeeded = cellWidth(widestText(CustomTableModel::eColName), true);
    m_newNameColWidthNeeded = cellWidth(widestText(CustomTableModel::eColNewName), false);
    m_pathColWidthNeeded = cellWidth(widestText(CustomTableModel::eColPath), false) + 8;   // +8: buffer subtracted by TableItemDelegate::paint() before eliding
}

void MainWindow::updateColumns() {
    if (!m_bHeaderVisible) {
        return;
    }

    QHeaderView *header = m_tableView->horizontalHeader();
    const int nameCol = CustomTableModel::eColName;
    const int newNameCol = CustomTableModel::eColNewName;
    const int pathCol = CustomTableModel::eColPath;

    auto isFlexible = [&](int col) { return col == nameCol || col == newNameCol || col == pathCol; };

    // Fixed columns: exact content width (Qt measures them).
    for (int col = 0; col < m_abstractModel->columnCount(); ++col) {
        header->setSectionResizeMode(col, isFlexible(col) ? QHeaderView::Interactive : QHeaderView::ResizeToContents);
    }
    header->doItemsLayout();

    int fixedSum = 0;
    for (int col = 0; col < m_abstractModel->columnCount(); ++col) {
        if (!isFlexible(col) && !header->isSectionHidden(col)) {
            fixedSum += header->sectionSize(col);
        }
    }

    header->setSectionResizeMode(QHeaderView::Interactive);

    constexpr int kMinPathWidth = 100;

    // Content width, but never narrower than the header label (incl. sort indicator).
    // With an empty table nothing is measured (0), so the header label alone decides.
    auto need = [&](int measured, int col) { return std::max(measured, header->sectionSizeHint(col)); };
    const int nameNeed    = need(m_nameColWidthNeeded,    nameCol);
    const int newNameNeed = need(m_newNameColWidthNeeded, newNameCol);
    const int pathNeed    = need(m_pathColWidthNeeded,    pathCol);

    const int available = m_tableView->viewport()->width() - fixedSum;

    // Name always gets exactly its content width.
    // Path gets its content width, but shrinks (eliding in the middle) down to its minimum when space is short.
    // New Name gets all the rest, but never less than its content width; if even that does not fit,
    // the table gets a horizontal scrollbar instead of truncating Name or New Name.
    const int nameWidth = nameNeed;
    const int pathFloor = std::min(pathNeed, kMinPathWidth);
    const int pathWidth = std::min(pathNeed, std::max(pathFloor, available - nameNeed - newNameNeed));
    const int newNameWidth = std::max(newNameNeed, available - nameWidth - pathWidth);

    m_tableView->setColumnWidth(nameCol, nameWidth);
    m_tableView->setColumnWidth(newNameCol, newNameWidth);
    m_tableView->setColumnWidth(pathCol, pathWidth);
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
        mainMenu.addAction(m_actionListViewFileProperties);
    } else {
        QStringList selectedPaths = getActiveViewPathList(); // Used by KDE SERVICE-ACTIONS

        const KService::Ptr openService = updateOpenActionText(selectedPaths);
        mainMenu.addAction(m_actionListViewOpenFiles);
        mainMenu.setDefaultAction(m_actionListViewOpenFiles);

        // Single executable file (program, script): offer to run it in a terminal window to interact with it
        if (selectedPaths.size() == 1 && Helpers::allExecutable(selectedPaths)) {
            mainMenu.addAction(m_actionListViewRunInTerminal);
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
        return;
    }
    if (!fileInfo.exists()) return;

    Helpers::openFileWithPreferredApp(fileInfo.absoluteFilePath(), (modifiers & Qt::ShiftModifier) ? 2 : 1);
}

// Labels the "Open" action like Dolphin: "Open with <app>" if all selected files are opened with the same application.
// Returns that application, or nullptr.
KService::Ptr MainWindow::updateOpenActionText(const QStringList &pathList) {
    const KService::Ptr service = Helpers::openActionService(pathList);
    if (Helpers::allExecutable(pathList)) {
        m_actionListViewOpenFiles->setText(tr("Run"));
        m_actionListViewOpenFiles->setIcon(QIcon::fromTheme("system-run"));
    } else if (service) {
        m_actionListViewOpenFiles->setText(tr("Open with %1").arg(service->name().replace('&', QLatin1String("&&"))));
        m_actionListViewOpenFiles->setIcon(QIcon::fromTheme(service->icon()));
    } else {
        m_actionListViewOpenFiles->setText(tr("Open"));
        m_actionListViewOpenFiles->setIcon(QIcon());
    }
    return service;
}

void MainWindow::action_ListViewRunInTerminal() {
    const QStringList pathList = getActiveViewPathList();
    if (pathList.size() != 1) return;

    if (!Helpers::executeInTerminal(pathList.first())) {
        QMessageBox::warning(this, tr("Run in Terminal"), tr("No terminal emulator could be found or started."));
    }
}

void MainWindow::action_ListViewOpenFiles() {
    QStringList pathList = getActiveViewPathList();
    if (pathList.isEmpty()) return;

    QList<QUrl> urlsToOpen;

    for (const QString &path : std::as_const(pathList)) {
        QFileInfo fileInfo(path);
        if (!fileInfo.exists()) continue;

        switch (Helpers::openMethod(fileInfo)) {
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

// We don't trigger an update of the view. The user should do it manually
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

void MainWindow::updateWidgetStyles() {

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

        m_groupBox2->setStyleSheet(Styles::groupBox);
        m_groupBox3->setStyleSheet(Styles::groupBox);
        m_groupBox5->setStyleSheet(Styles::groupBox);
        m_groupBox7->setStyleSheet(Styles::groupBox);
        m_groupBox9->setStyleSheet(Styles::groupBox);
        m_groupBox10->setStyleSheet(Styles::groupBox);
        m_groupBox11->setStyleSheet(Styles::groupBox);
        m_groupBox13->setStyleSheet(Styles::groupBox);
    } else {
        // IMPORTANT: Clear the stylesheet when the system switches to light mode!
        // This makes Qt switch back to the light default Breeze design.
        this->setStyleSheet("");

        m_groupBox2->setStyleSheet(Styles::groupBox);
        m_groupBox3->setStyleSheet(Styles::groupBox);
        m_groupBox5->setStyleSheet(Styles::groupBox);
        m_groupBox7->setStyleSheet(Styles::groupBox);
        m_groupBox9->setStyleSheet(Styles::groupBox);
        m_groupBox10->setStyleSheet(Styles::groupBox);
        m_groupBox11->setStyleSheet(Styles::groupBox);
        m_groupBox13->setStyleSheet(Styles::groupBox);
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
// Protected Overrides

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
            if (keyEvent->key() == Qt::Key_F) {
                Helpers::openFileListWithHandler(m_settings.searchTool, { m_currentDirectory });
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
        }
        else if (keyEvent->key() == Qt::Key_F5) {
            showFolder(m_currentDirectory, QStringList());
            return true;
        }

        // If an editor is currently open in the view (e.g. renaming a file),
        // we must NOT intercept keys like "Del", "Backspace" or "Enter"!
        if (qobject_cast<QLineEdit*>(targetWidget)) {
            return QObject::eventFilter(obj, event);
        }
        else if (qobject_cast<QLineEdit*>(targetWidget->parent())) {
            return QObject::eventFilter(obj, event);
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

void MainWindow::validateInputBoxRegex() {
    if (m_groupBox3_CheckBox2->isChecked()) {
        QString searchString = m_groupBox3_LineEdit1->text();

        if (searchString.isEmpty()) {
            m_groupBox3_LineEdit1->setStyleSheet(m_styleLineEditNormal);
        } else {
            QRegularExpression re1(searchString);

            if (!re1.isValid()) {
                m_groupBox3_LineEdit1->setStyleSheet(m_styleLineEditError);
            } else {
                m_groupBox3_LineEdit1->setStyleSheet(m_styleLineEditNormal);
            }
        }
    } else {
        m_groupBox3_LineEdit1->setStyleSheet(m_styleLineEditNormal);
    }
}

void MainWindow::setupRenameRuleSignals() {
    // Apply To
    connect(m_groupBox13_RadioButton1, &QRadioButton::toggled, this, &MainWindow::onRenameRulesChanged);
    connect(m_groupBox13_RadioButton2, &QRadioButton::toggled, this, &MainWindow::onRenameRulesChanged);
    connect(m_groupBox13_RadioButton3, &QRadioButton::toggled, this, &MainWindow::onRenameRulesChanged);

    // Replace
    connect(m_groupBox3_LineEdit1, &QLineEdit::textChanged, this, &MainWindow::onRenameRulesChanged);
    connect(m_groupBox3_LineEdit2, &QLineEdit::textChanged, this, &MainWindow::onRenameRulesChanged);
    connect(m_groupBox3_CheckBox1, &QCheckBox::checkStateChanged, this, &MainWindow::onRenameRulesChanged);
    connect(m_groupBox3_CheckBox2, &QCheckBox::checkStateChanged, this, [this]() {validateInputBoxRegex(); onRenameRulesChanged();});

    // Remove
    connect(m_groupBox5_SpinBox1, &QSpinBox::valueChanged, this, &MainWindow::onRenameRulesChanged);
    connect(m_groupBox5_SpinBox2, &QSpinBox::valueChanged, this, &MainWindow::onRenameRulesChanged);
    connect(m_groupBox5_SpinBox3, &QSpinBox::valueChanged, this, &MainWindow::onRenameRulesChanged);
    connect(m_groupBox5_SpinBox4, &QSpinBox::valueChanged, this, &MainWindow::onRenameRulesChanged);

    // Add
    connect(m_groupBox7_LineEdit1, &QLineEdit::textChanged, this, &MainWindow::onRenameRulesChanged);
    connect(m_groupBox7_LineEdit2, &QLineEdit::textChanged, this, &MainWindow::onRenameRulesChanged);
    connect(m_groupBox7_SpinBox,   &QSpinBox::valueChanged, this, &MainWindow::onRenameRulesChanged);
    connect(m_groupBox7_LineEdit3, &QLineEdit::textChanged, this, &MainWindow::onRenameRulesChanged);

    // Add Numbering
    connect(m_groupBox2_ComboBox,  &QComboBox::currentIndexChanged, this, &MainWindow::onRenameRulesChanged);
    connect(m_groupBox2_SpinBox1,  &QSpinBox::valueChanged, this, &MainWindow::onRenameRulesChanged);
    connect(m_groupBox2_SpinBox2,  &QSpinBox::valueChanged, this, &MainWindow::onRenameRulesChanged);
    connect(m_groupBox2_LineEdit,  &QLineEdit::textChanged, this, &MainWindow::onRenameRulesChanged);

    // Append Folder Name
    connect(m_groupBox9_ComboBox, &QComboBox::currentIndexChanged, this, &MainWindow::onRenameRulesChanged);
    connect(m_groupBox9_LineEdit, &QLineEdit::textChanged, this, &MainWindow::onRenameRulesChanged);
    connect(m_groupBox9_SpinBox,  &QSpinBox::valueChanged, this, &MainWindow::onRenameRulesChanged);

    // Number Padding
    connect(m_groupBox10_SpinBox0,  &QSpinBox::valueChanged, this, &MainWindow::onRenameRulesChanged);
    connect(m_groupBox10_CheckBox1, &QCheckBox::checkStateChanged, this, &MainWindow::onRenameRulesChanged);
    connect(m_groupBox10_SpinBox1,  &QSpinBox::valueChanged, this, &MainWindow::onRenameRulesChanged);
    connect(m_groupBox10_CheckBox2, &QCheckBox::checkStateChanged, this, &MainWindow::onRenameRulesChanged);
    connect(m_groupBox10_SpinBox2,  &QSpinBox::valueChanged, this, &MainWindow::onRenameRulesChanged);
    connect(m_groupBox10_SpinBox3,  &QSpinBox::valueChanged, this, &MainWindow::onRenameRulesChanged);

    // Case
    connect(m_groupBox11_ComboBox, &QComboBox::currentIndexChanged, this, &MainWindow::onRenameRulesChanged);

    // Filters
    connect(m_groupBox12_CheckBox1, &QCheckBox::checkStateChanged, this, &MainWindow::onRenameRulesChanged);
    connect(m_groupBox12_CheckBox2, &QCheckBox::checkStateChanged, this, &MainWindow::onRenameRulesChanged);
    connect(m_groupBox12_CheckBox3, &QCheckBox::checkStateChanged, this, &MainWindow::onRenameRulesChanged);
    connect(m_groupBox12_CheckBox4, &QCheckBox::checkStateChanged, this, &MainWindow::onRenameRulesChanged);
    connect(m_groupBox12_LineEdit,  &QLineEdit::textChanged, this, &MainWindow::onRenameRulesChanged);
}

void MainWindow::onRenameRulesChanged() {
    RenameRules rules;

    rules.target = m_groupBox13_RadioButton1->isChecked() ? 0 : m_groupBox13_RadioButton2->isChecked() ? 1 : 2;

    // Replace
    rules.rpl.match = m_groupBox3_LineEdit1->text();
    rules.rpl.replace = m_groupBox3_LineEdit2->text();
    rules.rpl.matchCase = m_groupBox3_CheckBox1->isChecked();
    rules.rpl.regex = m_groupBox3_CheckBox2->isChecked();

    // Remove
    rules.rmv.firstN = m_groupBox5_SpinBox1->value();
    rules.rmv.lastN = m_groupBox5_SpinBox2->value();
    rules.rmv.fromN = m_groupBox5_SpinBox3->value();
    rules.rmv.fromNcount = m_groupBox5_SpinBox4->value();

    // Add
    rules.add.prefix = m_groupBox7_LineEdit1->text();
    rules.add.insertText = m_groupBox7_LineEdit2->text();
    rules.add.insertPos = m_groupBox7_SpinBox->value();
    rules.add.suffix = m_groupBox7_LineEdit3->text();

    // Add Numbering
    rules.num.mode = m_groupBox2_ComboBox->currentIndex();
    rules.num.start = m_groupBox2_SpinBox1->value();
    rules.num.step = m_groupBox2_SpinBox2->value();
    rules.num.separator = m_groupBox2_LineEdit->text();

    // Add Folder Name
    rules.afn.mode = m_groupBox9_ComboBox->currentIndex();
    rules.afn.separator = m_groupBox9_LineEdit->text();
    rules.afn.levels = m_groupBox9_SpinBox->value();

    // Number Manipulation
    rules.man.targetGroup = m_groupBox10_SpinBox0->value();
    rules.man.addLeadEnabled = m_groupBox10_CheckBox1->isChecked();
    rules.man.addLeadCount = m_groupBox10_SpinBox1->value();
    rules.man.newEnabled = m_groupBox10_CheckBox2->isChecked();
    rules.man.newStart = m_groupBox10_SpinBox2->value();
    rules.man.newStep = m_groupBox10_SpinBox3->value();

    // Case
    rules.cas.mode = m_groupBox11_ComboBox->currentIndex();

    // Filters
    rules.flt.showFiles = m_groupBox12_CheckBox1->isChecked();
    rules.flt.showFolders = m_groupBox12_CheckBox2->isChecked();
    rules.flt.showRecursive = m_groupBox12_CheckBox3->isChecked();
    rules.flt.filter = m_groupBox12_LineEdit->text();
    rules.flt.matchCase = m_groupBox12_CheckBox4->isChecked();

    // Pass bundled settings to the model
    m_abstractModel->setRenameRules(rules);
}

void MainWindow::onResetRules() {
    // List of all affected controls for blocking signals
    const QList<QWidget*> controls = {
        m_groupBox13_RadioButton1, m_groupBox13_RadioButton2, m_groupBox13_RadioButton3,
        m_groupBox3_LineEdit1, m_groupBox3_LineEdit2, m_groupBox3_CheckBox1, m_groupBox3_CheckBox2,
        m_groupBox5_SpinBox1, m_groupBox5_SpinBox2, m_groupBox5_SpinBox3, m_groupBox5_SpinBox4,
        m_groupBox7_LineEdit1, m_groupBox7_LineEdit2, m_groupBox7_SpinBox, m_groupBox7_LineEdit3,
        m_groupBox2_ComboBox, m_groupBox2_SpinBox1, m_groupBox2_SpinBox2, m_groupBox2_LineEdit,
        m_groupBox9_ComboBox, m_groupBox9_SpinBox, m_groupBox9_LineEdit,
        m_groupBox10_SpinBox0, m_groupBox10_CheckBox1, m_groupBox10_SpinBox1,
        m_groupBox10_CheckBox2, m_groupBox10_SpinBox2, m_groupBox10_SpinBox3,
        m_groupBox11_ComboBox
    };

    // 1. Block signals for all controls
    for (QWidget *widget : controls) {
        widget->blockSignals(true);
    }

    // --------------------------------------------------------------------
    // 2. Reset to default values
    // --------------------------------------------------------------------

    // Apply To (Standard: Name)
    m_groupBox13_RadioButton1->setChecked(true);

    // Replace
    m_groupBox3_LineEdit1->clear();
    m_groupBox3_LineEdit2->clear();
    m_groupBox3_CheckBox1->setChecked(true);  // Match Case
    m_groupBox3_CheckBox2->setChecked(false); // RegEx

    // Remove
    m_groupBox5_SpinBox1->setValue(0); // First
    m_groupBox5_SpinBox2->setValue(0); // Last
    m_groupBox5_SpinBox3->setValue(0); // From
    m_groupBox5_SpinBox4->setValue(0); // To

    // Add
    m_groupBox7_LineEdit1->clear(); // Prefix
    m_groupBox7_LineEdit2->clear(); // Insert
    m_groupBox7_SpinBox->setValue(0);  // Position
    m_groupBox7_LineEdit3->clear(); // Suffix

    // Add Numbering
    m_groupBox2_ComboBox->setCurrentIndex(0); // None
    m_groupBox2_SpinBox1->setValue(1);        // Start at
    m_groupBox2_SpinBox2->setValue(1);        // Step
    m_groupBox2_LineEdit->setText("_");       // Separator

    // Add Folder Name
    m_groupBox9_ComboBox->setCurrentIndex(0); // None
    m_groupBox9_SpinBox->setValue(1);        // Levels
    m_groupBox9_LineEdit->setText("_");       // Separator

    // Number Manipulation
    m_groupBox10_SpinBox0->setValue(1);        // Group
    m_groupBox10_CheckBox1->setChecked(false); // Leading Zeroes
    m_groupBox10_SpinBox1->setValue(3);        // Digits
    m_groupBox10_CheckBox2->setChecked(false); // Renumber
    m_groupBox10_SpinBox2->setValue(1);        // Start at
    m_groupBox10_SpinBox3->setValue(1);        // Step

    // Case
    m_groupBox11_ComboBox->setCurrentIndex(0); // Same

    // --------------------------------------------------------------------
    // 3. Lift the signal blocking
    // --------------------------------------------------------------------
    for (QWidget *widget : controls) {
        widget->blockSignals(false);
    }

    // 4. Call once manually to apply the new (default) rules to the model
    onRenameRulesChanged();
}
