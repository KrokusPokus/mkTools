#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "customlistview.h"
#include "customtablemodel.h"
#include "customtableview.h"
#include "filesortproxymodel.h"
#include "helpers.h"
#include "settingsmanager.h"

#include <QCheckBox>
#include <QElapsedTimer>
#include <QFileIconProvider>
#include <QFileSystemWatcher>
#include <QItemSelectionModel>
#include <QLineEdit>
#include <QMainWindow>
#include <QSet>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(const QString &targetDirectoryr = QString(), const QString &focusPath = QString(), QWidget *parent = nullptr);
    ~MainWindow() override;

public slots:
    Q_SCRIPTABLE void openPath(const QString &path);

private slots:
    void onClipboardChanged();
    void onDirectoryChangedOnDisk(const QString &path);
    void onFilesDropped(const QList<QUrl> &urlList, const QString &targetDir, Qt::DropAction dropAction);
    void onFilterBoxChange(const QString &text);
    void onHorizontalBarScrollChange();
    void onListItemDoubleClicked(const QModelIndex &index);
    void onListViewHeaderClicked();
    void onTableCurrentChanged(const QModelIndex &current, const QModelIndex &previous);
    void onTimedUpdateIcons();
    void onToggleListViewHeader();
    void onVerticalBarScrollChange();
    void reloadDirectory();
    void setMainViewMode(ViewMode index);

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
    void action_toggleShowHidden();
    void action_LaunchRenameTool();

    bool showDeleteConfirmationDialog(const QStringList &pathList, bool bRecycleOnly);
    void browseFolder(QString directoryPath, const QString &focusPath = QString(), bool isHistoryNavigation = false);
    void duplicateInstance();
    void elevateInstance();
    void loadMimeCache();
    void navigateBack();
    void navigateForward();
    void navigateToClipboardPath();
    void navigateUp();
    void navigateSiblingNext();
    void navigateSiblingPrevious();
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

    CustomTableModel *m_abstractModel = nullptr;
    QItemSelectionModel *m_selectionModel = nullptr;
    FileSortProxyModel *m_proxyModel = nullptr;

    QWidget *m_centralWidget = nullptr;
    QVBoxLayout *m_mainLayout = nullptr;
    QWidget *m_topControlsContainerWidget = nullptr;

    CustomTableView *m_tableView = nullptr;
    CustomListView *m_listView = nullptr;
    CustomListView *m_thumbnailView = nullptr;
    QStackedWidget *m_viewStack = nullptr;

    QLineEdit *m_LineEdit1 = nullptr;
    //QLineEdit *m_LineEdit2 = nullptr;

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
    QAction *m_actionViewModeShowHidden = nullptr;
    QAction *m_actionViewModeRefresh = nullptr;
    QAction *m_actionSortByName = nullptr;
    QAction *m_actionSortBySize = nullptr;
    QAction *m_actionSortByDate = nullptr;
    QAction *m_actionSortByType = nullptr;
    QAction *m_actionSortAscending = nullptr;
    QAction *m_actionSortDescending = nullptr;
    QAction *m_actionNavigateUp = nullptr;
    QAction *m_actionNavigateBack = nullptr;
    QAction *m_actionNavigateForward = nullptr;
    QAction *m_actionNavigateSiblingPrevious = nullptr;
    QAction *m_actionNavigateSiblingNext = nullptr;
    QAction *m_actionNavigateClipboardPath = nullptr;
    QAction *m_actionNavigateDuplicate = nullptr;


    QTimer *m_timerUpdateIcons = nullptr;
    QTimer *m_scrollToDebounceTimer = nullptr;
    QTimer *m_themeUpdateDebounceTimer = nullptr;

    QFileSystemWatcher* m_fileSystemWatcher = nullptr;
    QTimer *m_watcherDebounceTimer = nullptr;
    bool m_ignoreNextWatcherSignal = false; // Flag für eigene Datei-Aktionen

    bool m_bShowHiddenFiles = false;
    bool m_bHeaderVisible = true;
    QString m_currentDirectory;
    QString m_activeFilterTerms;
    QElapsedTimer m_BenchmarkTimer;
    QFileIconProvider m_iconProvider;
    QHash<QString, QStringList> m_mimeCache;
    QByteArray m_currentClipboardToken;
    QString m_privateTokenName = "application/x-mkfolderwidget-token";
    SettingsManager m_settings;
    QStringList m_backHistory;
    QStringList m_forwardHistory;
    bool m_isEditingFile = false;
    bool m_refreshPendingWhileEditing = false;

    qint64 m_lastActivationTime = 0;
    bool m_activationClickActive = false;
    QSet<QString> m_loadingThumbnails;

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
    QAction *m_actionWinRarOpen = nullptr;
    QAction *m_actionCompressCBZ = nullptr;
    QAction *m_actionCompressZIP = nullptr;
    QAction *m_actionExtractHere = nullptr;
    QAction *m_actionExtractToSubfolder = nullptr;

    void action_WinRarOpen();
    void action_WinRarCompress(const QString &archiveExt);
    void action_WinRarExtract(bool toSubFolder);
    QString getSendToPath();

    QString m_winrarPath = "D:/A/1/WinRAR/WinRAR.exe";
    QIcon m_winrarIcon;
#endif

protected:
    void closeEvent(QCloseEvent *event) override;
    void changeEvent(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
#ifdef Q_OS_WIN
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;
#endif
};
#endif // MAINWINDOW_H

