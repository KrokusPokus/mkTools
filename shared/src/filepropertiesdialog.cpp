#include "filepropertiesdialog.h"
#include "helpers.h"    // needed for Helpers::formatAdaptiveSize()

#include <QFileIconProvider>
#include <QDateTime>
#include <QDirIterator>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QMimeDatabase>
#include <QMimeType>
#include <QStack>

#include <unistd.h> // Needed for getuid()

FilePropertiesDialog::FilePropertiesDialog(const QStringList &filePaths, QWidget *parent)
    : QDialog(parent), m_filePaths(filePaths) {

    setAttribute(Qt::WA_DeleteOnClose);

    if (m_filePaths.isEmpty()) {
        reject();
        return;
    }

    m_isMultiMode = (m_filePaths.size() > 1);

    if (m_isMultiMode) {
        setupUiMultiMode();
        initiateStatsCalculation();
        setWindowTitle(tr("Properties of %1 elements").arg(m_filePaths.size()));
    } else {
        QFileInfo fileInfo(m_filePaths.first());
        setupUi(fileInfo);
        setWindowTitle(tr("Properties of %1").arg(fileInfo.fileName()));
    }
    setWindowIcon(QIcon(":/icons/info.ico"));
}

FilePropertiesDialog::~FilePropertiesDialog() {
    m_abort.store(true);
    if (m_updateTimer) {
        m_updateTimer->stop();
    }

    m_watcher.disconnect();

    if (m_watcher.future().isRunning()) {
        m_watcher.future().waitForFinished();
    }
}

void FilePropertiesDialog::done(int r) {
    m_abort.store(true);
    if (m_updateTimer) {
        m_updateTimer->stop();
    }

    m_watcher.disconnect();

    if (m_watcher.future().isRunning()) {
        m_watcher.future().waitForFinished();
    }
    QDialog::done(r);       // call default behaviour (closes window)
}

void FilePropertiesDialog::setupUiMultiMode() {
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // --- Header ---
    auto *headerLayout = new QHBoxLayout();
    headerLayout->setContentsMargins(30, 25, 30, 25);
    headerLayout->setSpacing(25);
    m_iconLabel = new QLabel();

    m_iconLabel->setPixmap(QIcon(":/icons/multi.ico").pixmap(48, 48));

    auto *summaryLabel = new QLabel(tr("<strong>%n element selected</strong>", nullptr, m_filePaths.size()));
    summaryLabel->setStyleSheet("font-size: 14px;");

    headerLayout->addWidget(m_iconLabel);
    headerLayout->addWidget(summaryLabel);
    headerLayout->addStretch();
    mainLayout->addLayout(headerLayout);

    QFrame *line1 = new QFrame();
    line1->setContentsMargins(20, 10, 20, 10);
    line1->setFrameShape(QFrame::HLine);
    line1->setFrameShadow(QFrame::Sunken);
    mainLayout->addWidget(line1);

    // --- Aggregated info ---
    auto *formLayout = new QFormLayout();
    formLayout->setContentsMargins(30, 10, 30, 30);
    formLayout->setSpacing(10);
    formLayout->setHorizontalSpacing(20);
    formLayout->setLabelAlignment(Qt::AlignLeft);

    m_containsLabel = new QLabel(tr("Calculating content..."));
    m_containsLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_sizeLabel = new QLabel(tr("Calculating size..."));
    m_sizeLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_typeLabel1 = new QLabel(tr("Multiple types"));

    formLayout->addRow(tr("Type:"), m_typeLabel1);
    formLayout->addRow(tr("Total size:"), m_sizeLabel);
    formLayout->addRow(tr("Content:"), m_containsLabel);

    mainLayout->addLayout(formLayout);

    mainLayout->addStretch(); // Pushes everything up

    QFrame *line2 = new QFrame();
    line2->setFrameShape(QFrame::HLine);
    mainLayout->addWidget(line2);

    // Buttons
    auto *buttonLayout = new QHBoxLayout();
    buttonLayout->setContentsMargins(5, 5, 5, 5);
    buttonLayout->setSpacing(10);
    auto *okBtn = new QPushButton(tr("OK"));
    auto *cancelBtn = new QPushButton(tr("Cancel"));
    buttonLayout->addStretch();
    buttonLayout->addWidget(okBtn);
    buttonLayout->addWidget(cancelBtn);
    mainLayout->addLayout(buttonLayout);

    connect(okBtn, &QPushButton::clicked, this, &FilePropertiesDialog::onOkPressed);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
}

void FilePropertiesDialog::setupUi(const QFileInfo &fileInfo) {
    QString absoluteFilePath = fileInfo.absoluteFilePath();

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    auto *headerLayout = new QHBoxLayout();
    headerLayout->setContentsMargins(30, 25, 30, 25);
    headerLayout->setSpacing(25);

    m_iconLabel = new QLabel();
    QFileIconProvider provider;
    QIcon icon = provider.icon(fileInfo);
    QPixmap pix = icon.pixmap(QSize(48, 48));

    QPixmap thumb = Helpers::generateThumbnail(fileInfo);
    if (!thumb.isNull()) {
        pix = thumb;
    }

    m_iconLabel->setPixmap(pix);
    m_nameEdit = new QLineEdit(fileInfo.fileName());
    m_nameEdit->setStyleSheet("QLineEdit { padding-left: 7px; padding-right: 7px; padding-top: 5px; padding-bottom: 5px;}");
    int textWidth = m_nameEdit->fontMetrics().horizontalAdvance(m_nameEdit->text());
    m_nameEdit->setMinimumWidth(textWidth + 22);

    headerLayout->addWidget(m_iconLabel);
    headerLayout->addWidget(m_nameEdit);
    mainLayout->addLayout(headerLayout);

    QFrame *line1 = new QFrame();
    line1->setContentsMargins(20, 10, 20, 10);
    line1->setFrameShape(QFrame::HLine);
    line1->setFrameShadow(QFrame::Sunken);
    mainLayout->addWidget(line1);

    //---------------------------------------------------------------------------------------------------------------------------------------------------------
    // fileinfo

    QGridLayout *fileinfoGridLayout = new QGridLayout();

    fileinfoGridLayout->setContentsMargins(20, 10, 20, 20);
    fileinfoGridLayout->setHorizontalSpacing(0);
    fileinfoGridLayout->setVerticalSpacing(10);

    const int colLeftMargin  = 0; // Virtual margin left
    const int colLabel       = 1; // The labels go here
    const int colSpacingH    = 2; // Horizontal spacing
    const int colField       = 3; // The values/fields go here
    const int colRightMargin = 4; // Virtual margin right

    // Width of spacer columns
    fileinfoGridLayout->setColumnMinimumWidth(colLeftMargin, 10);
    fileinfoGridLayout->setColumnMinimumWidth(colSpacingH, 20);
    fileinfoGridLayout->setColumnMinimumWidth(colRightMargin, 10);

    // Set colField to stretch dynamically
    fileinfoGridLayout->setColumnStretch(colField, 1);

    int row = 0;

    // ================= BLOCK 1 =================
    m_typeLabel1 = new QLabel();
    fileinfoGridLayout->addWidget(new QLabel(tr("Type:")), row, colLabel);
    m_typeLabel1->setText(getFileType(fileInfo));
    m_typeLabel1->setTextInteractionFlags(Qt::TextSelectableByMouse);
    fileinfoGridLayout->addWidget(m_typeLabel1, row, colField);
    row++;


    m_typeLabel2 = new QLabel();
    fileinfoGridLayout->addWidget(new QLabel(tr("MimeType:")), row, colLabel);
    QMimeDatabase mimeDb;
    QMimeType mime = mimeDb.mimeTypeForFile(absoluteFilePath);  // .mimeTypeForFile goes by file extension AND if neccessary by content (magic bytes). Can be restricted by mimeTypeForFile(absoluteFilePath, QMimeDatabase::MatchExtension)
    m_typeLabel2->setText(mime.name());
    m_typeLabel2->setTextInteractionFlags(Qt::TextSelectableByMouse);
    fileinfoGridLayout->addWidget(m_typeLabel2, row, colField);
    row++;


    fileinfoGridLayout->addWidget(new QLabel(tr("Path:")), row, colLabel);
    m_pathEdit = new QLabel();
    m_pathEdit->setText(fileInfo.absolutePath());
    m_pathEdit->setTextInteractionFlags(Qt::TextSelectableByMouse);
    fileinfoGridLayout->addWidget(m_pathEdit, row, colField);
    row++;

    fileinfoGridLayout->addWidget(new QLabel(tr("Size:")), row, colLabel);

    m_sizeLabel = new QLabel();

    if (fileInfo.isDir()) {
        m_sizeLabel->setText(tr("Calculating size..."));    // will be changed again by initiateStatsCalculation() later on
    }
    else {
        if (fileInfo.size() <= 1024) {
            m_sizeLabel->setText(Helpers::formatAdaptiveSize(fileInfo.size()));
        } else {
            m_sizeLabel->setText(tr("%1 (%2 Bytes)").arg(Helpers::formatAdaptiveSize(fileInfo.size())).arg(m_locale.toString(fileInfo.size())));
        }
    }

    m_sizeLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    fileinfoGridLayout->addWidget(m_sizeLabel, row, colField);
    row++;

    if (fileInfo.isSymbolicLink()) {
        if (fileInfo.isDir()) {
            fileinfoGridLayout->addWidget(new QLabel(tr("Content:")), row, colLabel);
            m_containsLabel = new QLabel(tr("Calculating content..."));
            m_containsLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
            fileinfoGridLayout->addWidget(m_containsLabel, row, colField);
            row++;
        }

        // ================= SEPARATOR LINE =================
        QFrame *line5 = new QFrame();
        line5->setFrameShape(QFrame::HLine);
        line5->setFrameShadow(QFrame::Sunken);
        // The line starts at column 0 (far left) and spans all 4 columns!
        // Parameters: addWidget(widget, row, column, rowSpan, columnSpan)
        fileinfoGridLayout->addWidget(line5, row, colLeftMargin, 1, 5);
        row++;

        QString SLTpath = fileInfo.symLinkTarget();
        QString SLTpathRaw = fileInfo.readSymLink();

        fileinfoGridLayout->addWidget(new QLabel(tr("Target:")), row, colLabel);
        m_linkTargetLabel = new QLabel();
        m_linkTargetLabel->setText(SLTpath);
        m_linkTargetLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        fileinfoGridLayout->addWidget(m_linkTargetLabel, row, colField);
        row++;

        if (SLTpathRaw != SLTpath) {
            fileinfoGridLayout->addWidget(new QLabel(tr("Target (raw):")), row, colLabel);
            m_linkTargetLabelRaw = new QLabel();
            m_linkTargetLabelRaw->setText(SLTpathRaw);
            m_linkTargetLabelRaw->setTextInteractionFlags(Qt::TextSelectableByMouse);
            fileinfoGridLayout->addWidget(m_linkTargetLabelRaw, row, colField);
            row++;
        }
    }
    else if (fileInfo.isDir()) {
        fileinfoGridLayout->addWidget(new QLabel(tr("Content:")), row, colLabel);
        m_containsLabel = new QLabel(tr("Calculating content..."));
        m_containsLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        fileinfoGridLayout->addWidget(m_containsLabel, row, colField);
        row++;
    }

    // ================= SEPARATOR LINE =================
    QFrame *line6 = new QFrame();
    line6->setFrameShape(QFrame::HLine);
    line6->setFrameShadow(QFrame::Sunken);
    // The line starts at column 0 (far left) and spans all 4 columns!
    // Parameters: addWidget(widget, row, column, rowSpan, columnSpan)
    fileinfoGridLayout->addWidget(line6, row, colLeftMargin, 1, 5);
    row++;

    {
        const QString timeCreated = fileInfo.birthTime().toString("yyyy-MM-dd  HH:mm:ss");
        const QString timeModified = fileInfo.lastModified().toString("yyyy-MM-dd  HH:mm:ss");
        const QString timeAccessed = fileInfo.lastRead().toString("yyyy-MM-dd  HH:mm:ss");

        // ================= BLOCK 2 =================
        fileinfoGridLayout->addWidget(new QLabel(tr("Created:")), row, colLabel);
        m_createdLabel = new QLabel();
        m_createdLabel->setText(timeCreated);
        m_createdLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        fileinfoGridLayout->addWidget(m_createdLabel, row, colField);
        row++;

        fileinfoGridLayout->addWidget(new QLabel(tr("Modified:")), row, colLabel);
        m_modifiedLabel = new QLabel();
        m_modifiedLabel->setText(timeModified);
        m_modifiedLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        fileinfoGridLayout->addWidget(m_modifiedLabel, row, colField);
        row++;

        fileinfoGridLayout->addWidget(new QLabel(tr("Accessed:")), row, colLabel);
        m_lastReadLabel = new QLabel();
        m_lastReadLabel->setText(timeAccessed);
        m_lastReadLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        fileinfoGridLayout->addWidget(m_lastReadLabel, row, colField);
        row++;

        // ================= SEPARATOR LINE =================
        QFrame *line3 = new QFrame();
        line3->setFrameShape(QFrame::HLine);
        line3->setFrameShadow(QFrame::Sunken);
        fileinfoGridLayout->addWidget(line3, row, colLeftMargin, 1, 5);
        row++;
    }

    m_ownerLabel = new QLabel();
    m_ownerLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_groupLabel = new QLabel();
    m_groupLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_permLabel = new QLabel(); // Label for the octal display (e.g. 755)
    m_permLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    fileinfoGridLayout->addWidget(new QLabel(tr("Owner:")), row, colLabel);
    fileinfoGridLayout->addWidget(m_ownerLabel, row, colField);
    row++;

    fileinfoGridLayout->addWidget(new QLabel(tr("Group:")), row, colLabel);
    fileinfoGridLayout->addWidget(m_groupLabel, row, colField);
    row++;

    fileinfoGridLayout->addWidget(new QLabel(tr("Permissions:")), row, colLabel);
    fileinfoGridLayout->addWidget(m_permLabel, row, colField);
    row++;

    // A small matrix for the overview
    auto *permGrid = new QGridLayout();
    permGrid->setContentsMargins(0,0,0,0);
    permGrid->setSpacing(5);
    permGrid->setAlignment(Qt::AlignLeft);
    permGrid->setColumnStretch(4, 1);

    // Header
    permGrid->addWidget(new QLabel(tr("Read")), 0, 1);
    permGrid->addWidget(new QLabel(tr("Write")), 0, 2);
    permGrid->addWidget(new QLabel(tr("eXecute")), 0, 3);

    // Row helper
    QStringList rows = { tr("Owner"), tr("Group"), tr("Others") };
    for (int i = 0; i < 3; ++i) {
        permGrid->addWidget(new QLabel(rows[i]), i + 1, 0);
        for (int j = 0; j < 3; ++j) {
            QCheckBox *cb = new QCheckBox();
            m_permCBs[i][j] = cb;
            permGrid->addWidget(cb, i + 1, j + 1);
        }
    }

    fileinfoGridLayout->addLayout(permGrid, row, colField);
    row++;

    // Fill
    QString ownerName = fileInfo.owner();
    if (ownerName.isEmpty()) {
        m_ownerLabel->setText(QString::number(fileInfo.ownerId()));
    } else {
        m_ownerLabel->setText(tr("%1 (%2)").arg(ownerName).arg(fileInfo.ownerId()));
    }

    QString groupName = fileInfo.group();
    if (groupName.isEmpty()) {
        m_groupLabel->setText(QString::number(fileInfo.groupId()));
    } else {
        m_groupLabel->setText(tr("%1 (%2)").arg(groupName).arg(fileInfo.groupId()));
    }

    QFile::Permissions p = fileInfo.permissions();
    bool canModify = canModifyPermissions(fileInfo);

    int owner = ((p & QFile::ReadOwner) ? 4 : 0) + ((p & QFile::WriteOwner) ? 2 : 0) + ((p & QFile::ExeOwner) ? 1 : 0);
    int group = ((p & QFile::ReadGroup) ? 4 : 0) + ((p & QFile::WriteGroup) ? 2 : 0) + ((p & QFile::ExeGroup) ? 1 : 0);
    int other = ((p & QFile::ReadOther) ? 4 : 0) + ((p & QFile::WriteOther) ? 2 : 0) + ((p & QFile::ExeOther) ? 1 : 0);

    m_permLabel->setText(QString("0%1%2%3").arg(owner).arg(group).arg(other));

    m_permCBs[0][0]->setChecked(p & QFile::ReadOwner);
    m_permCBs[0][1]->setChecked(p & QFile::WriteOwner);
    m_permCBs[0][2]->setChecked(p & QFile::ExeOwner);
    m_permCBs[0][0]->setEnabled(canModify);
    m_permCBs[0][1]->setEnabled(canModify);
    m_permCBs[0][2]->setEnabled(canModify);

    m_permCBs[1][0]->setChecked(p & QFile::ReadGroup);
    m_permCBs[1][1]->setChecked(p & QFile::WriteGroup);
    m_permCBs[1][2]->setChecked(p & QFile::ExeGroup);
    m_permCBs[1][0]->setEnabled(canModify);
    m_permCBs[1][1]->setEnabled(canModify);
    m_permCBs[1][2]->setEnabled(canModify);

    m_permCBs[2][0]->setChecked(p & QFile::ReadOther);
    m_permCBs[2][1]->setChecked(p & QFile::WriteOther);
    m_permCBs[2][2]->setChecked(p & QFile::ExeOther);
    m_permCBs[2][0]->setEnabled(canModify);
    m_permCBs[2][1]->setEnabled(canModify);
    m_permCBs[2][2]->setEnabled(canModify);

    // ================= SEPARATOR LINE =================
    QFrame *line4 = new QFrame();
    line4->setFrameShape(QFrame::HLine);
    line4->setFrameShadow(QFrame::Sunken);
    fileinfoGridLayout->addWidget(line4, row, colLeftMargin, 1, 5);
    row++;

    mainLayout->addLayout(fileinfoGridLayout);

    // Buttons
    auto *buttonLayout = new QHBoxLayout();
    buttonLayout->setContentsMargins(5, 5, 5, 5);
    buttonLayout->setSpacing(10);
    auto *okBtn = new QPushButton(tr("OK"));
    auto *cancelBtn = new QPushButton(tr("Cancel"));
    buttonLayout->addStretch();
    buttonLayout->addWidget(okBtn);
    buttonLayout->addWidget(cancelBtn);
    mainLayout->addLayout(buttonLayout);

    connect(okBtn, &QPushButton::clicked, this, &FilePropertiesDialog::onOkPressed);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

    if (fileInfo.isDir()) {
        initiateStatsCalculation();
    }
}


void FilePropertiesDialog::updateGuiLabelText(const ProgressResult &result) {
    if (m_abort.load()) return;

    QString sizeInfoDisplay;

    if (result.followedSize <= 1024) {
        sizeInfoDisplay = Helpers::formatAdaptiveSize(result.followedSize);
    } else {
        sizeInfoDisplay = tr("%1 (%2 Bytes)")
        .arg(Helpers::formatAdaptiveSize(result.followedSize))
            .arg(m_locale.toString(result.followedSize));
    }

    if (result.followedSize != result.directSize) {
        QString bonusInfo;
        if (result.directSize <= 1024) {
            bonusInfo = Helpers::formatAdaptiveSize(result.directSize);
        } else {
            bonusInfo = tr("%1 (%2 Bytes)")
            .arg(Helpers::formatAdaptiveSize(result.directSize))
                .arg(m_locale.toString(result.directSize));
        }

        sizeInfoDisplay += tr("  Real: ") + bonusInfo;
    }

    m_sizeLabel->setText(sizeInfoDisplay);

    if (m_containsLabel) { // Null pointer check since m_containsLabel is only created in single file mode if isDir()
        QString contentInfoDisplay;

        contentInfoDisplay = tr("%1 Files, %2 Folders")
                                 .arg(m_locale.toString(result.followedFiles))
                                 .arg(m_locale.toString(result.followedFolders));

        if (result.followedFiles != result.directFiles || result.followedFolders != result.directFolders) {
            QString contentBonusInfo = tr("%1 Files, %2 Folders")
            .arg(m_locale.toString(result.directFiles))
                .arg(m_locale.toString(result.directFolders));

            contentInfoDisplay += tr("  Real: ") + contentBonusInfo;
        }

        m_containsLabel->setText(contentInfoDisplay);
    }
}

void FilePropertiesDialog::initiateStatsCalculation() {
    m_progress.reset();

    if (!m_updateTimer) {
        m_updateTimer = new QTimer(this);
        connect(m_updateTimer, &QTimer::timeout, this, [this]() {
            ProgressResult currentRes;
            m_progress.snapshot(currentRes);
            updateGuiLabelText(currentRes);
        });
    }
    m_updateTimer->start(200);

    m_watcher.disconnect();

    connect(&m_watcher, &QFutureWatcher<void>::finished, this, [this]() {
        if (m_updateTimer) {
            m_updateTimer->stop();
        }

        ProgressResult finalRes;
        m_progress.snapshot(finalRes);
        updateGuiLabelText(finalRes);
    });

    // Copy variables for the thread by value (thread-safe)
    const QStringList filePathsCopy = m_filePaths;

    auto computeTask = [filePathsCopy, &res = m_progress, &abort = m_abort]() {
        calculateStats(filePathsCopy, res, abort);
    };

    m_watcher.setFuture(QtConcurrent::run(computeTask));
}


void FilePropertiesDialog::calculateStats(const QStringList &filePaths,
                                          AtomicProgressResult &res,
                                          const std::atomic<bool> &abortFlag) {
    const bool isMultiMode = (filePaths.count() > 1);
    QSet<QString> visitedDirs;

    struct StackItem {
        QString path;
        bool isSymlinkBranch = false;
    };

    QStack<StackItem> dirStack;

    // Helper lambda for a uniform evaluation
    auto processInfo = [&](const QFileInfo &info, bool isSymlinkBranch, bool isTopLevel) {
        // --- 2. SYMLINKS & JUNCTIONS ---
        if (info.isSymbolicLink() || info.isJunction()) {
            // The link itself
            if (!isSymlinkBranch) {
                res.directFiles++;
                res.directSize += info.size();
            }

           // Resolve the target
            QString targetPath = info.isJunction() ? info.junctionTarget() : info.symLinkTarget();

            QFileInfo targetInfo(targetPath);
            if (targetInfo.exists()) {
                res.followedFiles++;
                res.followedSize += targetInfo.size();
            } else {
                // Dead symlink
                res.followedFiles++;
                res.followedSize += info.size();
            }
        }
        // --- 3. NORMALE ORDNER ---
        else if (info.isDir()) {
            if (isTopLevel) {
                // In multi mode, we also count selected top-level folders
                if (isMultiMode) {
                    res.directFolders++;
                    res.followedFolders++;
                }
            } else {
                // Normal subfolder counting
                res.followedFolders++;
                if (!isSymlinkBranch) {
                    res.directFolders++;
                }
            }
            dirStack.push({info.absoluteFilePath(), isSymlinkBranch});
        }
        // --- 4. NORMALE DATEIEN ---
        else {
            res.followedFiles++;
            res.followedSize += info.size();

            if (!isSymlinkBranch) {
                res.directFiles++;
                res.directSize += info.size();
            }
        }
    };

    // --- PHASE 1: Process top-level items ---
    for (const QString &path : filePaths) {
        if (abortFlag.load()) return;
        // Call: no symlink branch (false), but top level (true)
        processInfo(QFileInfo(path), false, true);
    }

    // --- PHASE 2: Stack-based traversal ---
    while (!dirStack.isEmpty()) {
        if (abortFlag.load()) return;

        StackItem current = dirStack.pop();
        QFileInfo dirInfo(current.path);
        QString canonical = dirInfo.canonicalFilePath();

        // Broken path or loop protection for circular symlinks
        if (canonical.isEmpty() || visitedDirs.contains(canonical)) {
            continue;
        }
        visitedDirs.insert(canonical);

        QDirIterator it(current.path, QDir::Files | QDir::Dirs | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);

        while (it.hasNext()) {
            if (abortFlag.load()) return;

            it.next();
            // Call: inherits the symlink status from the parent, but not top level (false)
            processInfo(it.fileInfo(), current.isSymlinkBranch, false);
        }
    }
}

QString FilePropertiesDialog::getFileType(const QFileInfo &info) {
    if (info.isSymbolicLink()) {
        return tr("SymLink");
    }
    else if (info.isDir()) {
        return tr("Folder");
    }

    return info.suffix().isEmpty() ? tr("File") : info.suffix().toUpper() + tr("-File");
}

void FilePropertiesDialog::onOkPressed() {
    if (!m_isMultiMode && !m_filePaths.isEmpty()) {
        QString filePath = m_filePaths.first();

        QFile::Permissions newPerms = QFile::Permissions();

        // 1. Owner permissions (row 0)
        if (m_permCBs[0][0]->isChecked()) newPerms |= QFile::ReadOwner;
        if (m_permCBs[0][1]->isChecked()) newPerms |= QFile::WriteOwner;
        if (m_permCBs[0][2]->isChecked()) newPerms |= QFile::ExeOwner;

        // 2. Group permissions (row 1)
        if (m_permCBs[1][0]->isChecked()) newPerms |= QFile::ReadGroup;
        if (m_permCBs[1][1]->isChecked()) newPerms |= QFile::WriteGroup;
        if (m_permCBs[1][2]->isChecked()) newPerms |= QFile::ExeGroup;

        // 3. Other permissions (row 2)
        if (m_permCBs[2][0]->isChecked()) newPerms |= QFile::ReadOther;
        if (m_permCBs[2][1]->isChecked()) newPerms |= QFile::WriteOther;
        if (m_permCBs[2][2]->isChecked()) newPerms |= QFile::ExeOther;

        // Apply the permissions to the file system
        if (!QFile::setPermissions(filePath, newPerms)) {
            QMessageBox::warning(this, tr("Error"),
                                 tr("Failed to set permissions for:\n%1\n\nDo you have sufficient rights?").arg(filePath));
            return; // Don't close the dialog, so that the user can correct it
        }
    }
    accept();
}

bool FilePropertiesDialog::canModifyPermissions(const QFileInfo &fileInfo) {
    uid_t currentUid = getuid();

    // 1. If we are root (ID 0), we are allowed to do anything.
    // 2. If we own the file, we may change the permissions.
    if (currentUid == 0 || fileInfo.ownerId() == currentUid) {
        return true;
    }
    return false;
}
