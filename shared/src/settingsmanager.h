#ifndef SETTINGSMANAGER_H
#define SETTINGSMANAGER_H

#include <QSet>
#include <QSettings>
#include <QString>
#include <QStringList>

class SettingsManager {
public:
	SettingsManager();

	void load();
    void saveSettings();
    QString getSettingsPath();

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
    int fontSizeOverride;
    QString fontNameOverride;

    QStringList searchFolders;

    // mkTransactionHandler specific
    bool useKdeFileOperations;

private:
	QSet<QString> parseExtensions(const QString &input);
    void safeSetValue(QSettings &settings, const QString &key, const QVariant &value);
    QString formatStringSet(const QSet<QString> &extensionSet);
    void getDefaults();

    QStringList importSearchFolders(const QString &input);
    QString exportSearchFolders(const QStringList &folders);

    // Defaults
    const QString DEFAULT_HIDE_EXTS = "3gp,aac,ahk,asf,ass,au3,avi,avif,bat,bmp,c,cbz,cfg,conf,cpp,cs,css,cue,cxx,desktop,dic,doc,dsf,dsk,duf,epub,flac,gif,h,heic,heif,hpp,htm,html,inf,ini,ion,jpeg,jpg,js,json,jxl,log,lst,lua,m4a,md,mid,mkv,mov,mp3,mp4,mpg,nfo,ods,odt,ogg,ogm,pdf,png,ppt,py,qoi,rc,reg,scp,sfv,sh,slang,slangp,sql,srt,ssa,tga,tif,tiff,ts,txt,url,vbs,vcxproj,wav,webm,webp,wm,wmv,xcf,xhtml,xml,xul,yml,zip";
    const QString DEFAULT_CONTENT_SEARCH_EXTS = "ahk,ass,au3,bat,c,cfg,conf,cpp,cs,css,cue,cxx,desktop,dic,dsf,dsk,duf,h,hpp,htm,html,inf,ini,ion,js,json,log,lst,lua,md,nfo,py,rc,reg,scp,sfv,sh,slang,slangp,sql,srt,ssa,ts,txt,url,vbs,vcxproj,xhtml,xml,xul,yml";

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
    int DEFAULT_FONT_SIZE_OVERRIDE;
    QString DEFAULT_FONT_NAME_OVERRIDE;
    QString DEFAULT_SEARCH_FOLDERS;
    bool DEFAULT_USE_KDE_FILE_OPERATIONS;
};

#endif // SETTINGSMANAGER_H
