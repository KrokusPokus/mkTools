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
#include <QItemSelectionModel>
#include <QLineEdit>
#include <QMainWindow>
#include <QPointer>
#include <QSet>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(const QString &targetDirectory, QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void onClipboardChanged();
    void onFilesDropped(const QList<QUrl> &urlList, const QString &targetDir, Qt::DropAction dropAction);
    void onHorizontalBarScrollChange();
    void onListItemDoubleClicked(const QModelIndex &index);
    void onListViewHeaderClicked();
    void onSearchFinished(uint iItemsFound, uint iNameMatched, uint iContentMatched, bool bSearchInterrupted);
    void onTableCurrentChanged(const QModelIndex &current, const QModelIndex &previous);
    void onTimedUpdateIcons();
    void onToggleListViewHeader();
    void onVerticalBarScrollChange();
    void setMainViewMode(ViewMode index);

    void onCheckboxClickedCRC(Qt::CheckState state);
    void onCheckboxClickedRegExContent(Qt::CheckState state);
    void onCheckboxClickedRegExName(Qt::CheckState state);
    void onTimedCalcCRC();

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
    void startSearch();
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
    QLineEdit *m_LineEdit2 = nullptr;

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
    QString m_privateTokenName = "application/x-mkfolderwidget-token";
    SettingsManager m_settings;

    qint64 m_lastActivationTime = 0;
    bool m_activationClickActive = false;
    QSet<QString> m_loadingThumbnails;
    QSet<QString> m_loadingCRCs;
    QString m_styleLineEditNormal = "";
    QString m_styleLineEditError = "background-color: red; color: white;";

    void validateInputBoxRegex();
    QCheckBox *m_CheckboxRegExContent = nullptr;
    QCheckBox *m_CheckboxRegExName = nullptr;
    QCheckBox *m_CheckboxNameCaseSense = nullptr;
    QCheckBox *m_CheckboxContentCaseSense = nullptr;
    QCheckBox *m_CheckboxDirectories = nullptr;
    QCheckBox *m_CheckboxCRC = nullptr;
    QTimer *m_timerCalcCrc = nullptr;

#ifdef Q_OS_WIN
    QString getSendToPath();
#endif

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
};
#endif // MAINWINDOW_H
