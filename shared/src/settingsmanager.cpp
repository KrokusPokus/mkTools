#include "settingsmanager.h"
#include "helpers.h"

#include <QDir>
#include <QProcessEnvironment>
#include <QStandardPaths>

#include <algorithm>

SettingsManager::SettingsManager() {
	load(); // Loads automatically on creation
}

void SettingsManager::load() {
    getDefaults();

    QSettings s(getSettingsPath(), QSettings::IniFormat);

    // Extensions
    // Migration: the former per-type lists together were the hidden extensions, the text list was the content search list
    QString hideExtsDefault = DEFAULT_HIDE_EXTS;
    if (!s.contains("Extensions/HideExts")
        && std::any_of(LEGACY_EXT_KEYS.begin(), LEGACY_EXT_KEYS.end(), [&s](const QString &key) { return s.contains(key); })) {
        QStringList legacyLists;
        for (const QString &key : LEGACY_EXT_KEYS) {
            legacyLists << s.value(key).toString();
        }
        hideExtsDefault = legacyLists.join(u',');
    }
    const QString contentSearchExtsDefault = s.value("Extensions/FileExtText", DEFAULT_CONTENT_SEARCH_EXTS).toString();

    hideExts          = parseExtensions(s.value("Extensions/HideExts", hideExtsDefault).toString());
    contentSearchExts = parseExtensions(s.value("Extensions/ContentSearchExts", contentSearchExtsDefault).toString());

    // Handlers
	mergeTool        = s.value("Handlers/MergeTool", DEFAULT_MERGE_TOOL).toString();
    fileManager      = s.value("Handlers/FileManager", DEFAULT_FILE_MANAGER).toString();
    renameTool       = s.value("Handlers/RenameTool", DEFAULT_RENAME_TOOL).toString();
    searchTool       = s.value("Handlers/SearchTool", DEFAULT_SEARCH_TOOL).toString();

    // Interface
    alternatingRowColors = s.value("Interface/AlternatingRowColors", DEFAULT_ALTERNATING_ROW_COLORS).toBool();
    executableFilesRed   = s.value("Interface/ExecutableFilesRed", DEFAULT_EXECUTABLE_FILES_RED).toBool();
    fontNameOverride     = s.value("Interface/FontNameOverride", DEFAULT_FONT_NAME_OVERRIDE).toString();
    fontSizeOverride     = s.value("Interface/FontSizeOverride", DEFAULT_FONT_SIZE_OVERRIDE).toInt();
    menuOnMouseUp        = s.value("Interface/MenuOnMouseUp", DEFAULT_MENU_ON_MOUSE_UP).toBool();
    showFileExtensions   = s.value("Interface/ShowFileExtensions", DEFAULT_SHOW_FILE_EXTENSIONS).toBool();
    showGrid             = s.value("Interface/ShowGrid", DEFAULT_SHOW_GRID).toBool();
    showIconsInMenu      = s.value("Interface/ShowIconsInMenu", DEFAULT_SHOW_ICONS_IN_MENU).toBool();
    showPlaceholderText  = s.value("Interface/ShowPlaceholderText", DEFAULT_SHOW_PLACEHOLDER_TEXT).toBool();
    showShortcutsInMenu  = s.value("Interface/ShowShortcutsInMenu", DEFAULT_SHOW_SHORTCUTS_IN_MENU).toBool();
    allowLauncherShrink  = s.value("Interface/AllowLauncherShrink", DEFAULT_ALLOW_LAUNCHER_SHRINK).toBool();

    // mkLauncher specific
    searchFolders = importSearchFolders(s.value("mkLauncher/UserFolders", DEFAULT_SEARCH_FOLDERS).toString());

    // mkTransactionHandler specific
    useKdeFileOperations = s.value("mkTransactionHandler/UseKdeFileOperations", DEFAULT_USE_KDE_FILE_OPERATIONS).toBool();

    saveSettings();
}

void SettingsManager::getDefaults() {
    QStringList searchPaths;

    DEFAULT_MERGE_TOOL = "libremerge";
    DEFAULT_FILE_MANAGER = "mkFolderWidget";
    DEFAULT_RENAME_TOOL = "mkBatchRename";
    DEFAULT_SEARCH_TOOL = "mkFileSearch";

    DEFAULT_ALTERNATING_ROW_COLORS = true;
    DEFAULT_EXECUTABLE_FILES_RED = false;
    DEFAULT_FONT_NAME_OVERRIDE = "";
    DEFAULT_FONT_SIZE_OVERRIDE = 0;
    DEFAULT_MENU_ON_MOUSE_UP = false;
    DEFAULT_SHOW_FILE_EXTENSIONS = true;
    DEFAULT_SHOW_GRID = false;
    DEFAULT_SHOW_ICONS_IN_MENU = true;
    DEFAULT_SHOW_PLACEHOLDER_TEXT = true;
    DEFAULT_SHOW_SHORTCUTS_IN_MENU = true;
    DEFAULT_ALLOW_LAUNCHER_SHRINK = true;

    searchPaths << QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    searchPaths << QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    searchPaths << QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
    searchPaths << QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    searchPaths << QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    searchPaths << QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);

    // Hand copying/moving/deleting completely over to KIO (KDE dialogs and Plasma progress display)
    DEFAULT_USE_KDE_FILE_OPERATIONS = true;

    // Join the list with the pipe symbol |
    DEFAULT_SEARCH_FOLDERS = searchPaths.join("|");
}

void SettingsManager::saveSettings() {
    QSettings s(getSettingsPath(), QSettings::IniFormat);

    // Extensions
    for (const QString &legacyKey : LEGACY_EXT_KEYS) {
        if (s.contains(legacyKey)) s.remove(legacyKey);
    }
    safeSetValue(s, "Extensions/HideExts", formatStringSet(hideExts));
    safeSetValue(s, "Extensions/ContentSearchExts", formatStringSet(contentSearchExts));

    // Handlers
    // Obsolete: the edit action now uses the 2nd application of the "Open with" menu
    for (const char *obsoleteKey : {"Handlers/EditorAudio", "Handlers/EditorImage", "Handlers/EditorText", "Handlers/EditorVideo"}) {
        if (s.contains(obsoleteKey)) s.remove(obsoleteKey);
    }
	safeSetValue(s, "Handlers/MergeTool", mergeTool);
    safeSetValue(s, "Handlers/FileManager", fileManager);
    safeSetValue(s, "Handlers/RenameTool", renameTool);
    safeSetValue(s, "Handlers/SearchTool", searchTool);

    // Interface
    safeSetValue(s, "Interface/AlternatingRowColors", alternatingRowColors);
    safeSetValue(s, "Interface/ExecutableFilesRed", executableFilesRed);
    safeSetValue(s, "Interface/FontNameOverride", fontNameOverride);
    safeSetValue(s, "Interface/FontSizeOverride", fontSizeOverride);
    safeSetValue(s, "Interface/MenuOnMouseUp", menuOnMouseUp);
    safeSetValue(s, "Interface/ShowGrid", showGrid);
    safeSetValue(s, "Interface/ShowPlaceholderText", showPlaceholderText);
    safeSetValue(s, "Interface/ShowIconsInMenu", showIconsInMenu);
    safeSetValue(s, "Interface/ShowShortcutsInMenu", showShortcutsInMenu);
    safeSetValue(s, "Interface/AllowLauncherShrink", allowLauncherShrink);
    safeSetValue(s, "Interface/ShowFileExtensions", showFileExtensions);

    // mkLauncher specific
    //safeSetValue(s, "mkLauncher/UserFolders", exportSearchFolders(searchFolders));    // searchFolders contains the expanded paths. Since we don't change this value in the gui, no reason to overwrite it in the ini file.

    // mkTransactionHandler specific
    safeSetValue(s, "mkTransactionHandler/UseKdeFileOperations", useKdeFileOperations);

    // Because we used safeSetValue(), the file will only get written if there were changes in values.
	s.sync();
}

void SettingsManager::safeSetValue(QSettings &settings, const QString &key, const QVariant &value) {
    if (settings.value(key) != value) {
        settings.setValue(key, value);
    }
}

QString SettingsManager::formatStringSet(const QSet<QString> &extensionSet) {
    QStringList stringList = extensionSet.values();
    stringList.sort(Qt::CaseInsensitive);
    return stringList.join(",");
    }

QSet<QString> SettingsManager::parseExtensions(const QString &input) {
    const QStringList list = input.split(u',', Qt::SkipEmptyParts);

	QSet<QString> set;
    set.reserve(list.size());

	for (const QString &s : list) {
		set.insert(s.trimmed().toLower());
	}

	return set;
}

// Shared by all mkTools programs: ~/.config/mkProjects/mkTools.conf
QString SettingsManager::getSettingsPath() {
    const QString configDir = QDir(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)).filePath("mkProjects");
    QDir().mkpath(configDir);
    return QDir(configDir).filePath("mkTools.conf");
}

QStringList SettingsManager::importSearchFolders(const QString &input) {
    QString rawInput = input;

    QStringList searchFolders = rawInput.split(u'|', Qt::SkipEmptyParts);

    for (QString &path : searchFolders) {
        path = Helpers::expandPath(path);
        QDir dir(path);
        path = QDir::cleanPath(dir.absolutePath());
    }

    searchFolders.removeDuplicates();

    return searchFolders;
}

QString SettingsManager::exportSearchFolders(const QStringList &folders) {
    QStringList workingCopy = folders;

    workingCopy.removeDuplicates();

    QString homePath = QDir::homePath();

    for (QString &path : workingCopy) {
        if (path.startsWith(homePath)) {
            path.replace(0, homePath.length(), "$HOME");
        }
    }

    return workingCopy.join(u'|');
}
