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
    //setMinimumWidth(500);

    QFileInfo srcInfo(conflict.sourcePath);
    QFileInfo dstInfo(conflict.targetPath);

    bool isDirConflict = srcInfo.isDir() && dstInfo.isDir();
    QString actionWord = isDirConflict ? tr("Merge") : tr("Replace");
    bool isDestinationNewer = dstInfo.lastModified() > srcInfo.lastModified();

    // Layouts
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(15);

    // Grid für Daten
    auto *gridLayout = new QGridLayout();
    gridLayout->setHorizontalSpacing(16);
    gridLayout->setVerticalSpacing(4);
    gridLayout->setContentsMargins(10, 0, 10, 0);

    // Line 1: Source Header
    QLabel *lblSrcIcon = new QLabel(this);
    lblSrcIcon->setPixmap(generateThumbnail(srcInfo));
    lblSrcIcon->setAlignment(Qt::AlignCenter);
    lblSrcIcon->setContentsMargins(8, 8, 8, 8);

    QLabel *pathSrcLabel = new QLabel(QDir::toNativeSeparators(conflict.sourcePath), this);
    pathSrcLabel->setWordWrap(true);

    // Line 2: Source Data
    auto *lblSrcTitle = new QLabel(QString("<b>%1</b>").arg(tr("Source")), this);
    //lblSrcTitle->setStyleSheet("color: #2e7d32;");
    lblSrcTitle->setAlignment(Qt::AlignCenter);

    auto *lblSrcTime  = new QLabel(srcInfo.lastModified().toString("yyyy-MM-dd  HH:mm:ss"), this);
    lblSrcTime->setAlignment(Qt::AlignCenter);

    auto *lblSrcSizeB = new QLabel(tr("%1 Bytes").arg(m_locale.toString(srcInfo.size())), this);
    lblSrcSizeB->setAlignment(Qt::AlignRight);

    auto *lblSrcType  = new QLabel(getSourceTypeString(srcInfo), this);
    lblSrcType->setAlignment(Qt::AlignLeft);

    // Line 3: Destination Data
    auto *lblDstTitle = new QLabel(QString("<b>%1</b>").arg(tr("Target")), this);
    //lblDstTitle->setStyleSheet("color: #d32f2f;");
    lblDstTitle->setAlignment(Qt::AlignCenter);

    UnderlinedLabel *lblDstTime = new UnderlinedLabel(this);
    lblDstTime->setText(dstInfo.lastModified().toString("yyyy-MM-dd  HH:mm:ss"));
    lblDstTime->setAlignment(Qt::AlignCenter);
    if (isDestinationNewer) {
        lblDstTime->setUnderlineVisible(isDestinationNewer);
    }

    auto *lblDstSizeB = new QLabel(tr("%1 Bytes").arg(m_locale.toString(dstInfo.size())), this);
    lblDstSizeB->setAlignment(Qt::AlignRight);

    auto *lblDstType  = new QLabel(getSourceTypeString(dstInfo), this);
    lblDstType->setAlignment(Qt::AlignLeft);

    // Line 4: Destination Header
    QLabel *lblDstIcon = new QLabel(this);
    lblDstIcon->setPixmap(generateThumbnail(dstInfo));
    lblDstIcon->setAlignment(Qt::AlignCenter);
    lblDstIcon->setContentsMargins(8, 8, 8, 8);

    QLabel *pathDstLabel = new QLabel(QDir::toNativeSeparators(conflict.targetPath), this);
    pathDstLabel->setWordWrap(true);

    // Grid befüllen
    int iCol = 0;
    gridLayout->addWidget(lblSrcIcon,  0, iCol);
    gridLayout->addWidget(lblSrcTitle, 1, iCol);
    gridLayout->addWidget(lblDstTitle, 2, iCol);
    gridLayout->addWidget(lblDstIcon,  3, iCol);
    iCol++;

    int pathStartCol = iCol;

    gridLayout->addWidget(lblSrcTime,   1, iCol);
    gridLayout->addWidget(lblDstTime,   2, iCol);
    iCol++;

    if (srcInfo.size() >= 1024 || dstInfo.size() >= 1024) {
        auto *lblSrcSizeA = new QLabel(formatAdaptiveSize(srcInfo.size()), this);
        lblSrcSizeA->setAlignment(Qt::AlignRight);
        gridLayout->addWidget(lblSrcSizeA, 1, iCol);

        auto *lblDstSizeA = new QLabel(formatAdaptiveSize(dstInfo.size()), this);
        lblDstSizeA->setAlignment(Qt::AlignRight);
        gridLayout->addWidget(lblDstSizeA, 2, iCol);

        iCol++;
    }

    gridLayout->addWidget(lblSrcSizeB, 1, iCol);
    gridLayout->addWidget(lblDstSizeB, 2, iCol);
    iCol++;

    gridLayout->addWidget(lblSrcType,  1, iCol);
    gridLayout->addWidget(lblDstType,  2, iCol);
    iCol++;

    QLabel *warningLabel = new QLabel(this);
    if (isDestinationNewer) {
        warningLabel->setText("<b><font color='#ff4040'>" + tr("&larr; destination is newer!") + "</font></b>");
        gridLayout->addWidget(warningLabel, 2, iCol);
    }

    // 1. Pfad-Labels dynamisch über restlichen Spalten spannen
    int totalPathSpan = iCol - pathStartCol + 1;
    gridLayout->addWidget(pathSrcLabel, 0, pathStartCol, 1, totalPathSpan);
    gridLayout->addWidget(pathDstLabel, 3, pathStartCol, 1, totalPathSpan);

    // 0 = Spalte wächst nicht (bleibt so kompakt wie möglich)
    // 1 = Spalte teilt sich den restlichen Platz des Fensters
    // Alle vorderen Spalten (0 bis iCol-1) bleiben kompakt (0)
    for (int c = 0; c < iCol; ++c) {
        gridLayout->setColumnStretch(c, 0);
    }
    // Die allerletzte Spalte bekommt Stretch 1
    gridLayout->setColumnStretch(iCol, 1);

    mainLayout->addLayout(gridLayout);

    // Trennlinie
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

QString ConflictDialog::getSourceTypeString(const QFileInfo &fileInfo) {
    if (fileInfo.isJunction())     return tr("Junction");
    if (fileInfo.isSymbolicLink()) return tr("SymLink");
    if (fileInfo.isDir())          return tr("Directory");

    return tr("File");
}

QString ConflictDialog::formatAdaptiveSize(quint64 bytes) {
    if (bytes < 1024) {
        return m_locale.toString(bytes) + tr(" Bytes");
    }

    double size = static_cast<double>(bytes);
    static const QStringList units = {"Bytes", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB", "ZiB", "YiB"};
    int unitIndex = 0;

    while (size >= 1024.0 && unitIndex < units.size() - 1) {
        size /= 1024.0;
        unitIndex++;
    }

    int precision = 0;
    if (size < 10.0) {
        precision = 2; // z.B. 1,23 MiB
    } else if (size < 100.0) {
        precision = 1; // z.B. 12,3 MiB
    } else {
        precision = 0; // z.B. 123 MiB
    }

    return m_locale.toString(size, 'f', precision) + " " + units[unitIndex];
}

QPixmap ConflictDialog::generateThumbnail(const QFileInfo &fileInfo) {
    QPixmap pix = m_iconProvider.icon(fileInfo).pixmap(48, 48);

    QString ext = fileInfo.suffix().toLower();
    if (ext == "cur" || ext == "ico" || ext == "icns") {
        return pix;
    }

    // 2. Unterstützte Formate statisch cachen (wird nur einmalig beim allerersten Aufruf erstellt)
    static const QSet<QByteArray> supportedFormats = []() {
        auto formats = QImageReader::supportedImageFormats();
        return QSet<QByteArray>(formats.begin(), formats.end());
    }();

    if (supportedFormats.contains(fileInfo.suffix().toLower().toUtf8())) {
        QImageReader reader(fileInfo.absoluteFilePath());
        reader.setAutoTransform(true); // Wichtig für EXIF-Rotationen von Smartphones

        if (reader.canRead()) {
            // 1. Die echte Dimension des Bildes auslesen (kostet kaum Performance)
            QSize originalSize = reader.size();

            // 2. Proportionale Größe berechnen, die in eine 96x96 Box passt
            // Aus z.B. 1920x1080 wird hier automatisch 96x54
            QSize scaledSize = originalSize.scaled(QSize(96, 96), Qt::KeepAspectRatio);

            // 3. Dem Reader die proportionale Größe mitteilen
            reader.setScaledSize(scaledSize);

            QImage img = reader.read();
            if (!img.isNull()) {
                pix =  QPixmap::fromImage(img);
            }
        }
    }

    return pix;
}
