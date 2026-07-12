#ifndef SETTINGSMANAGER_H
#define SETTINGSMANAGER_H

#include <QSet>
#include <QSettings>
#include <QString>

class SettingsManager {
public:
	SettingsManager();

	void load();
    void saveSettings();
    QString getSettingsPath();

	QSet<QString> audioExts;
	QSet<QString> imageExts;
    QSet<QString> miscExts;
	QSet<QString> textExts;
	QSet<QString> videoExts;
    QSet<QString> knownExts;

	QString audioEditor;
	QString imageEditor;
	QString textEditor;
	QString videoEditor;
	QString fileManager;
    QString searchTool;
    QString renameTool;

	bool alternatingRowColors;
    bool executableFilesRed;
    bool menuOnMouseUp;
	bool showGrid;
    bool showIconsInMenu;
	bool showPlaceholderText;
    bool showShortcutsInMenu;
    bool showFileExtensions;
    int fontSizeOverride;
    QString fontNameOverride;

    void saveHistory();
    QStringList searchFolders;
    QStringList recentOpenList;
    QStringList recentInputList;

private:
	QSet<QString> parseExtensions(const QString &input);
    void safeSetValue(QSettings &settings, const QString &key, const QVariant &value);
    QString formatStringSet(const QSet<QString> &extensionSet);
    void getDefaults();

    QStringList importSearchFolders(const QString &input);
    QString exportSearchFolders(const QStringList &folders);
    QString getHistoryPath();

    // Defaults
    const QString DEFAULT_AUDIO = "aac,flac,m4a,mid,mp3,ogg,wav";
    const QString DEFAULT_IMAGE = "avif,bmp,gif,heic,heif,jpg,jpeg,jxl,png,qoi,tga,tif,tiff,webp,xcf";
    const QString DEFAULT_TEXT  = "ahk,ass,au3,bat,c,cfg,conf,cpp,cs,css,cue,cxx,desktop,dic,dsf,dsk,duf,h,hpp,htm,html,inf,ini,ion,js,json,log,lst,lua,md,nfo,py,rc,reg,scp,sfv,sh,slang,slangp,sql,srt,ssa,ts,txt,url,vbs,vcxproj,xhtml,xml,xul,yml";
    const QString DEFAULT_VIDEO = "3gp,asf,avi,mkv,mov,mp4,mpg,ogm,ts,wm,wmv,webm";
    const QString DEFAULT_MISC  = "cbz,doc,epub,ods,odt,pdf,ppt,zip";

    QString DEFAULT_AUDIO_EDITOR;
    QString DEFAULT_IMAGE_EDITOR;
    QString DEFAULT_TEXT_EDITOR;
    QString DEFAULT_VIDEO_EDITOR;
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
    bool DEFAULT_SHOW_FILE_EXTENSIONS;
    int DEFAULT_FONT_SIZE_OVERRIDE;
    QString DEFAULT_FONT_NAME_OVERRIDE;
    QString DEFAULT_SEARCH_FOLDERS;
};

#endif // SETTINGSMANAGER_H
