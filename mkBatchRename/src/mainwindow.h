#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "customlistview.h"
#include "customtablemodel.h"
#include "customtableview.h"
#include "filesortproxymodel.h"
#include "helpers.h"
#include "settingsmanager.h"

#include <QCheckBox>
#include <QComboBox>
#include <QElapsedTimer>
#include <QFileIconProvider>
#include <QGroupBox>
#include <QItemSelectionModel>
#include <QLineEdit>
#include <QMainWindow>
#include <QPointer>
#include <QPushButton>
#include <QRadioButton>
#include <QSet>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

class ZeroOnEmptySpinBox : public QSpinBox {
    Q_OBJECT
public:
    using QSpinBox::QSpinBox;

protected:
    // Wird von Qt aufgerufen, wenn die Eingabe ungültig/leer ist
    void fixup(QString &input) const override {
        if (input.trimmed().isEmpty()) {
            input = QString::number(minimum()); // Setzt den Text auf das Minimum (z.B. "0")
        } else {
            QSpinBox::fixup(input);
        }
    }
};

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QString targetDirectory, QStringList pathList, QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void onClipboardChanged();
    void onFilesDropped(const QList<QUrl> &urlList, const QString &targetDir, Qt::DropAction dropAction);
    void onHorizontalBarScrollChange();
    void onListItemDoubleClicked(const QModelIndex &index);
    void onListViewHeaderClicked();
    void showFolder(const QString &directoryPath, const QStringList &externalPathList);
    void onTableCurrentChanged(const QModelIndex &current, const QModelIndex &previous);
    void onTimedUpdateIcons();
    void onToggleListViewHeader();
    void onVerticalBarScrollChange();
    void setMainViewMode(ViewMode index);

    void onCheckboxClickedRegExContent(Qt::CheckState state);
    void onCheckboxClickedRegExName(Qt::CheckState state);

private:
    QString getActiveViewCurrentItemPath();
    QStringList getActiveViewPathList();
    QSet<int> getActiveViewRowSet();

    void action_EditSettingsFile();
    void action_ListViewBrowseToFile();
    void action_ListViewCopyFiles();
    void action_ListViewCopyPaths();
    void action_ListViewCutFiles();
    void action_ListViewDeleteFiles(bool bRecycleOnly);
    void action_ListViewEditFiles();
    void action_ListViewFileProperties();
    void action_ListViewOpenFiles();
    void action_ListViewPasteFiles();
    void action_ListViewRenameFiles();
    void action_ListViewNewFolder();
    void action_ListViewNewTextFile();
    void action_ViewModeList();
    void action_ViewModeDetails();
    void action_ViewModeThumbs();
    void action_SortByName();
    void action_SortBySize();
    void action_SortByDate();
    void action_SortByType();
    void action_SortAscending();
    void action_SortDescending();

    bool showDeleteConfirmationDialog(const QStringList &pathList, bool bRecycleOnly);
    void duplicateInstance();
    void elevateInstance();
    void loadMimeCache();
    void navigateBack();
    void navigateForward();
    void navigateToClipboardPath();
    void navigateUp();
    void fileOperation(OperationType opType, const QList<QUrl> &urls, const QString &targetDir, bool fromClipboard = false);
    void onShowContextMenu(QAbstractItemView *senderView, const QPoint &pos);
    void parseMimeAppsList(const QString &path);
    void parseMimeInfoCache(const QString &path);
    void removeCutMarkers();
    void scrollToCurrentItem();
    void selectAllItems();
    void setupClipboardForCopyOrCut(const QStringList &cutFilePaths, bool isCut);
    void updateColumns();
    void updateWidgetStyles();
    QPixmap generateThumbnailIcon(const QFileInfo &fileInfo);
    static QImage generateThumbnailAsync(const QFileInfo &fileInfo);
    void setupRenameRuleSignals();
    void onRenameRulesChanged();

    CustomTableModel *m_abstractModel = nullptr;
    QItemSelectionModel *m_selectionModel = nullptr;
    FileSortProxyModel *m_proxyModel = nullptr;

    QWidget *m_centralWidget = nullptr;
    QHBoxLayout *m_mainLayout = nullptr;

    CustomTableView *m_tableView = nullptr;
    CustomListView *m_listView = nullptr;
    CustomListView *m_thumbnailView = nullptr;
    QStackedWidget *m_viewStack = nullptr;

    QGroupBox *m_groupBox1 = nullptr;
    QLineEdit *m_groupBox1_LineEdit1 = nullptr;
    QLineEdit *m_groupBox1_LineEdit2 = nullptr;
    QGroupBox *m_groupBox2 = nullptr;
    QComboBox *m_groupBox2_ComboBox = nullptr;
    ZeroOnEmptySpinBox *m_groupBox2_SpinBox1 = nullptr;
    QSpinBox *m_groupBox2_SpinBox2 = nullptr;
    QLineEdit *m_groupBox2_LineEdit = nullptr;
    QGroupBox *m_groupBox3 = nullptr;
    QLineEdit *m_groupBox3_LineEdit1 = nullptr;
    QLineEdit *m_groupBox3_LineEdit2 = nullptr;
    QCheckBox *m_groupBox3_CheckBox = nullptr;
    QGroupBox *m_groupBox5 = nullptr;
    ZeroOnEmptySpinBox  *m_groupBox5_SpinBox1 = nullptr;
    ZeroOnEmptySpinBox  *m_groupBox5_SpinBox2 = nullptr;
    ZeroOnEmptySpinBox  *m_groupBox5_SpinBox3 = nullptr;
    ZeroOnEmptySpinBox  *m_groupBox5_SpinBox4 = nullptr;
    QGroupBox *m_groupBox6 = nullptr;
    QComboBox *m_groupBox6_ComboBox1 = nullptr;
    ZeroOnEmptySpinBox  *m_groupBox6_SpinBox1 = nullptr;
    QComboBox *m_groupBox6_ComboBox2 = nullptr;
    ZeroOnEmptySpinBox  *m_groupBox6_SpinBox2 = nullptr;
    QLineEdit *m_groupBox6_LineEdit = nullptr;
    QGroupBox *m_groupBox7 = nullptr;
    QLineEdit *m_groupBox7_LineEdit1 = nullptr;
    QLineEdit *m_groupBox7_LineEdit2 = nullptr;
    QLineEdit *m_groupBox7_LineEdit3 = nullptr;
    ZeroOnEmptySpinBox  *m_groupBox7_SpinBox = nullptr;
    QGroupBox *m_groupBox8 = nullptr;
    QGroupBox *m_groupBox9 = nullptr;
    QComboBox *m_groupBox9_ComboBox = nullptr;
    QLineEdit *m_groupBox9_LineEdit = nullptr;
    QSpinBox  *m_groupBox9_SpinBox = nullptr;
    QGroupBox *m_groupBox10 = nullptr;
    QCheckBox *m_groupBox10_CheckBox1 = nullptr;
    QLineEdit *m_groupBox10_LineEdit = nullptr;
    QCheckBox *m_groupBox10_CheckBox2 = nullptr;
    QSpinBox  *m_groupBox10_SpinBox1 = nullptr;
    ZeroOnEmptySpinBox  *m_groupBox10_SpinBox2 = nullptr;
    QSpinBox  *m_groupBox10_SpinBox3 = nullptr;
    QGroupBox *m_groupBox11 = nullptr;
    QComboBox *m_groupBox11_ComboBox = nullptr;
    QLineEdit *m_groupBox11_LineEdit = nullptr;
    QGroupBox *m_groupBox13 = nullptr;
    QRadioButton *m_groupBox13_RadioButton1 = nullptr;
    QRadioButton *m_groupBox13_RadioButton2 = nullptr;
    QRadioButton *m_groupBox13_RadioButton3 = nullptr;
    QPushButton *m_buttonDoRename = nullptr;

    QLineEdit *m_groupBox12_LineEdit = nullptr;
    QCheckBox *m_groupBox12_CheckBox1 = nullptr;
    QCheckBox *m_groupBox12_CheckBox2 = nullptr;
    QCheckBox *m_groupBox12_CheckBox3 = nullptr;
    QCheckBox *m_groupBox12_CheckBox4 = nullptr;

    QAction *m_actionListViewOpenFiles = nullptr;
    QAction *m_actionListViewEditFiles = nullptr;
    QAction *m_actionListViewBrowseToFile = nullptr;
    QAction *m_actionListViewCopyPaths = nullptr;
    QAction *m_actionListViewCutFiles = nullptr;
    QAction *m_actionListViewCopyFiles = nullptr;
    QAction *m_actionListViewDeleteFiles = nullptr;
    QAction *m_actionListViewRenameFiles = nullptr;
    QAction *m_actionListViewFileProperties = nullptr;
    QAction *m_actionListViewPasteFiles = nullptr;
    QAction *m_actionListViewNewFolder = nullptr;
    QAction *m_actionListViewNewTextFile = nullptr;
    QAction *m_actionViewModeList = nullptr;
    QAction *m_actionViewModeDetails = nullptr;
    QAction *m_actionViewModeThumbs = nullptr;
    QAction *m_actionSortByName = nullptr;
    QAction *m_actionSortBySize = nullptr;
    QAction *m_actionSortByDate = nullptr;
    QAction *m_actionSortByType = nullptr;
    QAction *m_actionSortAscending = nullptr;
    QAction *m_actionSortDescending = nullptr;

    QTimer *m_timerUpdateIcons = nullptr;
    QTimer *m_scrollToDebounceTimer = nullptr;
    QTimer *m_themeUpdateDebounceTimer = nullptr;

    bool m_bShowHiddenFiles = true;
    bool m_bHeaderVisible = true;
    QString m_currentDirectory;
    QElapsedTimer m_BenchmarkTimer;
    QFileIconProvider m_iconProvider;
    QPointer<QWidget> m_lastWidget;

    std::atomic<bool> m_bSearchActive{false};
    std::atomic<int> m_currentSearchGeneration{0};
    QHash<QString, QStringList> m_mimeCache;
    QByteArray m_currentClipboardToken;
    QString m_privateTokenName = "application/x-mkbatchrename-token";
    SettingsManager m_settings;

    qint64 m_lastActivationTime = 0;
    bool m_activationClickActive = false;
    QSet<QString> m_loadingThumbnails;
    QString m_styleLineEditNormal = "";
    QString m_styleLineEditError = "background-color: red; color: white;";

    void validateInputBoxRegex();

    bool m_processIsElevated{false};
    QPalette m_StyleLastPalette;
    enum class StyleState {
        Uninitialized,
        Light,
        Dark,
        Elevated
    };

    StyleState m_currentStyleState{StyleState::Uninitialized};

#ifdef Q_OS_WIN
    QString getSendToPath();
#endif

protected:
    void changeEvent(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
};
#endif // MAINWINDOW_H
