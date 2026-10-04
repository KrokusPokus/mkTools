#include "mainwindow.h"
#include "customitemdelegates.h"
#include "customproxystyle.h"
#include "filepropertiesdialog.h"
#include "stylesheets.h"

#include <QActionGroup>
#include <QApplication>
#include <QBuffer>
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
#include <QRegularExpression>
#include <QScrollBar>
#include <QSettings>
#include <QSharedMemory>  // for handing over a file list in fileOperation()
#include <QShortcut>
#include <QSize>
#include <QStandardPaths>
#include <QStringList>
#include <QStringView>
#include <QStyleHints>
#include <QThread>
#include <QUrl>
#include <QUuid>

#include <algorithm> // Für std::reverse
#include <utility> // Für std::as_const

#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <shellapi.h>
#include <shlobj.h>
#elif defined(Q_OS_LINUX)
#include <KFileItem>
#include <KFileItemListProperties>
#include <KFileItemActions>
#include <KApplicationTrader>
#include <KService>
#include <KIO/ApplicationLauncherJob>
#include <KOpenWithDialog>
#endif

MainWindow::MainWindow(QString targetDirectory, QStringList externalPathList, QWidget *parent)
    : QMainWindow(parent)
    , m_currentDirectory(std::move(targetDirectory))
{
    if (!m_currentDirectory.isEmpty()) {
        setWindowTitle(QDir::toNativeSeparators(m_currentDirectory));
    } else {
        setWindowTitle(QDir::toNativeSeparators("mkBatchRename"));
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
    m_tableView->setSortingEnabled(true);   // Unlike with TableWidget, this doesn't need turning on und off for speed optimization.
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

    m_groupBox13 = new QGroupBox("Which part", this);
    m_groupBox13_RadioButton1 = new QRadioButton("File Name");
    m_groupBox13_RadioButton2 = new QRadioButton("File Extension");
    m_groupBox13_RadioButton3 = new QRadioButton("Full Name");
    m_groupBox13_RadioButton1->setChecked(true);
    QHBoxLayout *groupBox13Layout = new QHBoxLayout();
    groupBox13Layout->setContentsMargins(50, 7, 7, 7); // (Links, Oben, Rechts, Unten)
    groupBox13Layout->addWidget(m_groupBox13_RadioButton1);
    groupBox13Layout->addWidget(m_groupBox13_RadioButton2);
    groupBox13Layout->addWidget(m_groupBox13_RadioButton3);
    groupBox13Layout->addStretch();
    m_groupBox13->setLayout(groupBox13Layout);

    m_groupBox1 = new QGroupBox("RegEx", this);
    m_groupBox1->setCheckable(true);
    QLabel *groupBox1_Label1 = new QLabel(tr("Match"));
    m_groupBox1_LineEdit1 = new QLineEdit();
    QLabel *groupBox1_Label2 = new QLabel(tr("Replace"));
    m_groupBox1_LineEdit2 = new QLineEdit();
    QGridLayout *groupBox1Layout = new QGridLayout;
    groupBox1Layout->setContentsMargins(50, 7, 7, 7); // (Links, Oben, Rechts, Unten)
    groupBox1Layout->addWidget(groupBox1_Label1, 1, 0);
    groupBox1Layout->addWidget(m_groupBox1_LineEdit1, 1, 1);
    groupBox1Layout->addWidget(groupBox1_Label2, 2, 0);
    groupBox1Layout->addWidget(m_groupBox1_LineEdit2, 2, 1);
    m_groupBox1->setLayout(groupBox1Layout);

    m_groupBox3 = new QGroupBox("Replace", this);
    m_groupBox3->setCheckable(true);
    QLabel *groupBox3_Label1 = new QLabel(tr("Replace"));
    m_groupBox3_LineEdit1 = new QLineEdit();
    m_groupBox3_CheckBox = new QCheckBox(tr("Match Case"));
    m_groupBox3_CheckBox->setChecked(false);
    QLabel *groupBox3_Label2 = new QLabel(tr("With"));
    m_groupBox3_LineEdit2 = new QLineEdit();
    QGridLayout *groupBox3Layout = new QGridLayout();
    groupBox3Layout->setContentsMargins(50, 7, 7, 7); // (Links, Oben, Rechts, Unten)
    groupBox3Layout->addWidget(groupBox3_Label1, 1, 0);
    groupBox3Layout->addWidget(m_groupBox3_LineEdit1, 1, 1);
    groupBox3Layout->addWidget(m_groupBox3_CheckBox, 1, 2);
    groupBox3Layout->addWidget(groupBox3_Label2, 2, 0);
    groupBox3Layout->addWidget(m_groupBox3_LineEdit2, 2, 1, 1, 2);
    m_groupBox3->setLayout(groupBox3Layout);

    m_groupBox5 = new QGroupBox("Remove", this);
    m_groupBox5->setCheckable(true);
    QLabel *groupBox5_Label1 = new QLabel(tr("First"));
    m_groupBox5_SpinBox1 = new ZeroOnEmptySpinBox();
    m_groupBox5_SpinBox1->setRange(0, 255);
    QLabel *groupBox5_Label2 = new QLabel(tr("Last"));
    m_groupBox5_SpinBox2 = new ZeroOnEmptySpinBox();
    m_groupBox5_SpinBox2->setRange(0, 255);
    QLabel *groupBox5_Label3 = new QLabel(tr("From"));
    m_groupBox5_SpinBox3 = new ZeroOnEmptySpinBox();
    m_groupBox5_SpinBox3->setRange(0, 255);
    QLabel *groupBox5_Label4 = new QLabel(tr("to"));
    m_groupBox5_SpinBox4 = new ZeroOnEmptySpinBox();
    m_groupBox5_SpinBox4->setRange(0, 255);
    QHBoxLayout *groupBox5Layout = new QHBoxLayout();
    groupBox5Layout->setContentsMargins(50, 7, 7, 7); // (Links, Oben, Rechts, Unten)
    groupBox5Layout->addWidget(groupBox5_Label1);
    groupBox5Layout->addWidget(m_groupBox5_SpinBox1);
    groupBox5Layout->addSpacing(15);
    groupBox5Layout->addWidget(groupBox5_Label3);
    groupBox5Layout->addWidget(m_groupBox5_SpinBox3);
    groupBox5Layout->addWidget(groupBox5_Label4);
    groupBox5Layout->addWidget(m_groupBox5_SpinBox4);
    groupBox5Layout->addSpacing(15);
    groupBox5Layout->addWidget(groupBox5_Label2);
    groupBox5Layout->addWidget(m_groupBox5_SpinBox2);
    groupBox5Layout->addStretch();
    m_groupBox5->setLayout(groupBox5Layout);

    m_groupBox6 = new QGroupBox("Move/Copy", this);
    m_groupBox6->setCheckable(true);
    m_groupBox6_ComboBox1 = new QComboBox();
    m_groupBox6_ComboBox1->addItems({"None", "Copy first n", "Copy last n", "Move first n", "Move last n"});
    m_groupBox6_SpinBox1 = new ZeroOnEmptySpinBox();
    m_groupBox6_SpinBox1->setRange(0, 255);
    QLabel *groupBox6_Label1 = new QLabel(tr("to"));
    m_groupBox6_ComboBox2 = new QComboBox();
    m_groupBox6_ComboBox2->addItems({"None", "To start", "To end", "To pos."});
    m_groupBox6_SpinBox2 = new ZeroOnEmptySpinBox();
    m_groupBox6_SpinBox2->setRange(0, 255);
    QLabel *groupBox6_Label2 = new QLabel(tr("Sep."));
    m_groupBox6_LineEdit = new QLineEdit();
    m_groupBox6_LineEdit->setFixedWidth(24);
    m_groupBox6_LineEdit->setText("_");
    QHBoxLayout *groupBox6Layout = new QHBoxLayout;
    groupBox6Layout->setContentsMargins(50, 7, 7, 7); // (Links, Oben, Rechts, Unten)
    groupBox6Layout->addWidget(m_groupBox6_ComboBox1);
    groupBox6Layout->addWidget(m_groupBox6_SpinBox1);
    groupBox6Layout->addWidget(groupBox6_Label1);
    groupBox6Layout->addWidget(m_groupBox6_ComboBox2);
    groupBox6Layout->addWidget(m_groupBox6_SpinBox2);
    groupBox6Layout->addWidget(groupBox6_Label2);
    groupBox6Layout->addWidget(m_groupBox6_LineEdit);
    groupBox6Layout->addStretch();
    m_groupBox6->setLayout(groupBox6Layout);

    m_groupBox7 = new QGroupBox("Add", this);
    m_groupBox7->setCheckable(true);
    QLabel *groupBox7_Label1 = new QLabel(tr("Prefix"));
    m_groupBox7_LineEdit1 = new QLineEdit();
    QLabel *groupBox7_Label2 = new QLabel(tr("Insert"));
    m_groupBox7_LineEdit2 = new QLineEdit();
    QLabel *groupBox7_Label3 = new QLabel(tr("at pos."));
    m_groupBox7_SpinBox = new ZeroOnEmptySpinBox();
    m_groupBox7_SpinBox->setRange(0, 255);
    QLabel *groupBox7_Label4 = new QLabel(tr("Suffix"));
    m_groupBox7_LineEdit3 = new QLineEdit();
    QGridLayout *groupBox7Layout = new QGridLayout();
    groupBox7Layout->setContentsMargins(50, 7, 7, 7); // (Links, Oben, Rechts, Unten)
    groupBox7Layout->addWidget(groupBox7_Label1, 0, 0);
    groupBox7Layout->addWidget(m_groupBox7_LineEdit1, 0, 1, 1, 3);
    groupBox7Layout->addWidget(groupBox7_Label2, 1, 0);
    groupBox7Layout->addWidget(m_groupBox7_LineEdit2, 1, 1);
    groupBox7Layout->addWidget(groupBox7_Label3, 1, 2);
    groupBox7Layout->addWidget(m_groupBox7_SpinBox, 1, 3);
    groupBox7Layout->addWidget(groupBox7_Label4, 2, 0);
    groupBox7Layout->addWidget(m_groupBox7_LineEdit3, 2, 1, 1, 3);
    m_groupBox7->setLayout(groupBox7Layout);

    m_groupBox2 = new QGroupBox("Add Numbering", this);
    m_groupBox2->setCheckable(true);
    m_groupBox2_ComboBox = new QComboBox();
    m_groupBox2_ComboBox->addItems({"None", "Prefix", "Suffix"});
    QLabel *groupBox2_Label1 = new QLabel(tr("Start:"));
    m_groupBox2_SpinBox1 = new ZeroOnEmptySpinBox();
    m_groupBox2_SpinBox1->setRange(0, 100000);
    m_groupBox2_SpinBox1->setValue(1);
    QLabel *groupBox2_Label2 = new QLabel(tr("Step:"));
    m_groupBox2_SpinBox2 = new QSpinBox();
    m_groupBox2_SpinBox2->setRange(1, 100000);
    m_groupBox2_SpinBox2->setValue(1);
    QLabel *groupBox2_Label3 = new QLabel(tr("Sep."));
    m_groupBox2_LineEdit = new QLineEdit();
    m_groupBox2_LineEdit->setFixedWidth(24);
    m_groupBox2_LineEdit->setText("_");
    QHBoxLayout *groupBox2Layout = new QHBoxLayout;
    groupBox2Layout->setContentsMargins(50, 7, 7, 7); // (Links, Oben, Rechts, Unten)
    groupBox2Layout->addWidget(m_groupBox2_ComboBox);
    groupBox2Layout->addWidget(groupBox2_Label1);
    groupBox2Layout->addWidget(m_groupBox2_SpinBox1);
    groupBox2Layout->addWidget(groupBox2_Label2);
    groupBox2Layout->addWidget(m_groupBox2_SpinBox2);
    groupBox2Layout->addWidget(groupBox2_Label3);
    groupBox2Layout->addWidget(m_groupBox2_LineEdit);
    groupBox2Layout->addStretch();
    m_groupBox2->setLayout(groupBox2Layout);

    /*
    m_groupBox8 = new QGroupBox("Add Date", this);
    m_groupBox8->setCheckable(true);
    QVBoxLayout *groupBox8Layout = new QVBoxLayout;
    groupBox8Layout->setContentsMargins(50, 7, 7, 7); // (Links, Oben, Rechts, Unten)
    m_groupBox8->setLayout(groupBox8Layout);
    */

    m_groupBox9 = new QGroupBox("Add Folder Name", this);
    m_groupBox9->setCheckable(true);
    m_groupBox9_ComboBox = new QComboBox();
    m_groupBox9_ComboBox->addItems({"None", "Prefix", "Suffix"});
    QLabel *groupBox9_Label1 = new QLabel(tr("Sep."));
    m_groupBox9_LineEdit = new QLineEdit();
    m_groupBox9_LineEdit->setFixedWidth(24);
    m_groupBox9_LineEdit->setText("_");
    QLabel *groupBox9_Label2 = new QLabel(tr("Levels"));
    m_groupBox9_SpinBox = new QSpinBox();
    m_groupBox9_SpinBox->setRange(1, 255);
    m_groupBox9_SpinBox->setValue(1);
    QHBoxLayout *groupBox9Layout = new QHBoxLayout;
    groupBox9Layout->setContentsMargins(50, 7, 7, 7); // (Links, Oben, Rechts, Unten)
    groupBox9Layout->addWidget(m_groupBox9_ComboBox);
    groupBox9Layout->addWidget(groupBox9_Label2);
    groupBox9Layout->addWidget(m_groupBox9_SpinBox);
    groupBox9Layout->addWidget(groupBox9_Label1);
    groupBox9Layout->addWidget(m_groupBox9_LineEdit);
    groupBox9Layout->addStretch();
    m_groupBox9->setLayout(groupBox9Layout);

    m_groupBox10 = new QGroupBox("Number Padding", this);
    m_groupBox10->setCheckable(true);
    m_groupBox10_CheckBox1 = new QCheckBox("Add lead:");
    m_groupBox10_LineEdit = new QLineEdit();
    m_groupBox10_LineEdit->setText("0");
    m_groupBox10_LineEdit->setMaxLength(1);
    m_groupBox10_LineEdit->setFixedWidth(24);
    QLabel *groupBox10_Label1 = new QLabel(tr("Digits:"));
    m_groupBox10_SpinBox1 = new QSpinBox();
    m_groupBox10_SpinBox1->setRange(1, 255);
    m_groupBox10_SpinBox1->setValue(3);
    m_groupBox10_CheckBox2 = new QCheckBox("New");
    QLabel *groupBox10_Label2 = new QLabel(tr("Start:"));
    m_groupBox10_SpinBox2 = new ZeroOnEmptySpinBox();
    m_groupBox10_SpinBox2->setRange(0, 100000);
    m_groupBox10_SpinBox2->setValue(1);
    QLabel *groupBox10_Label3 = new QLabel(tr("Step:"));
    m_groupBox10_SpinBox3 = new QSpinBox();
    m_groupBox10_SpinBox3->setRange(1, 100000);
    m_groupBox10_SpinBox3->setValue(1);
    QGridLayout *groupBox10Layout = new QGridLayout();
    groupBox10Layout->setContentsMargins(50, 7, 7, 7); // (Links, Oben, Rechts, Unten)
    groupBox10Layout->addWidget(m_groupBox10_CheckBox1, 0, 0);
    groupBox10Layout->addWidget(m_groupBox10_LineEdit,  0, 1);
    groupBox10Layout->addWidget(groupBox10_Label1,      0, 2, Qt::AlignRight | Qt::AlignVCenter);
    groupBox10Layout->addWidget(m_groupBox10_SpinBox1,  0, 3);
    groupBox10Layout->addWidget(m_groupBox10_CheckBox2, 1, 1);
    groupBox10Layout->addWidget(groupBox10_Label2,      1, 2, Qt::AlignRight | Qt::AlignVCenter);
    groupBox10Layout->addWidget(m_groupBox10_SpinBox2,  1, 3);
    groupBox10Layout->addWidget(groupBox10_Label3,      1, 4, Qt::AlignRight | Qt::AlignVCenter);
    groupBox10Layout->addWidget(m_groupBox10_SpinBox3,  1, 5);
    groupBox10Layout->setColumnStretch(0, 0);
    groupBox10Layout->setColumnStretch(1, 0);
    groupBox10Layout->setColumnStretch(2, 0);
    groupBox10Layout->setColumnStretch(3, 0);
    groupBox10Layout->setColumnStretch(4, 0);
    groupBox10Layout->setColumnStretch(5, 0);
    groupBox10Layout->setColumnStretch(6, 1);  // Die allerletzte Spalte bekommt Stretch 1
    m_groupBox10->setLayout(groupBox10Layout);

    m_groupBox11 = new QGroupBox("Case", this);
    m_groupBox11->setCheckable(true);
    m_groupBox11_ComboBox = new QComboBox();
    m_groupBox11_ComboBox->addItems({"Same", "Lower", "Upper", "Title", "Fixed", "Sentence"});
    m_groupBox11_LineEdit = new QLineEdit();
    QHBoxLayout *groupBox11Layout = new QHBoxLayout;
    groupBox11Layout->setContentsMargins(50, 7, 7, 7); // (Links, Oben, Rechts, Unten)
    groupBox11Layout->addWidget(m_groupBox11_ComboBox);
    groupBox11Layout->addWidget(m_groupBox11_LineEdit);
    m_groupBox11->setLayout(groupBox11Layout);

    m_groupBox12_CheckBox1 = new QCheckBox("Files");
    m_groupBox12_CheckBox1->setChecked(true);
    m_groupBox12_CheckBox2 = new QCheckBox("Folders");
    m_groupBox12_CheckBox2->setChecked(true);
    m_groupBox12_CheckBox3 = new QCheckBox("Recursive");
    m_groupBox12_CheckBox3->setChecked(false);
    QLabel *groupBox12_Label = new QLabel(tr("Filter"));
    m_groupBox12_LineEdit = new QLineEdit();
    m_groupBox12_CheckBox4 = new QCheckBox("Match case");
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
    m_buttonDoRename = new QPushButton("Rename", this);
    m_buttonDoRename->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    m_buttonDoRename->setEnabled(false);
    buttonBoxLayout->addStretch();
    buttonBoxLayout->addWidget(m_buttonDoRename);

    QVBoxLayout *controlsLayout = new QVBoxLayout();
    controlsLayout->setContentsMargins(10, 10, 10, 10);
    controlsLayout->setSpacing(10);

    controlsLayout->addWidget(m_groupBox13); // Which part
    controlsLayout->addWidget(m_groupBox1);  // RegEx
    controlsLayout->addWidget(m_groupBox3);  // Replace
    controlsLayout->addWidget(m_groupBox5);  // Remove
    controlsLayout->addWidget(m_groupBox6);  // Move/Copy
    controlsLayout->addWidget(m_groupBox7);  // Add
    controlsLayout->addWidget(m_groupBox2);  // Add Numbering
    //controlsLayout->addWidget(m_groupBox8);  // Add Date
    controlsLayout->addWidget(m_groupBox9);  // Add Folder Name
    controlsLayout->addWidget(m_groupBox10); // Number Padding
    controlsLayout->addWidget(m_groupBox11); // Case

    controlsLayout->addLayout(buttonBoxLayout);
    controlsLayout->addStretch();

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

    m_actionListViewEditFiles = new QAction(tr("Edit"), this);
    m_actionListViewEditFiles->setShortcut(QKeySequence("Ctrl+E"));
    connect(m_actionListViewEditFiles, &QAction::triggered, this, &MainWindow::action_ListViewEditFiles);

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
        m_actionListViewEditFiles->setIconVisibleInMenu(false);
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
        m_actionListViewEditFiles->setShortcutVisibleInContextMenu(false);
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

#ifdef Q_OS_LINUX
    loadMimeCache();
#endif

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
    
    connect(m_groupBox1_LineEdit1, &QLineEdit::textChanged, this, &MainWindow::validateInputBoxRegex);
    connect(m_buttonDoRename, &QPushButton::clicked, this, [this]() {
        m_abstractModel->applyBatchRename();
        onRenameRulesChanged(); // force recalculation of newName
        //m_tableView->clearSelection();
    });
    connect(m_tableView->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, [this]() {
                QModelIndexList selectedProxyIndexes = m_tableView->selectionModel()->selectedIndexes();

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
                m_buttonDoRename->setEnabled(!orderedSourceRows.isEmpty());
            });

    // part of mitigation for Shift+Pos1 / Shift+End not working in tableView
    connect(m_tableView->selectionModel(), &QItemSelectionModel::currentChanged, this, &MainWindow::onTableCurrentChanged);

    setupRenameRuleSignals();
    onRenameRulesChanged();

    showFolder(m_currentDirectory, externalPathList);
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

void MainWindow::onCheckboxClickedRegExName(Qt::CheckState state) {
}

void MainWindow::onCheckboxClickedRegExContent(Qt::CheckState state) {
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

void MainWindow::showFolder(const QString &directoryPath, const QStringList &externalPathList) {
    if (directoryPath == "drives://") {
        return;
    }

    if (!directoryPath.isEmpty()) {
        // Handle non-existing paths
        QDir dir(directoryPath);
        if (!dir.exists()) {
            return;
        }

        // Rechteprüfung (Leserechte vorhanden?)
        QFileInfo dirInfo(directoryPath);
        if (!dirInfo.isReadable()) {
            QMessageBox::warning(
                this,
                tr("Access denied"),
                tr("You don't have the required permissions to access this folder:<br><br>%1")
                    .arg(QDir::toNativeSeparators(directoryPath))
                );
            return;
        }
    }

    auto *activeView = qobject_cast<QAbstractItemView*>(m_viewStack->currentWidget());

    // --- STORE FOCUS AND SELECTION ---
    QStringList selectedPathsToRestore;
    QString focusedPathToRestore;
    QString pathBelowLastSelectedToRestore;
    int lastSelectedProxyRow = -1;

    if (m_selectionModel) {
        if (externalPathList.isEmpty()) {
            // A) Selektierte Zeilen merken & höchste Zeilennummer ermitteln
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

        // B) Fokussiertes (Current) Element merken (falls keines via focusPath erzwungen wurde)
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

        // C) Pfad des Elements DIREKT UNTER DEM LETZTEN selektierten/fokussierten Element merken
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

            // Prüfen, ob diese Zeile selektiert war
            if (selectedSet.contains(currentPath)) {
                QModelIndex topLeft = proxyIdx;
                QModelIndex bottomRight = m_proxyModel->index(i, colCount - 1);
                restoreSelection.select(topLeft, bottomRight);

                // Merken für Prio 2 (Falls Original-Fokus weg ist, aber Markierung existiert)
                if (!firstStillExistingSelectedProxyIndex.isValid()) {
                    firstStillExistingSelectedProxyIndex = proxyIdx;
                }
            }

            // Prio 1:  Prüfen, ob diese Zeile den Fokus hatte (oder haben soll)
            if (!focusedPathToRestore.isEmpty() && currentPath == focusedPathToRestore) {
                proxyIndexToFocus = proxyIdx;
            }

            // Prio 3: Prüfen, ob dies das Element unterhalb der ehemaligen Selektion ist
            if (!pathBelowLastSelectedToRestore.isEmpty() && currentPath == pathBelowLastSelectedToRestore) {
                indexBelowLastSelectedProxy = proxyIdx;
            }
        }

        // --- DOLPHIN FOCUS EVALUATION ---
        if (!proxyIndexToFocus.isValid()) {
            // Prio 2: Focus auf ein verbliebenes, markiertes Item setzen
            if (firstStillExistingSelectedProxyIndex.isValid()) {
                proxyIndexToFocus = firstStillExistingSelectedProxyIndex;
            }
            // Prio 3: Focus auf das Item setzen, das vorher UNTER dem letzten markierten lag
            else if (indexBelowLastSelectedProxy.isValid()) {
                proxyIndexToFocus = indexBelowLastSelectedProxy;
            }
            // Prio 4: Fallback auf die relative Position (Zeilenindex) vor dem Reload
            else if (lastSelectedProxyRow >= 0) {
                int targetRow = std::min(lastSelectedProxyRow, rowCount - 1);
                targetRow = std::max(0, targetRow);
                proxyIndexToFocus = m_proxyModel->index(targetRow, 0);
            }
            // Prio 5: Erstes Element im Ordner
            else {
                //proxyIndexToFocus = m_proxyModel->index(0, 0);
            }
        }

        // Fokus anwenden
        if (proxyIndexToFocus.isValid() && activeView) {
            activeView->setCurrentIndex(proxyIndexToFocus);
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
        // m_tableView->horizontalHeader()->setVisible(true);
        
        m_tableView->horizontalHeader()->setSectionResizeMode(CustomTableModel::eColPath,    QHeaderView::ResizeToContents);
        m_tableView->horizontalHeader()->setSectionResizeMode(CustomTableModel::eColSize,    QHeaderView::ResizeToContents);
        m_tableView->horizontalHeader()->setSectionResizeMode(CustomTableModel::eColDate,    QHeaderView::ResizeToContents);
        m_tableView->horizontalHeader()->setSectionResizeMode(CustomTableModel::eColType,    QHeaderView::ResizeToContents);

        m_tableView->horizontalHeader()->setSectionResizeMode(CustomTableModel::eColName, QHeaderView::Stretch);
        m_tableView->horizontalHeader()->setSectionResizeMode(CustomTableModel::eColNewName, QHeaderView::Stretch);

        m_tableView->horizontalHeader()->doItemsLayout();

        int eColNameWidth = m_tableView->columnWidth(CustomTableModel::eColName);
        int eColNewNameWidth = m_tableView->columnWidth(CustomTableModel::eColNewName);

        m_tableView->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);

        m_tableView->setColumnWidth(CustomTableModel::eColName, eColNameWidth);
        m_tableView->setColumnWidth(CustomTableModel::eColNewName, eColNewNameWidth);
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
        /*
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
        */
        mainMenu.addSeparator(); //-----------------------------------------
        mainMenu.addAction(m_actionListViewFileProperties);
    } else if (m_currentDirectory == "drives://") {
        mainMenu.addAction(m_actionListViewOpenFiles);
        mainMenu.setDefaultAction(m_actionListViewOpenFiles);
        mainMenu.addSeparator(); //-----------------------------------------
        mainMenu.addAction(m_actionListViewFileProperties);
    } else {
        QStringList selectedPaths = getActiveViewPathList(); // Used by KDE SERVICE-ACTIONS

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
        /*
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
                DesktopEntry info = Helpers::getDesktopEntryById(id);
                if (info.isValid) {
                    QAction *action = openWithMenu->addAction(QIcon::fromTheme(info.icon), info.name);
                    connect(action, &QAction::triggered, [info, filePath, this]() {
                        Helpers::openFileListWithHandler(info.id, getActiveViewPathList());
                    });
                }
            }
        }
        */
        // Native KDE Open with menu
        mainMenu.addSeparator(); //-----------------------------------------

        if (!selectedPaths.isEmpty()) {
            // 1. Alle eindeutigen MIME-Typen aus den markierten Dateien ermitteln
            QMimeDatabase db;
            QSet<QString> uniqueMimeTypes;
            for (const QString &path : std::as_const(selectedPaths)) {
                uniqueMimeTypes.insert(db.mimeTypeForFile(path).name());
            }

            // 2. Schnittmenge der verfügbaren KServices berechnen
            KService::List commonServices;
            bool isFirstMime = true;

            for (const QString &mimeName : std::as_const(uniqueMimeTypes)) {
                KService::List servicesForMime = KApplicationTrader::queryByMimeType(mimeName);

                if (isFirstMime) {
                    commonServices = servicesForMime;
                    isFirstMime = false;
                } else {
                    // Nur Services behalten, die auch den aktuellen MIME-Typ unterstützen
                    KService::List intersected;
                    for (const KService::Ptr &service : std::as_const(commonServices)) {
                        bool supportsMime = std::any_of(servicesForMime.begin(), servicesForMime.end(),
                                                        [&service](const KService::Ptr &s) {
                                                            return s->storageId() == service->storageId();
                                                        });

                        if (supportsMime) {
                            intersected.append(service);
                        }
                    }
                    commonServices = intersected;
                }

                // Abbrechen, wenn die Schnittmenge leer wird
                if (commonServices.isEmpty()) {
                    break;
                }
            }

            // 3. Untermenü "Öffnen mit" im Hauptmenü anlegen
            QMenu *openWithMenu = mainMenu.addMenu(tr("Open with"));
            if (m_settings.showIconsInMenu) {
                openWithMenu->setIcon(QIcon::fromTheme("system-run"));
            }

            // 4. Gefundene Anwendungen (Schnittmenge) zum Menü hinzufügen
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

            // 5. "Other Application..." hinzufügen, wenn alle Items den gleichen MimeType haben ODER commonServices nicht leer ist
            bool showOtherApp = (uniqueMimeTypes.size() == 1) || !commonServices.isEmpty();

            if (showOtherApp) {
                if (!commonServices.isEmpty()) {
                    openWithMenu->addSeparator();
                }

                QAction *otherAppAction = openWithMenu->addAction(QIcon::fromTheme("system-run"), tr("Other Application..."));
                connect(otherAppAction, &QAction::triggered, this, [selectedPaths, this]() {
                    QList<QUrl> urls;
                    urls.reserve(selectedPaths.size());
                    for (const QString &path : selectedPaths) {
                        urls.append(QUrl::fromLocalFile(path));
                    }

                    auto *dialog = new KOpenWithDialog(urls, this);
                    dialog->show();
                });
            }
        }
#endif
        mainMenu.addSeparator(); //-----------------------------------------
        mainMenu.addAction(m_actionListViewBrowseToFile);
        mainMenu.addAction(m_actionListViewCopyPaths);
        mainMenu.addAction(m_actionListViewCutFiles);
        mainMenu.addAction(m_actionListViewCopyFiles);
        mainMenu.addAction(m_actionListViewRenameFiles);
        mainMenu.addSeparator(); //-----------------------------------------
        mainMenu.addAction(m_actionListViewDeleteFiles);
#ifdef Q_OS_LINUX
        // KDE SERVICE-ACTIONS
        if (!selectedPaths.isEmpty()) {
            KFileItemList fileItemList;
            fileItemList.reserve(selectedPaths.size());
            for (const QString &path : std::as_const(selectedPaths)) {
                fileItemList.append(KFileItem(QUrl::fromLocalFile(path)));
            }

            KFileItemListProperties itemProperties(fileItemList);

            // KFileItemActions mit &mainMenu als Parent erstellen
            auto *serviceActions = new KFileItemActions(&mainMenu);
            serviceActions->setParentWidget(this);
            serviceActions->setItemListProperties(itemProperties);

            mainMenu.addSeparator(); //-----------------------------------------
            serviceActions->addActionsTo(&mainMenu);
        }
#endif
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

    // 1. Alle ausgewählten Zellen/Indizes holen
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

void MainWindow::action_ListViewOpenFiles() {
    QStringList pathList = getActiveViewPathList();
    if (pathList.isEmpty()) return;

    QList<QUrl> urlsToOpen;

    for (const QString &path : std::as_const(pathList)) {
        QFileInfo fileInfo(path);
        if (!fileInfo.exists()) continue;

        QString fileExt = fileInfo.suffix().toLower();

        if (fileExt == "desktop") {
            Helpers::launchDesktopFile(Helpers::getDesktopEntry(fileInfo));
        }
#ifdef Q_OS_LINUX
        else if (fileInfo.isExecutable()
                 && !fileInfo.isDir()
                 && !m_settings.audioExts.contains(fileExt)
                 && !m_settings.imageExts.contains(fileExt)
                 && !m_settings.videoExts.contains(fileExt)) {
            // Workaround on linux where executable files are not neccessarily executed when opened via QDesktopServices::openUrl().
            QProcess::startDetached(path, QStringList(), fileInfo.absolutePath());
        }
#endif
        else {
            urlsToOpen.append(QUrl::fromLocalFile(fileInfo.absoluteFilePath()));
        }
    }

    if (!urlsToOpen.isEmpty()) {
#ifdef Q_OS_LINUX
        Helpers::openUrlsWithKIO(urlsToOpen);
#else
        Helpers::openUrlsWithWin32(urlsToOpen);
#endif
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
#ifdef Q_OS_LINUX
    QMimeDatabase db;
#endif

    for (const QString &fullPath : std::as_const(pathList)) {
        QFileInfo fileInfo(fullPath);
        QString fileExt = fileInfo.suffix().toLower();

        if (fileExt.isEmpty()) {
#ifdef Q_OS_LINUX
            QMimeType mime = db.mimeTypeForFile(fullPath);
            if (mime.inherits("text/plain")) {
                pathListText << fullPath;
            }
#endif
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
        Helpers::openFileListWithHandler(m_settings.audioEditor, pathListAudio);
    }

    if (!pathListImage.isEmpty()) {
        Helpers::openFileListWithHandler(m_settings.imageEditor, pathListImage);
    }

    if (!pathListText.isEmpty()) {
        Helpers::openFileListWithHandler(m_settings.textEditor, pathListText);
    }

    if (!pathListVideo.isEmpty()) {
        Helpers::openFileListWithHandler(m_settings.videoEditor, pathListVideo);
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
        fileOperation(OperationType::Recycle, urlFileList, "", false);
    } else {
        fileOperation(OperationType::Delete, urlFileList, "", false);
    }

// We don't trigger an update of the view. The user should do it manually
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
        if (m_currentDirectory.isEmpty() || m_currentDirectory == "drives://") {
            return;
        }
        pathList = { m_currentDirectory };
    }

#ifdef Q_OS_LINUX
    if (pathList.size() > 1) {
        Helpers::showKdePropertiesDialog(pathList, this);
        return;
    }
#endif

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
        QString size = Helpers::formatAdaptiveSize(fileInfo.size());
        QString lastModified = fileInfo.lastModified().toString("yyyy-MM-dd  HH:mm:ss");

        QIcon icon = m_iconProvider.icon(fileInfo);
        QPixmap pix = icon.pixmap(QSize(48, 48));
        if (!Helpers::hasIconExt(fileInfo)) {
            QPixmap thumb = Helpers::generateThumbnail(fileInfo);
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

void MainWindow::updateWidgetStyles() {

#ifdef Q_OS_WIN
    bool isDark = true;
#elif defined(Q_OS_LINUX)
    bool isDark = (QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark);
#endif

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

    if (m_currentStyleState != targetState) {
        m_currentStyleState = targetState;
        m_StyleLastPalette = currentPalette;

#ifdef Q_OS_WIN
        if (targetState == StyleState::Elevated) {
            m_tableView->setStyleSheet(Styles::tableViewElevated);
            m_listView->setStyleSheet(Styles::listViewElevated);
            m_thumbnailView->setStyleSheet(Styles::thumbnailViewElevated);
        } else {
            m_tableView->setStyleSheet(Styles::tableViewLight);
            m_listView->setStyleSheet(Styles::listViewLight);
            m_thumbnailView->setStyleSheet(Styles::thumbnailViewLight);
        }
#elif defined(Q_OS_LINUX)
        if (targetState == StyleState::Dark) {
            this->setStyleSheet(
                "QMainWindow { background-color: #222222; }"
                "QHeaderView::section { background-color: #222222; color: #ffffff; }"
                );

            m_groupBox1->setStyleSheet(Styles::groupBoxStyleSheetDark);
            m_groupBox2->setStyleSheet(Styles::groupBoxStyleSheetDark);
            m_groupBox3->setStyleSheet(Styles::groupBoxStyleSheetDark);
            m_groupBox5->setStyleSheet(Styles::groupBoxStyleSheetDark);
            m_groupBox6->setStyleSheet(Styles::groupBoxStyleSheetDark);
            m_groupBox7->setStyleSheet(Styles::groupBoxStyleSheetDark);
            //m_groupBox8->setStyleSheet(Styles::groupBoxStyleSheetDark);
            m_groupBox9->setStyleSheet(Styles::groupBoxStyleSheetDark);
            m_groupBox10->setStyleSheet(Styles::groupBoxStyleSheetDark);
            m_groupBox11->setStyleSheet(Styles::groupBoxStyleSheetDark);
            //m_groupBox12->setStyleSheet(Styles::groupBoxStyleSheetDark);
            m_groupBox13->setStyleSheet(Styles::groupBoxStyleSheetDark);
        } else {
            // WICHTIG: Stylesheet leeren, wenn das System auf Light Mode wechselt!
            // Dadurch schaltet Qt wieder auf das helle Breeze-Standarddesign um.
            this->setStyleSheet("");

            m_groupBox1->setStyleSheet(Styles::groupBoxStyleSheet);
            m_groupBox2->setStyleSheet(Styles::groupBoxStyleSheet);
            m_groupBox3->setStyleSheet(Styles::groupBoxStyleSheet);
            m_groupBox5->setStyleSheet(Styles::groupBoxStyleSheet);
            m_groupBox6->setStyleSheet(Styles::groupBoxStyleSheet);
            m_groupBox7->setStyleSheet(Styles::groupBoxStyleSheet);
            //m_groupBox8->setStyleSheet(Styles::groupBoxStyleSheet);
            m_groupBox9->setStyleSheet(Styles::groupBoxStyleSheet);
            m_groupBox10->setStyleSheet(Styles::groupBoxStyleSheet);
            m_groupBox11->setStyleSheet(Styles::groupBoxStyleSheet);
            //m_groupBox12->setStyleSheet(Styles::groupBoxStyleSheet);
            m_groupBox13->setStyleSheet(Styles::groupBoxStyleSheet);
        }

        if (targetState == StyleState::Elevated) {
            m_tableView->setStyleSheet(Styles::tableViewElevatedLinux);
            m_listView->setStyleSheet(Styles::listViewElevatedLinux);
            m_thumbnailView->setStyleSheet(Styles::thumbnailViewElevatedLinux);
        } else {
            m_listView->setStyleSheet(Styles::listViewLinux);
        }
#endif
    }
    else if (currentPalette != m_StyleLastPalette) {
        m_currentStyleState = targetState;
        m_StyleLastPalette = currentPalette;

        if (m_groupBox1_LineEdit1) {
            m_groupBox1_LineEdit1->style()->unpolish(m_groupBox1_LineEdit1);
            m_groupBox1_LineEdit1->style()->polish(m_groupBox1_LineEdit1);
        }

        if (m_tableView) {
            m_tableView->style()->unpolish(m_tableView);
            m_tableView->style()->polish(m_tableView);
        }

        if (m_listView) {
            m_listView->style()->unpolish(m_listView);
            m_listView->style()->polish(m_listView);
        }

        if (m_thumbnailView) {
            m_thumbnailView->style()->unpolish(m_thumbnailView);
            m_thumbnailView->style()->polish(m_thumbnailView);
        }
    }
}

//######################################################################################
// Protected Overrides

void MainWindow::changeEvent(QEvent *event) {
    QMainWindow::changeEvent(event);

    // Reagiert auf System-Palette-Änderungen (z. B. Wechsel der Akzentfarbe in KDE)
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
            /*
            else if (mouseEvent->button() == Qt::MiddleButton) {
                navigateUp();
                return true;
            }
            */
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

        // Wenn in der View gerade ein Editor offen ist (z.B. Dateiname umbenennen),
        // dürfen wir Tasten wie "Entf", "Backspace" oder "Enter" NICHT abfangen!
        if (qobject_cast<QLineEdit*>(targetWidget)) {
            return QObject::eventFilter(obj, event);
        }
        else if (qobject_cast<QLineEdit*>(targetWidget->parent())) {
            return QObject::eventFilter(obj, event);
        }

        if (keyEvent->key() == Qt::Key_Escape) {
            if (m_bSearchActive.load()) {
                m_abstractModel->abort();
                return true;
            }
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
                else if (keyEvent->key() == Qt::Key_F5) {
                    showFolder(m_currentDirectory, QStringList());
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
    QString InputBox1Text = m_groupBox1_LineEdit1->text();

    if (InputBox1Text.isEmpty()) {
        m_groupBox1_LineEdit1->setStyleSheet(m_styleLineEditNormal);
    } else {
        QRegularExpression re1(InputBox1Text);

        if (!re1.isValid()) {
            m_groupBox1_LineEdit1->setStyleSheet(m_styleLineEditError);
        } else {
            m_groupBox1_LineEdit1->setStyleSheet(m_styleLineEditNormal);
        }
    }
}

void MainWindow::setupRenameRuleSignals() {
    // Which part
    connect(m_groupBox13_RadioButton1, &QRadioButton::toggled, this, &MainWindow::onRenameRulesChanged);
    connect(m_groupBox13_RadioButton2, &QRadioButton::toggled, this, &MainWindow::onRenameRulesChanged);
    connect(m_groupBox13_RadioButton3, &QRadioButton::toggled, this, &MainWindow::onRenameRulesChanged);

    // RegEx
    connect(m_groupBox1, &QGroupBox::toggled, this, &MainWindow::onRenameRulesChanged);
    connect(m_groupBox1_LineEdit1, &QLineEdit::textChanged, this, &MainWindow::onRenameRulesChanged);
    connect(m_groupBox1_LineEdit2, &QLineEdit::textChanged, this, &MainWindow::onRenameRulesChanged);

    // Filename (2)
    connect(m_groupBox2, &QGroupBox::toggled, this, &MainWindow::onRenameRulesChanged);
    connect(m_groupBox2_ComboBox, &QComboBox::currentIndexChanged, this, &MainWindow::onRenameRulesChanged);
    connect(m_groupBox2_LineEdit, &QLineEdit::textChanged, this, &MainWindow::onRenameRulesChanged);

    // Replace
    connect(m_groupBox3, &QGroupBox::toggled, this, &MainWindow::onRenameRulesChanged);
    connect(m_groupBox3_LineEdit1, &QLineEdit::textChanged, this, &MainWindow::onRenameRulesChanged);
    connect(m_groupBox3_LineEdit2, &QLineEdit::textChanged, this, &MainWindow::onRenameRulesChanged);
    connect(m_groupBox3_CheckBox, &QCheckBox::checkStateChanged, this, &MainWindow::onRenameRulesChanged);

    // Remove
    connect(m_groupBox5, &QGroupBox::toggled, this, &MainWindow::onRenameRulesChanged);
    connect(m_groupBox5_SpinBox1, &QSpinBox::valueChanged, this, &MainWindow::onRenameRulesChanged);
    connect(m_groupBox5_SpinBox2, &QSpinBox::valueChanged, this, &MainWindow::onRenameRulesChanged);
    connect(m_groupBox5_SpinBox3, &QSpinBox::valueChanged, this, &MainWindow::onRenameRulesChanged);
    connect(m_groupBox5_SpinBox4, &QSpinBox::valueChanged, this, &MainWindow::onRenameRulesChanged);

    // Move/Copy
    connect(m_groupBox6, &QGroupBox::toggled, this, &MainWindow::onRenameRulesChanged);
    connect(m_groupBox6_ComboBox1, &QComboBox::currentIndexChanged, this, &MainWindow::onRenameRulesChanged);
    connect(m_groupBox6_ComboBox2, &QComboBox::currentIndexChanged, this, &MainWindow::onRenameRulesChanged);
    connect(m_groupBox6_SpinBox1, &QSpinBox::valueChanged, this, &MainWindow::onRenameRulesChanged);
    connect(m_groupBox6_SpinBox2, &QSpinBox::valueChanged, this, &MainWindow::onRenameRulesChanged);
    connect(m_groupBox6_LineEdit, &QLineEdit::textChanged, this, &MainWindow::onRenameRulesChanged);

    // Add
    connect(m_groupBox7, &QGroupBox::toggled, this, &MainWindow::onRenameRulesChanged);
    connect(m_groupBox7_LineEdit1, &QLineEdit::textChanged, this, &MainWindow::onRenameRulesChanged);
    connect(m_groupBox7_LineEdit2, &QLineEdit::textChanged, this, &MainWindow::onRenameRulesChanged);
    connect(m_groupBox7_SpinBox,   &QSpinBox::valueChanged, this, &MainWindow::onRenameRulesChanged);
    connect(m_groupBox7_LineEdit3, &QLineEdit::textChanged, this, &MainWindow::onRenameRulesChanged);

    // Add Numbering
    connect(m_groupBox2_ComboBox,  &QComboBox::currentIndexChanged, this, &MainWindow::onRenameRulesChanged);
    connect(m_groupBox2_SpinBox1,  &QSpinBox::valueChanged, this, &MainWindow::onRenameRulesChanged);
    connect(m_groupBox2_SpinBox2,  &QSpinBox::valueChanged, this, &MainWindow::onRenameRulesChanged);
    connect(m_groupBox2_LineEdit,  &QLineEdit::textChanged, this, &MainWindow::onRenameRulesChanged);

    // Auto Date
    //connect(m_groupBox8, &QGroupBox::toggled, this, &MainWindow::onRenameRulesChanged);

    // Append Folder Name
    connect(m_groupBox9, &QGroupBox::toggled, this, &MainWindow::onRenameRulesChanged);
    connect(m_groupBox9_ComboBox, &QComboBox::currentIndexChanged, this, &MainWindow::onRenameRulesChanged);
    connect(m_groupBox9_LineEdit, &QLineEdit::textChanged, this, &MainWindow::onRenameRulesChanged);
    connect(m_groupBox9_SpinBox,  &QSpinBox::valueChanged, this, &MainWindow::onRenameRulesChanged);

    // Number Padding
    connect(m_groupBox10, &QGroupBox::toggled, this, &MainWindow::onRenameRulesChanged);
    connect(m_groupBox10_CheckBox1, &QCheckBox::checkStateChanged, this, &MainWindow::onRenameRulesChanged);
    connect(m_groupBox10_LineEdit,  &QLineEdit::textChanged, this, &MainWindow::onRenameRulesChanged);
    connect(m_groupBox10_SpinBox1,  &QSpinBox::valueChanged, this, &MainWindow::onRenameRulesChanged);
    connect(m_groupBox10_CheckBox2, &QCheckBox::checkStateChanged, this, &MainWindow::onRenameRulesChanged);
    connect(m_groupBox10_SpinBox2,  &QSpinBox::valueChanged, this, &MainWindow::onRenameRulesChanged);
    connect(m_groupBox10_SpinBox3,  &QSpinBox::valueChanged, this, &MainWindow::onRenameRulesChanged);

    // Case
    connect(m_groupBox11, &QGroupBox::toggled, this, &MainWindow::onRenameRulesChanged);
    connect(m_groupBox11_ComboBox, &QComboBox::currentIndexChanged, this, &MainWindow::onRenameRulesChanged);
    connect(m_groupBox11_LineEdit, &QLineEdit::textChanged, this, &MainWindow::onRenameRulesChanged);

    // Filters
    connect(m_groupBox12_CheckBox1, &QCheckBox::checkStateChanged, this, &MainWindow::onRenameRulesChanged);
    connect(m_groupBox12_CheckBox2, &QCheckBox::checkStateChanged, this, &MainWindow::onRenameRulesChanged);
    connect(m_groupBox12_CheckBox3, &QCheckBox::checkStateChanged, this, &MainWindow::onRenameRulesChanged);
    connect(m_groupBox12_CheckBox4, &QCheckBox::checkStateChanged, this, &MainWindow::onRenameRulesChanged);
    connect(m_groupBox12_LineEdit,  &QLineEdit::textChanged, this, &MainWindow::onRenameRulesChanged);
}

void MainWindow::onRenameRulesChanged() {
    RenameRules rules;

    int target = m_groupBox13_RadioButton1->isChecked() ? 0 : m_groupBox13_RadioButton2->isChecked() ? 1 : 2;

    // RegEx
    rules.rgx.enabled = m_groupBox1->isChecked();
    rules.rgx.target = target;
    rules.rgx.match = m_groupBox1_LineEdit1->text();
    rules.rgx.replace = m_groupBox1_LineEdit2->text();

    // Replace
    rules.rpl.enabled = m_groupBox3->isChecked();
    rules.rpl.target = target;
    rules.rpl.match = m_groupBox3_LineEdit1->text();
    rules.rpl.replace = m_groupBox3_LineEdit2->text();
    rules.rpl.matchCase = m_groupBox3_CheckBox->isChecked();

    // Remove
    rules.rmv.enabled = m_groupBox5->isChecked();
    rules.rmv.target = target;
    rules.rmv.firstN = m_groupBox5_SpinBox1->value();
    rules.rmv.lastN = m_groupBox5_SpinBox2->value();
    rules.rmv.fromN = m_groupBox5_SpinBox3->value();
    rules.rmv.toN = m_groupBox5_SpinBox4->value();

    // Move/Copy
    rules.mcp.enabled = m_groupBox6->isChecked();
    rules.mcp.target = target;
    rules.mcp.fromMode = m_groupBox6_ComboBox1->currentIndex();
    rules.mcp.fromPos = m_groupBox6_SpinBox1->value();
    rules.mcp.toMode = m_groupBox6_ComboBox2->currentIndex();
    rules.mcp.toPos = m_groupBox6_SpinBox2->value();
    rules.mcp.separator = m_groupBox6_LineEdit->text();

    // Add
    rules.add.enabled = m_groupBox7->isChecked();
    rules.add.target = target;
    rules.add.prefix = m_groupBox7_LineEdit1->text();
    rules.add.insertText = m_groupBox7_LineEdit2->text();
    rules.add.insertPos = m_groupBox7_SpinBox->value();
    rules.add.suffix = m_groupBox7_LineEdit3->text();

    // Add Numbering
    rules.num.enabled = m_groupBox2->isChecked();
    rules.num.target = target;
    rules.num.mode = m_groupBox2_ComboBox->currentIndex();
    rules.num.start = m_groupBox2_SpinBox1->value();
    rules.num.step = m_groupBox2_SpinBox2->value();
    rules.num.separator = m_groupBox2_LineEdit->text();

    // Add Date
    //rules.dat.enabled = m_groupBox8->isChecked();
    //rules.dat.target = target;

    // Add Folder Name
    rules.afn.enabled = m_groupBox9->isChecked();
    rules.afn.target = target;
    rules.afn.mode = m_groupBox9_ComboBox->currentIndex();
    rules.afn.separator = m_groupBox9_LineEdit->text();
    rules.afn.levels = m_groupBox9_SpinBox->value();

    // Padding
    rules.pad.enabled = m_groupBox10->isChecked();
    rules.pad.target = target;
    rules.pad.addLeadEnabled = m_groupBox10_CheckBox1->isChecked();
    rules.pad.addLeadChar = m_groupBox10_LineEdit->text();
    rules.pad.addLeadCount = m_groupBox10_SpinBox1->value();
    rules.pad.addLeadNewEnabled = m_groupBox10_CheckBox2->isChecked();
    rules.pad.addLeadNewStart = m_groupBox10_SpinBox2->value();
    rules.pad.addLeadNewStep = m_groupBox10_SpinBox3->value();

    // Case
    rules.cas.enabled = m_groupBox11->isChecked();
    rules.cas.target = target;
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
