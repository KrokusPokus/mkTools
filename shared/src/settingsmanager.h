#ifndef SETTINGSMANAGER_H
#define SETTINGSMANAGER_H

#include <QDialog>
#include <QSet>
#include <QSettings>
#include <QString>
#include <QStringList>

class QCheckBox;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QStackedWidget;

class SettingsManager {
public:
	SettingsManager();

    // Manager without settings file, holds the defaults of all settings (for the settings dialog)
    struct DefaultsOnly {};
    explicit SettingsManager(DefaultsOnly);

    // Shows the settings dialog with the values of the settings file. On OK the file is written and this
    // instance is reloaded from it (runtime overrides of the program have to be applied again), returns true.
    bool showSettingsDialog(QWidget *parent);

	void load();
    void saveSettings();
    static QString getSettingsPath();

    QSet<QString> hideExts;           // extensions that are hidden if showFileExtensions is off
    QSet<QString> contentSearchExts;  // files whose content is searched by mkFileSearch

    QString mergeTool;
	QString fileManager;
    QString renameTool;
    QString searchTool;

	bool alternatingRowColors;
    bool executableFilesRed;
    bool menuOnMouseUp;
	bool showGrid;
    bool showIconsInMenu;
	bool showPlaceholderText;
    bool showShortcutsInMenu;
    bool allowLauncherShrink;
    bool showFileExtensions;

    QStringList searchFolders;

    // mkTransactionHandler specific
    bool useKdeFileOperations;

    friend class SettingsDialog;

private:
    void saveSearchFolders();

	QSet<QString> parseExtensions(const QString &input);
    void safeSetValue(QSettings &settings, const QString &key, const QVariant &value);
    QString formatStringSet(const QSet<QString> &extensionSet);
    void getDefaults();

    QStringList importSearchFolders(const QString &input);
    QString exportSearchFolders(const QStringList &folders);

    // Defaults
    const QString DEFAULT_CONTENT_SEARCH_EXTS = "adr,ahk,ass,au3,bat,c,cfg,conf,cpp,cs,css,cue,cxx,desktop,dic,dsf,dsk,duf,h,hpp,htm,html,inf,ini,ion,js,json,log,lst,lua,md,nfo,py,rc,reg,scp,sfv,sh,slang,slangp,sql,srt,ssa,ts,txt,url,vbs,vcxproj,xhtml,xml,xul,yml";
    const QString DEFAULT_HIDE_EXTS = "3gp,7z,aac,asf,ass,avi,avif,bmp,c,cbz,chm,cpp,css,cue,cxx,doc,epub,flac,gif,h,heic,heif,htm,html,ini,iso,jpeg,jpg,jxl,log,lua,m4a,md,mid,mkv,mp3,mp4,mpg,nfo,odp,ods,odt,ogg,ogm,pdf,png,ppt,py,qoi,rar,reg,srt,ssa,sxw,tga,tif,tiff,txt,url,wav,webm,webp,wmv,xcf,xml,xul,yml,zip";

    // Former per-type extension lists (until 2026-11), only read once to migrate them to HideExts/ContentSearchExts
    const QStringList LEGACY_EXT_KEYS = { "Extensions/FileExtAudio", "Extensions/FileExtImage", "Extensions/FileExtMisc",
                                          "Extensions/FileExtText", "Extensions/FileExtVideo" };

    QString DEFAULT_MERGE_TOOL;
    QString DEFAULT_FILE_MANAGER;
    QString DEFAULT_SEARCH_TOOL;
    QString DEFAULT_RENAME_TOOL;

    bool DEFAULT_ALTERNATING_ROW_COLORS;
    bool DEFAULT_EXECUTABLE_FILES_RED;
    bool DEFAULT_MENU_ON_MOUSE_UP;
    bool DEFAULT_SHOW_GRID;
    bool DEFAULT_SHOW_ICONS_IN_MENU;
    bool DEFAULT_SHOW_PLACEHOLDER_TEXT;
    bool DEFAULT_SHOW_SHORTCUTS_IN_MENU;
    bool DEFAULT_ALLOW_LAUNCHER_SHRINK;
    bool DEFAULT_SHOW_FILE_EXTENSIONS;
    QString DEFAULT_SEARCH_FOLDERS;
    bool DEFAULT_USE_KDE_FILE_OPERATIONS;
};

// Settings dialog shared by all mkTools programs (Ctrl+Shift+Comma), covers all values of mkTools.conf
class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget *parent = nullptr);

    void loadFrom(const SettingsManager &settings);
    void saveTo(SettingsManager &settings) const;

private:
    void addPage(const QString &iconName, const QString &title, QWidget *page);
    QWidget *createDisplayPage();
    QWidget *createMenusPage();
    QWidget *createFileTypesPage();
    QWidget *createProgramsPage();
    QWidget *createLauncherPage();
    void restoreDefaults();
    void addSearchFolder();

    QListWidget *m_categoryList = nullptr;
    QStackedWidget *m_pages = nullptr;

    // Display
    QCheckBox *m_checkAlternatingRowColors = nullptr;
    QCheckBox *m_checkShowGrid = nullptr;
    QCheckBox *m_checkExecutableFilesRed = nullptr;
    QCheckBox *m_checkShowFileExtensions = nullptr;
    QCheckBox *m_checkShowPlaceholderText = nullptr;

    // Menus & Mouse
    QCheckBox *m_checkMenuOnMouseUp = nullptr;
    QCheckBox *m_checkShowIconsInMenu = nullptr;
    QCheckBox *m_checkShowShortcutsInMenu = nullptr;

    // File types
    QPlainTextEdit *m_editHideExts = nullptr;
    QPlainTextEdit *m_editContentSearchExts = nullptr;

    // Programs
    QLineEdit *m_editFileManager = nullptr;
    QLineEdit *m_editRenameTool = nullptr;
    QLineEdit *m_editSearchTool = nullptr;
    QLineEdit *m_editMergeTool = nullptr;
    QCheckBox *m_checkUseKdeFileOperations = nullptr;

    // mkLauncher
    QCheckBox *m_checkAllowLauncherShrink = nullptr;
    QListWidget *m_listSearchFolders = nullptr;
};

#endif // SETTINGSMANAGER_H
