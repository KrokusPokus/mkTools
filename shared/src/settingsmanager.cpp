#include "settingsmanager.h"
#include "helpers.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFontComboBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLineEdit>
#include <QListWidget>
#include <QPalette>
#include <QPlainTextEdit>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

SettingsManager::SettingsManager() {
	load(); // Loads automatically on creation
}

SettingsManager::SettingsManager(DefaultsOnly) {
    // Neither reads nor writes the settings file
    getDefaults();

    hideExts          = parseExtensions(DEFAULT_HIDE_EXTS);
    contentSearchExts = parseExtensions(DEFAULT_CONTENT_SEARCH_EXTS);

    mergeTool   = DEFAULT_MERGE_TOOL;
    fileManager = DEFAULT_FILE_MANAGER;
    renameTool  = DEFAULT_RENAME_TOOL;
    searchTool  = DEFAULT_SEARCH_TOOL;

    alternatingRowColors = DEFAULT_ALTERNATING_ROW_COLORS;
    executableFilesRed   = DEFAULT_EXECUTABLE_FILES_RED;
    fontNameOverride     = DEFAULT_FONT_NAME_OVERRIDE;
    fontSizeOverride     = DEFAULT_FONT_SIZE_OVERRIDE;
    menuOnMouseUp        = DEFAULT_MENU_ON_MOUSE_UP;
    showFileExtensions   = DEFAULT_SHOW_FILE_EXTENSIONS;
    showGrid             = DEFAULT_SHOW_GRID;
    showIconsInMenu      = DEFAULT_SHOW_ICONS_IN_MENU;
    showPlaceholderText  = DEFAULT_SHOW_PLACEHOLDER_TEXT;
    showShortcutsInMenu  = DEFAULT_SHOW_SHORTCUTS_IN_MENU;
    allowLauncherShrink  = DEFAULT_ALLOW_LAUNCHER_SHRINK;

    searchFolders = importSearchFolders(DEFAULT_SEARCH_FOLDERS);

    useKdeFileOperations = DEFAULT_USE_KDE_FILE_OPERATIONS;
}

bool SettingsManager::showSettingsDialog(QWidget *parent) {
    // The dialog shows the settings file, not this instance: programs change some members at runtime
    // (e.g. mkBatchRename always shows file extensions), those values must not end up in the file
    SettingsManager fileSettings;

    SettingsDialog dlg(parent);
    dlg.loadFrom(fileSettings);
    if (dlg.exec() != QDialog::Accepted) {
        return false;
    }
    dlg.saveTo(fileSettings);
    load();
    return true;
}

// saveSettings() does not write the search folders (they are expanded on loading), only the settings dialog does
void SettingsManager::saveSearchFolders() {
    QSettings s(getSettingsPath(), QSettings::IniFormat);
    safeSetValue(s, "mkLauncher/UserFolders", exportSearchFolders(searchFolders));
    s.sync();
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

// ============================================================================
// SETTINGS DIALOG
// ============================================================================

namespace {

    // Form of a page: labels right aligned, fields in their natural size, the form centered at the top
    QFormLayout *createFormLayout(QWidget *panel) {
        QFormLayout *layout = new QFormLayout(panel);
        layout->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
        layout->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
        layout->setFormAlignment(Qt::AlignHCenter | Qt::AlignTop);
        layout->setContentsMargins(10, 10, 10, 10);
        return layout;
    }

    // Every page in a scroll area for small screen resolutions
    QWidget *makeScrollable(QWidget *content) {
        QScrollArea *area = new QScrollArea();
        area->setWidgetResizable(true);
        area->setFrameShape(QFrame::NoFrame);
        area->setWidget(content);
        return area;
    }

    // ~/.local/state/mkProjects/mkTools_state.conf: the dialog is shared by all mkTools programs, so is its state
    QString dialogStatePath() {
        const QString stateDir = QDir(QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation)).filePath("mkProjects");
        QDir().mkpath(stateDir);
        return QDir(stateDir).filePath("mkTools_state.conf");
    }

    QPlainTextEdit *createExtensionsEdit() {
        QPlainTextEdit *edit = new QPlainTextEdit();
        edit->setLineWrapMode(QPlainTextEdit::WidgetWidth);
        edit->setTabChangesFocus(true);
        edit->setMinimumWidth(300);
        edit->setFixedHeight(edit->fontMetrics().lineSpacing() * 6 + 2 * edit->frameWidth() + 8);
        edit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        return edit;
    }

    // Shown separated by ", " so the text can wrap
    QString extensionsToText(const QSet<QString> &extensions) {
        QStringList list = extensions.values();
        list.sort(Qt::CaseInsensitive);
        return list.join(QStringLiteral(", "));
    }

    // Accepts commas, semicolons and whitespace as separators and a leading dot ("*.txt; .md" -> "txt,md")
    QString extensionsFromText(const QString &text) {
        static const QRegularExpression separators(QStringLiteral("[,;\\s]+"));
        QStringList list;
        for (QString ext : text.split(separators, Qt::SkipEmptyParts)) {
            while (ext.startsWith(u'*') || ext.startsWith(u'.')) {
                ext.remove(0, 1);
            }
            if (!ext.isEmpty()) {
                list << ext;
            }
        }
        return list.join(u',');
    }

    QLineEdit *createHandlerEdit(const QString &defaultValue) {
        QLineEdit *edit = new QLineEdit();
        edit->setMinimumWidth(300);
        edit->setPlaceholderText(defaultValue);
        edit->setClearButtonEnabled(true);
        return edit;
    }

} // namespace

SettingsDialog::SettingsDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle(tr("mkTools Settings"));
    resize(780, 560);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    QHBoxLayout *sectionsLayout = new QHBoxLayout();
    m_categoryList = new QListWidget(this);
    m_categoryList->setIconSize(QSize(24, 24));
    m_categoryList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // The selected section stays clearly marked when the list does not have the focus (the theme's inactive
    // selection is dimmed): inactive selection colors = active ones, the colors of the theme stay
    QPalette listPalette = m_categoryList->palette();
    listPalette.setColor(QPalette::Inactive, QPalette::Highlight, listPalette.color(QPalette::Active, QPalette::Highlight));
    listPalette.setColor(QPalette::Inactive, QPalette::HighlightedText, listPalette.color(QPalette::Active, QPalette::HighlightedText));
    m_categoryList->setPalette(listPalette);
    m_pages = new QStackedWidget(this);
    sectionsLayout->addWidget(m_categoryList);
    sectionsLayout->addWidget(m_pages, 1);
    mainLayout->addLayout(sectionsLayout);

    addPage(QStringLiteral("view-list-details"), tr("Display"), createDisplayPage());
    addPage(QStringLiteral("application-menu"), tr("Menus & Mouse"), createMenusPage());
    addPage(QStringLiteral("document-properties"), tr("File Types"), createFileTypesPage());
    addPage(QStringLiteral("preferences-desktop-default-applications"), tr("Programs"), createProgramsPage());
    addPage(QStringLiteral("system-search"), tr("mkLauncher"), createLauncherPage());

    // The list is as wide as its longest entry
    m_categoryList->setFixedWidth(m_categoryList->sizeHintForColumn(0) + 2 * m_categoryList->frameWidth() + 8);
    connect(m_categoryList, &QListWidget::currentRowChanged, m_pages, &QStackedWidget::setCurrentIndex);
    const int lastPage = QSettings(dialogStatePath(), QSettings::IniFormat).value(QStringLiteral("State/SettingsPage"), 0).toInt();
    m_categoryList->setCurrentRow(std::clamp(lastPage, 0, m_pages->count() - 1));
    // connected after the initial selection: only a page the user chooses is remembered
    connect(m_categoryList, &QListWidget::currentRowChanged, this, [](int row) {
        if (row >= 0) {
            QSettings(dialogStatePath(), QSettings::IniFormat).setValue(QStringLiteral("State/SettingsPage"), row);
        }
    });

    QDialogButtonBox *buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::RestoreDefaults, this);
    QPushButton *btnRestoreDefaults = buttonBox->button(QDialogButtonBox::RestoreDefaults);
    btnRestoreDefaults->setToolTip(tr("Resets all options to their defaults; nothing is saved until OK"));
    connect(btnRestoreDefaults, &QPushButton::clicked, this, &SettingsDialog::restoreDefaults);

    // Editing the file by hand: the dialog closes without saving, so it does not overwrite the manual changes
    QPushButton *btnEditFile = buttonBox->addButton(tr("Edit File…"), QDialogButtonBox::ActionRole);
    btnEditFile->setIcon(QIcon::fromTheme(QStringLiteral("document-edit")));
    const QString path = SettingsManager::getSettingsPath();
    btnEditFile->setToolTip(tr("Opens %1 in the text editor; the dialog is closed without saving").arg(path));
    connect(btnEditFile, &QPushButton::clicked, this, [this, path]() {
        Helpers::openWithSystemDefault(QUrl::fromLocalFile(path));
        reject();
    });

    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttonBox);
}

void SettingsDialog::addPage(const QString &iconName, const QString &title, QWidget *page) {
    // Icon from the desktop theme (Breeze under KDE); no icon if the theme does not have it
    m_categoryList->addItem(new QListWidgetItem(QIcon::fromTheme(iconName), title));
    m_pages->addWidget(page);
}

// ----------------------------------------------------------------------------
// Page "Display": file lists and font
// ----------------------------------------------------------------------------
QWidget *SettingsDialog::createDisplayPage() {
    QWidget *panel = new QWidget();
    QFormLayout *layout = createFormLayout(panel);

    m_checkAlternatingRowColors = new QCheckBox(tr("Alternating row colors"));
    m_checkAlternatingRowColors->setToolTip(tr("AlternatingRowColors"));
    m_checkShowGrid = new QCheckBox(tr("Show grid"));
    m_checkShowGrid->setToolTip(tr("Grid lines in the details view (ShowGrid)"));
    m_checkExecutableFilesRed = new QCheckBox(tr("Show executable files in red"));
    m_checkExecutableFilesRed->setToolTip(tr("ExecutableFilesRed"));
    layout->addRow(tr("File list:"), m_checkAlternatingRowColors);
    layout->addRow("", m_checkShowGrid);
    layout->addRow("", m_checkExecutableFilesRed);

    m_checkShowFileExtensions = new QCheckBox(tr("Show all file extensions"));
    m_checkShowFileExtensions->setToolTip(tr("If off, the extensions listed under \"File Types\" are hidden; "
                                             "mkBatchRename always shows them (ShowFileExtensions)"));
    layout->addRow("", m_checkShowFileExtensions);

    m_checkShowPlaceholderText = new QCheckBox(tr("Show hints in empty input fields"));
    m_checkShowPlaceholderText->setToolTip(tr("Takes effect after a restart (ShowPlaceholderText)"));
    layout->addRow(tr("Input fields:"), m_checkShowPlaceholderText);

    m_comboFontName = new QFontComboBox();
    m_comboFontName->setEditable(true);
    m_comboFontName->lineEdit()->setPlaceholderText(tr("System default"));
    m_comboFontName->lineEdit()->setClearButtonEnabled(true);
    m_comboFontName->setToolTip(tr("Empty = font of the desktop. Takes effect after a restart (FontNameOverride)"));
    m_spinFontSize = new QSpinBox();
    m_spinFontSize->setRange(0, 99);
    m_spinFontSize->setSuffix(tr(" pt"));
    m_spinFontSize->setSpecialValueText(tr("System default"));
    m_spinFontSize->setToolTip(tr("0 = size of the desktop font. Takes effect after a restart (FontSizeOverride)"));
    layout->addRow(tr("Font:"), m_comboFontName);
    layout->addRow(tr("Font size:"), m_spinFontSize);

    return makeScrollable(panel);
}

// ----------------------------------------------------------------------------
// Page "Menus & Mouse": context menus
// ----------------------------------------------------------------------------
QWidget *SettingsDialog::createMenusPage() {
    QWidget *panel = new QWidget();
    QFormLayout *layout = createFormLayout(panel);

    m_checkMenuOnMouseUp = new QCheckBox(tr("Open the context menu when the right mouse button is released"));
    m_checkMenuOnMouseUp->setToolTip(tr("Off: the menu opens when the button is pressed (MenuOnMouseUp)"));
    layout->addRow(tr("Context menu:"), m_checkMenuOnMouseUp);

    m_checkShowIconsInMenu = new QCheckBox(tr("Show icons"));
    m_checkShowIconsInMenu->setToolTip(tr("Takes full effect after a restart (ShowIconsInMenu)"));
    m_checkShowShortcutsInMenu = new QCheckBox(tr("Show keyboard shortcuts"));
    m_checkShowShortcutsInMenu->setToolTip(tr("Takes effect after a restart (ShowShortcutsInMenu)"));
    layout->addRow("", m_checkShowIconsInMenu);
    layout->addRow("", m_checkShowShortcutsInMenu);

    return makeScrollable(panel);
}

// ----------------------------------------------------------------------------
// Page "File Types": hidden extensions, content search
// ----------------------------------------------------------------------------
QWidget *SettingsDialog::createFileTypesPage() {
    QWidget *panel = new QWidget();
    QFormLayout *layout = createFormLayout(panel);
    layout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);  // the lists use the width of the page

    m_editHideExts = createExtensionsEdit();
    m_editHideExts->setToolTip(tr("Extensions that are hidden when \"Show all file extensions\" is off, "
                                  "separated by commas (HideExts)"));
    layout->addRow(tr("Hidden extensions:"), m_editHideExts);

    m_editContentSearchExts = createExtensionsEdit();
    m_editContentSearchExts->setToolTip(tr("Files whose content mkFileSearch searches, separated by commas (ContentSearchExts)"));
    layout->addRow(tr("Content search:"), m_editContentSearchExts);

    return makeScrollable(panel);
}

// ----------------------------------------------------------------------------
// Page "Programs": handlers, file operations
// ----------------------------------------------------------------------------
QWidget *SettingsDialog::createProgramsPage() {
    QWidget *panel = new QWidget();
    QFormLayout *layout = createFormLayout(panel);
    const SettingsManager defaults{SettingsManager::DefaultsOnly{}};
    const QString handlerHint = tr(".desktop ID (e.g. org.kde.dolphin.desktop), program name or path");

    m_editFileManager = createHandlerEdit(defaults.fileManager);
    m_editFileManager->setToolTip(tr("Shows a file in its folder. %1 (FileManager)").arg(handlerHint));
    m_editRenameTool = createHandlerEdit(defaults.renameTool);
    m_editRenameTool->setToolTip(tr("Renames the selected files. %1 (RenameTool)").arg(handlerHint));
    m_editSearchTool = createHandlerEdit(defaults.searchTool);
    m_editSearchTool->setToolTip(tr("Searches the current folder (Ctrl+Shift+F). %1 (SearchTool)").arg(handlerHint));
    m_editMergeTool = createHandlerEdit(tr("None"));
    m_editMergeTool->setToolTip(tr("Compares two files when copying runs into a conflict, empty = no compare button. "
                                   "%1 (MergeTool)").arg(handlerHint));
    layout->addRow(tr("File manager:"), m_editFileManager);
    layout->addRow(tr("Rename tool:"), m_editRenameTool);
    layout->addRow(tr("Search tool:"), m_editSearchTool);
    layout->addRow(tr("Compare tool:"), m_editMergeTool);

    m_checkUseKdeFileOperations = new QCheckBox(tr("Copy, move and delete with KDE (KIO)"));
    m_checkUseKdeFileOperations->setToolTip(tr("KDE dialogs and progress in the Plasma notifications instead of "
                                               "mkTransactionHandler's own (UseKdeFileOperations)"));
    layout->addRow(tr("File operations:"), m_checkUseKdeFileOperations);

    return makeScrollable(panel);
}

// ----------------------------------------------------------------------------
// Page "mkLauncher": window, search folders
// ----------------------------------------------------------------------------
QWidget *SettingsDialog::createLauncherPage() {
    QWidget *panel = new QWidget();
    QFormLayout *layout = createFormLayout(panel);

    m_checkAllowLauncherShrink = new QCheckBox(tr("Window shrinks to fit the results"));
    m_checkAllowLauncherShrink->setToolTip(tr("AllowLauncherShrink"));
    layout->addRow(tr("Window:"), m_checkAllowLauncherShrink);

    m_listSearchFolders = new QListWidget();
    m_listSearchFolders->setMinimumWidth(380);
    m_listSearchFolders->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_listSearchFolders->setToolTip(tr("Folders mkLauncher searches; double click to edit (UserFolders)"));

    QPushButton *btnAdd = new QPushButton(QIcon::fromTheme(QStringLiteral("list-add")), tr("Add…"));
    QPushButton *btnRemove = new QPushButton(QIcon::fromTheme(QStringLiteral("list-remove")), tr("Remove"));
    connect(btnAdd, &QPushButton::clicked, this, &SettingsDialog::addSearchFolder);
    connect(btnRemove, &QPushButton::clicked, this, [this]() { qDeleteAll(m_listSearchFolders->selectedItems()); });
    auto updateRemove = [this, btnRemove]() { btnRemove->setEnabled(!m_listSearchFolders->selectedItems().isEmpty()); };
    connect(m_listSearchFolders, &QListWidget::itemSelectionChanged, this, updateRemove);
    updateRemove();

    QWidget *foldersRow = new QWidget();
    QHBoxLayout *foldersLayout = new QHBoxLayout(foldersRow);
    foldersLayout->setContentsMargins(0, 0, 0, 0);
    foldersLayout->addWidget(m_listSearchFolders);
    QVBoxLayout *buttonsLayout = new QVBoxLayout();
    buttonsLayout->addWidget(btnAdd);
    buttonsLayout->addWidget(btnRemove);
    buttonsLayout->addStretch();
    foldersLayout->addLayout(buttonsLayout);
    layout->addRow(tr("Search folders:"), foldersRow);

    return makeScrollable(panel);
}

void SettingsDialog::addSearchFolder() {
    const QListWidgetItem *current = m_listSearchFolders->currentItem();
    const QString startDir = current ? current->text() : QDir::homePath();
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Add Search Folder"), startDir);
    if (dir.isEmpty()) {
        return;
    }
    const QString path = QDir::cleanPath(dir);
    if (!m_listSearchFolders->findItems(path, Qt::MatchExactly).isEmpty()) {
        return;
    }
    QListWidgetItem *item = new QListWidgetItem(path, m_listSearchFolders);
    item->setFlags(item->flags() | Qt::ItemIsEditable);
    m_listSearchFolders->setCurrentItem(item);
}

void SettingsDialog::restoreDefaults() {
    // Only the controls change; the file is written on OK, Cancel keeps the stored settings
    loadFrom(SettingsManager(SettingsManager::DefaultsOnly{}));
}

void SettingsDialog::loadFrom(const SettingsManager &settings) {
    // Display
    m_checkAlternatingRowColors->setChecked(settings.alternatingRowColors);
    m_checkShowGrid->setChecked(settings.showGrid);
    m_checkExecutableFilesRed->setChecked(settings.executableFilesRed);
    m_checkShowFileExtensions->setChecked(settings.showFileExtensions);
    m_checkShowPlaceholderText->setChecked(settings.showPlaceholderText);
    if (settings.fontNameOverride.isEmpty()) {
        m_comboFontName->setCurrentIndex(-1);
        m_comboFontName->setEditText(QString());
    } else {
        m_comboFontName->setCurrentFont(QFont(settings.fontNameOverride));
        m_comboFontName->setEditText(settings.fontNameOverride);
    }
    m_spinFontSize->setValue((settings.fontSizeOverride > 0 && settings.fontSizeOverride < 100) ? settings.fontSizeOverride : 0);

    // Menus & Mouse
    m_checkMenuOnMouseUp->setChecked(settings.menuOnMouseUp);
    m_checkShowIconsInMenu->setChecked(settings.showIconsInMenu);
    m_checkShowShortcutsInMenu->setChecked(settings.showShortcutsInMenu);

    // File types
    m_editHideExts->setPlainText(extensionsToText(settings.hideExts));
    m_editContentSearchExts->setPlainText(extensionsToText(settings.contentSearchExts));

    // Programs
    m_editFileManager->setText(settings.fileManager);
    m_editRenameTool->setText(settings.renameTool);
    m_editSearchTool->setText(settings.searchTool);
    m_editMergeTool->setText(settings.mergeTool);
    m_checkUseKdeFileOperations->setChecked(settings.useKdeFileOperations);

    // mkLauncher
    m_checkAllowLauncherShrink->setChecked(settings.allowLauncherShrink);
    m_listSearchFolders->clear();
    for (const QString &folder : settings.searchFolders) {
        QListWidgetItem *item = new QListWidgetItem(folder, m_listSearchFolders);
        item->setFlags(item->flags() | Qt::ItemIsEditable);
    }
}

void SettingsDialog::saveTo(SettingsManager &settings) const {
    // Display
    settings.alternatingRowColors = m_checkAlternatingRowColors->isChecked();
    settings.showGrid             = m_checkShowGrid->isChecked();
    settings.executableFilesRed   = m_checkExecutableFilesRed->isChecked();
    settings.showFileExtensions   = m_checkShowFileExtensions->isChecked();
    settings.showPlaceholderText  = m_checkShowPlaceholderText->isChecked();
    settings.fontNameOverride     = m_comboFontName->currentText().trimmed();
    settings.fontSizeOverride     = m_spinFontSize->value();

    // Menus & Mouse
    settings.menuOnMouseUp       = m_checkMenuOnMouseUp->isChecked();
    settings.showIconsInMenu     = m_checkShowIconsInMenu->isChecked();
    settings.showShortcutsInMenu = m_checkShowShortcutsInMenu->isChecked();

    // File types
    settings.hideExts          = settings.parseExtensions(extensionsFromText(m_editHideExts->toPlainText()));
    settings.contentSearchExts = settings.parseExtensions(extensionsFromText(m_editContentSearchExts->toPlainText()));

    // Programs (an empty field gives the default, the placeholder shows it; no merge tool is a valid choice)
    const SettingsManager defaults{SettingsManager::DefaultsOnly{}};
    auto handler = [](const QLineEdit *edit, const QString &defaultValue) {
        const QString text = edit->text().trimmed();
        return text.isEmpty() ? defaultValue : text;
    };
    settings.fileManager = handler(m_editFileManager, defaults.fileManager);
    settings.renameTool  = handler(m_editRenameTool, defaults.renameTool);
    settings.searchTool  = handler(m_editSearchTool, defaults.searchTool);
    settings.mergeTool   = m_editMergeTool->text().trimmed();
    settings.useKdeFileOperations = m_checkUseKdeFileOperations->isChecked();

    // mkLauncher
    settings.allowLauncherShrink = m_checkAllowLauncherShrink->isChecked();
    QStringList folders;
    for (int row = 0; row < m_listSearchFolders->count(); ++row) {
        const QString folder = m_listSearchFolders->item(row)->text().trimmed();
        if (!folder.isEmpty()) {
            folders << QDir::cleanPath(Helpers::expandPath(folder));
        }
    }
    folders.removeDuplicates();

    settings.saveSettings();

    // Written only when changed: the file may contain other variables than $HOME, those stay as they are
    if (folders != settings.searchFolders) {
        settings.searchFolders = folders;
        settings.saveSearchFolders();
    }
}
