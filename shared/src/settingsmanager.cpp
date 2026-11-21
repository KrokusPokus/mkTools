#include "settingsmanager.h"
#include "helpers.h"

#include <QDir>
#include <QProcessEnvironment>
#include <QStandardPaths>

SettingsManager::SettingsManager() {
	load(); // Loads automatically on creation
}

void SettingsManager::load() {
    getDefaults();

    QSettings s(getSettingsPath(), QSettings::IniFormat);

	// Extensions
	audioExts = parseExtensions(s.value("Extensions/FileExtAudio", DEFAULT_AUDIO).toString());
	imageExts = parseExtensions(s.value("Extensions/FileExtImage", DEFAULT_IMAGE).toString());
    miscExts  = parseExtensions(s.value("Extensions/FileExtMisc", DEFAULT_MISC).toString());
	textExts  = parseExtensions(s.value("Extensions/FileExtText",  DEFAULT_TEXT).toString());
	videoExts = parseExtensions(s.value("Extensions/FileExtVideo", DEFAULT_VIDEO).toString());

    knownExts = audioExts | imageExts | textExts | videoExts | miscExts;

    // Handlers
    audioEditor      = s.value("Handlers/EditorAudio", DEFAULT_AUDIO_EDITOR).toString();
    imageEditor      = s.value("Handlers/EditorImage", DEFAULT_IMAGE_EDITOR).toString();
    textEditor       = s.value("Handlers/EditorText", DEFAULT_TEXT_EDITOR).toString();
    videoEditor      = s.value("Handlers/EditorVideo", DEFAULT_VIDEO_EDITOR).toString();
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

    DEFAULT_AUDIO_EDITOR = "org.kde.kwave.desktop";
    DEFAULT_IMAGE_EDITOR = "gimp.desktop";
    DEFAULT_TEXT_EDITOR = "org.kde.kate.desktop";
    DEFAULT_VIDEO_EDITOR = "org.kde.kdenlive.desktop";
    DEFAULT_MERGE_TOOL = "";
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
    safeSetValue(s, "Extensions/FileExtAudio", formatStringSet(audioExts));
    safeSetValue(s, "Extensions/FileExtImage", formatStringSet(imageExts));
    safeSetValue(s, "Extensions/FileExtMisc", formatStringSet(miscExts));
    safeSetValue(s, "Extensions/FileExtText", formatStringSet(textExts));
    safeSetValue(s, "Extensions/FileExtVideo", formatStringSet(videoExts));

    // Handlers
    safeSetValue(s, "Handlers/EditorAudio", audioEditor);
    safeSetValue(s, "Handlers/EditorImage", imageEditor);
    safeSetValue(s, "Handlers/EditorText", textEditor);
    safeSetValue(s, "Handlers/EditorVideo", videoEditor);
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
