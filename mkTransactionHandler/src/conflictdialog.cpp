#include "conflictdialog.h"

#include <QDateTime>
#include <QDir>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

ConflictDialog::ConflictDialog(const Conflict &conflict, QWidget *parent)
    : QDialog(parent)
{

    setWindowTitle(tr("Target already exists"));

    QFileInfo srcInfo(conflict.sourcePath);
    QFileInfo dstInfo(conflict.targetPath);
    QString srcType = getTypeString(srcInfo);
    QString dstType = getTypeString(dstInfo);

    bool isDirConflict = srcInfo.isDir() && dstInfo.isDir();
    QString actionWord = isDirConflict ? tr("Merge") : tr("Replace");
    bool isDestinationNewer = dstInfo.lastModified() > srcInfo.lastModified();

    // Layouts
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(15);

    // Grid for data
    auto *gridLayout = new QGridLayout();
    gridLayout->setHorizontalSpacing(16);
    gridLayout->setVerticalSpacing(4);
    gridLayout->setContentsMargins(10, 0, 10, 0);

    // Line 1: Source Header
    QLabel *lblSrcIcon = new QLabel(this);
    lblSrcIcon->setPixmap(generateIconOrThumbnail(srcInfo));
    lblSrcIcon->setAlignment(Qt::AlignCenter);
    lblSrcIcon->setContentsMargins(8, 8, 8, 8);

    QLabel *pathSrcLabel = new QLabel(conflict.sourcePath, this);
    pathSrcLabel->setWordWrap(true);

    // Line 2: Source Data
    auto *lblSrcTitle = new QLabel(QString("<b>%1</b>").arg(tr("Source")), this);
    lblSrcTitle->setAlignment(Qt::AlignCenter);

    auto *lblSrcTime  = new QLabel(srcInfo.lastModified().toString("yyyy-MM-dd  HH:mm:ss"), this);
    lblSrcTime->setAlignment(Qt::AlignCenter);

    auto *lblSrcSizeBytes = new QLabel(tr("%1 Bytes").arg(m_locale.toString(srcInfo.size())), this);
    lblSrcSizeBytes->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    auto *lblSrcType  = new QLabel(srcType, this);
    lblSrcType->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    // Line 3: Destination Data
    auto *lblDstTitle = new QLabel(QString("<b>%1</b>").arg(tr("Target")), this);
    lblDstTitle->setAlignment(Qt::AlignCenter);

    UnderlinedLabel *lblDstTime = new UnderlinedLabel(this);
    lblDstTime->setText(dstInfo.lastModified().toString("yyyy-MM-dd  HH:mm:ss"));
    lblDstTime->setAlignment(Qt::AlignCenter);
    lblDstTime->setUnderlineVisible(isDestinationNewer);

    auto *lblDstSizeBytes = new QLabel(tr("%1 Bytes").arg(m_locale.toString(dstInfo.size())), this);
    lblDstSizeBytes->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    auto *lblDstType  = new QLabel(dstType, this);
    lblDstType->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    // Line 4: Destination Header
    QLabel *lblDstIcon = new QLabel(this);
    lblDstIcon->setPixmap(generateIconOrThumbnail(dstInfo));
    lblDstIcon->setAlignment(Qt::AlignCenter);
    lblDstIcon->setContentsMargins(8, 8, 8, 8);

    QLabel *pathDstLabel = new QLabel(conflict.targetPath, this);
    pathDstLabel->setWordWrap(true);

    // Fill the grid
    int iCol = 0;
    gridLayout->addWidget(lblSrcIcon,  0, iCol);
    gridLayout->addWidget(lblSrcTitle, 1, iCol);
    gridLayout->addWidget(lblDstTitle, 2, iCol);
    gridLayout->addWidget(lblDstIcon,  3, iCol);
    iCol++;

    int pathStartCol = iCol;

    gridLayout->addWidget(lblSrcType,  1, iCol);
    gridLayout->addWidget(lblDstType,  2, iCol);
    iCol++;

    gridLayout->addWidget(lblSrcSizeBytes, 1, iCol);
    gridLayout->addWidget(lblDstSizeBytes, 2, iCol);
    iCol++;

    if (srcInfo.size() >= 1024 || dstInfo.size() >= 1024) {
        auto *lblSrcSizeShort = new QLabel(Helpers::formatAdaptiveSize(srcInfo.size()), this);
        lblSrcSizeShort->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        gridLayout->addWidget(lblSrcSizeShort, 1, iCol);

        auto *lblDstSizeShort = new QLabel(Helpers::formatAdaptiveSize(dstInfo.size()), this);
        lblDstSizeShort->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        gridLayout->addWidget(lblDstSizeShort, 2, iCol);

        iCol++;
    }

    gridLayout->addWidget(lblSrcTime,   1, iCol);
    gridLayout->addWidget(lblDstTime,   2, iCol);
    iCol++;

    QLabel *warningLabel = new QLabel(this);
    if (isDestinationNewer) {
        warningLabel->setText("<b><font color='#ff4040'>" + tr("&larr; destination is newer!") + "</font></b>");
        gridLayout->addWidget(warningLabel, 2, iCol);
    }

    if (!m_settings.mergeTool.isEmpty()) {
        if (isDestinationNewer) {
            iCol++;
        }

        auto *btnCompare = new QPushButton(tr("Compare..."), this);
        gridLayout->addWidget(btnCompare, 1, iCol, 2, 1, Qt::AlignCenter);
        btnCompare->setIcon(QIcon::fromTheme("document-revert"));

        connect(btnCompare, &QPushButton::clicked, this, [this, conflict]() {
            openCompareTool(conflict.sourcePath, conflict.targetPath);
        });
    }

    // 1. Span the path labels dynamically across the remaining columns
    int totalPathSpan = iCol - pathStartCol + 1;
    gridLayout->addWidget(pathSrcLabel, 0, pathStartCol, 1, totalPathSpan);
    gridLayout->addWidget(pathDstLabel, 3, pathStartCol, 1, totalPathSpan);

    // 0 = column does not grow (stays as compact as possible)
    // 1 = column shares the remaining space of the window
    // All leading columns (0 to iCol-1) stay compact (0)
    for (int c = 0; c < iCol; ++c) {
        gridLayout->setColumnStretch(c, 0);
    }
    // The very last column gets stretch 1
    gridLayout->setColumnStretch(iCol, 1);

    mainLayout->addLayout(gridLayout);

    // Separator line
    auto *line = new QFrame(this);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    mainLayout->addWidget(line);

    // Buttons
    auto *btnLayout = new QHBoxLayout();
    btnLayout->setSpacing(8);

    auto *btnOverwrite    = new QPushButton(actionWord, this);
    auto *btnOverwriteAll = new QPushButton(actionWord + tr(" all"), this);
    auto *btnSkip         = new QPushButton(tr("Skip"), this);
    auto *btnSkipAll      = new QPushButton(tr("Skip") + tr(" all"), this);
    auto *btnCancel       = new QPushButton(tr("Cancel"), this);

    btnOverwrite->setDefault(true);

    btnLayout->addWidget(btnOverwrite);
    btnLayout->addWidget(btnOverwriteAll);
    btnLayout->addSpacing(20);
    btnLayout->addWidget(btnSkip);
    btnLayout->addWidget(btnSkipAll);
    btnLayout->addSpacing(20);
    btnLayout->addWidget(btnCancel);
    mainLayout->addLayout(btnLayout);

    // Connects
    connect(btnOverwrite, &QPushButton::clicked, this, [this]() {
        m_result = { ConflictResolution::Overwrite, false }; accept();
    });
    connect(btnOverwriteAll, &QPushButton::clicked, this, [this]() {
        m_result = { ConflictResolution::Overwrite, true }; accept();
    });
    connect(btnSkip, &QPushButton::clicked, this, [this]() {
        m_result = { ConflictResolution::Skip, false }; accept();
    });
    connect(btnSkipAll, &QPushButton::clicked, this, [this]() {
        m_result = { ConflictResolution::Skip, true }; accept();
    });
    connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
}

void ConflictDialog::openCompareTool(const QString &pathA, const QString &pathB) {
    Helpers::openFileListWithHandler(m_settings.mergeTool, { pathA, pathB});
}

QString ConflictDialog::getTypeString(const QFileInfo &fileInfo) {
    if (fileInfo.isJunction())     return tr("Junction");
    if (fileInfo.isSymbolicLink()) return tr("SymLink");
    if (fileInfo.isDir())          return tr("Directory");

    return tr("File");
}

    QPixmap ConflictDialog::generateIconOrThumbnail(const QFileInfo &fileInfo) {
        QPixmap pix = m_iconProvider.icon(fileInfo).pixmap(48, 48);

        QString ext = fileInfo.suffix().toLower();
        if (ext == "cur" || ext == "ico" || ext == "icns") {
            return pix;
        }

        // 2. Cache the supported formats statically (created only once on the very first call)
        static const QSet<QByteArray> supportedFormats = []() {
            auto formats = QImageReader::supportedImageFormats();
            return QSet<QByteArray>(formats.begin(), formats.end());
        }();

        if (supportedFormats.contains(fileInfo.suffix().toLower().toUtf8())) {
            QImageReader reader(fileInfo.absoluteFilePath());
            reader.setAutoTransform(true); // Important for EXIF rotations from smartphones

            if (reader.canRead()) {
                // 1. Read the real dimensions of the image (costs hardly any performance)
                QSize originalSize = reader.size();

                // 2. Compute a proportional size that fits into a 96x96 box
                // e.g. 1920x1080 automatically becomes 96x54 here
                QSize scaledSize = originalSize.scaled(QSize(96, 96), Qt::KeepAspectRatio);

                // 3. Tell the reader the proportional size
                reader.setScaledSize(scaledSize);

                QImage img = reader.read();
                if (!img.isNull()) {
                    pix =  QPixmap::fromImage(img);
                }
            }
        }

        return pix;
    }
