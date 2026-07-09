#include "mainwindow.h"
#include "conflictdialog.h"
#include "fileoperation.h"

#include <QApplication>
#include <QBuffer>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QTimer>
#include <QVBoxLayout>

MainWindow::MainWindow(OperationType opType, QList<QUrl> urls, QString targetDir, QWidget *parent)
    : QMainWindow(parent)
    , m_urls(std::move(urls))
    , m_targetDir(std::move(targetDir))
    , m_operationType(opType)
    , m_workerThread(nullptr)
    , m_fileOp(nullptr)
{
#ifdef Q_OS_WIN
    // COM-Schnittstelle für die Windows-Taskleiste instanziieren
    CoCreateInstance(CLSID_TaskbarList, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&m_taskbarList));
#endif
    m_isFinished = false;

    // 1. UI aufbauen
    setupUi();

    // 2. Worker und Thread initialisieren
    m_workerThread = new QThread(this);
    m_fileOp = new FileOperation(m_operationType, m_urls, m_targetDir);
    m_fileOp->moveToThread(m_workerThread);

    connect(m_workerThread, &QThread::started,                m_fileOp,  &FileOperation::run);

    // 3. Signal-Slot-Verbindungen (Wichtig: Conflict via QueuedConnection!)
    connect(m_workerThread, &QThread::started,                this,     &MainWindow::onThreadStarted);
    connect(m_fileOp,       &FileOperation::progress,         this,     &MainWindow::onProgressUpdated);
    connect(m_fileOp,       &FileOperation::conflictDetected, this,     &MainWindow::onConflictDetected, Qt::QueuedConnection);
    connect(m_fileOp,       &FileOperation::finished,         this,     &MainWindow::onOperationFinished);

    // Clean-up wenn der Thread fertig ist
    connect(m_fileOp,       &FileOperation::finished,       m_workerThread, &QThread::quit);
    connect(m_fileOp,       &FileOperation::finished,       m_fileOp,       &QObject::deleteLater);

    // Starten!
    m_workerThread->start();

    QTimer::singleShot(2000, this, [this]() {
        if (!m_isFinished) {
            show();
        }
    });
}

MainWindow::~MainWindow() {
    if (m_workerThread && m_workerThread->isRunning()) {
        m_workerThread->quit();
        m_workerThread->wait();
    }
#ifdef Q_OS_WIN
    if (m_taskbarList) {
        m_taskbarList->Release();
    }
#endif
}

void MainWindow::setupUi() {
    QString windowTitle;

    if (m_operationType == OperationType::Copy) {
        windowTitle =  tr("Copying...");
    } else if (m_operationType == OperationType::Move) {
        windowTitle =  tr("Moving...");
    } else if (m_operationType == OperationType::Link) {
        windowTitle =  tr("Linking...");
    } else if (m_operationType == OperationType::Delete) {
        windowTitle =  tr("Deleting...");
    } else if (m_operationType == OperationType::Recycle) {
        windowTitle =  tr("Recycling...");
    } else {
        windowTitle =  tr("Unknown operation type...");
    }

    setWindowTitle(windowTitle);
    setWindowIcon(QIcon(":/icons/app.ico"));
    setMinimumWidth(450);

    auto *centralWidget = new QWidget(this);
    auto *mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setContentsMargins(13, 7, 13, 7);
    mainLayout->setSpacing(0); // Präzise Kontrolle über Absätze via addSpacing

    // 1. ZEILE: "Copying X items. (Y Bytes)"
    m_headerLabel = new CleanLabel(this);
    m_headerLabel->setContentsMargins(1, 0, 1, 0);
    mainLayout->addWidget(m_headerLabel);

    mainLayout->addSpacing(8); // Erster Absatz

    // 2. BLOCK: Datei-Details (Pfade)
    auto *pathLayout = new QFormLayout();
    pathLayout->setLabelAlignment(Qt::AlignLeft);
    pathLayout->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    pathLayout->setHorizontalSpacing(10);
    pathLayout->setVerticalSpacing(2);

    // Doing it this we so we can setContentsMargins. Otherwise, the text would get cut off a little...
    CleanLabel *nameLabelName = new CleanLabel(this);
    nameLabelName->setContentsMargins(1, 0, 1, 0);
    nameLabelName->setText(tr("Name:"));
    m_nameLabel = new ElidedLabel(this);
    m_nameLabel->setWordWrap(false);
    m_nameLabel->setContentsMargins(1, 0, 1, 0);
    pathLayout->addRow(nameLabelName, m_nameLabel);

    CleanLabel *sourceLabelName = new CleanLabel(this);
    sourceLabelName->setContentsMargins(1, 0, 1, 0);
    sourceLabelName->setText(tr("Source:"));
    m_sourceLabel = new ElidedLabel(this);
    m_sourceLabel->setWordWrap(false);
    m_sourceLabel->setContentsMargins(1, 0, 1, 0);
    pathLayout->addRow(sourceLabelName, m_sourceLabel);

    if (m_operationType != OperationType::Delete && m_operationType != OperationType::Recycle) {
        CleanLabel *targetLabelName = new CleanLabel(this);
        targetLabelName->setContentsMargins(1, 0, 1, 0);
        targetLabelName->setText(tr("Target:"));
        m_targetLabel = new ElidedLabel(this);
        m_targetLabel->setWordWrap(false);
        m_targetLabel->setContentsMargins(1, 0, 1, 0);
        pathLayout->addRow(targetLabelName, m_targetLabel);
    }

    mainLayout->addLayout(pathLayout);
    mainLayout->addSpacing(8);

    // 3. BLOCK: Metriken & Performance
    auto *metricsLayout = new QFormLayout();
    metricsLayout->setLabelAlignment(Qt::AlignLeft);
    metricsLayout->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    metricsLayout->setHorizontalSpacing(31);
    metricsLayout->setVerticalSpacing(2);

    CleanLabel *timeLabelName = new CleanLabel(this);   // Doing it this we so we can setContentsMargins. Otherwise, the text would get cut off a little...
    timeLabelName->setContentsMargins(1, 0, 1, 0);
    timeLabelName->setText(tr("Time:"));
    m_timeLabel = new CleanLabel(this);
    m_timeLabel->setContentsMargins(1, 0, 1, 0);
    metricsLayout->addRow(timeLabelName, m_timeLabel);

    CleanLabel *itemsLeftLabelName = new CleanLabel(this);
    itemsLeftLabelName->setContentsMargins(1, 0, 1, 0);
    itemsLeftLabelName->setText(tr("Items:"));
    m_itemsLeftLabel = new CleanLabel(this);
    m_itemsLeftLabel->setContentsMargins(1, 0, 1, 0);
    metricsLayout->addRow(itemsLeftLabelName, m_itemsLeftLabel);

    CleanLabel *speedLabelName = new CleanLabel(this);
    speedLabelName->setContentsMargins(1, 0, 1, 0);
    speedLabelName->setText(tr("Speed:"));
    m_speedLabel = new CleanLabel(this);
    m_speedLabel->setContentsMargins(1, 0, 1, 0);
    metricsLayout->addRow(speedLabelName, m_speedLabel);

    if (m_operationType != OperationType::Delete && m_operationType != OperationType::Recycle) {
        CleanLabel *modeLabelName = new CleanLabel(this);
        modeLabelName->setContentsMargins(1, 0, 1, 0);
        modeLabelName->setText(tr("Mode:"));
        m_driveModeLabel = new CleanLabel(this);
        m_driveModeLabel->setContentsMargins(1, 0, 1, 0);
        metricsLayout->addRow(modeLabelName, m_driveModeLabel);
    }

    auto *bottomLayout = new QHBoxLayout();
    bottomLayout->addLayout(metricsLayout);

    auto *buttonLayout = new QVBoxLayout();
    buttonLayout->addStretch(); // Schiebt den Button nach unten
    m_cancelButton = new QPushButton(tr("Cancel"), this);
    m_cancelButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    buttonLayout->addWidget(m_cancelButton);
    bottomLayout->addLayout(buttonLayout);

    mainLayout->addLayout(bottomLayout);

    connect(m_cancelButton, &QPushButton::clicked, this, &MainWindow::onCancelRequested);

    setCentralWidget(centralWidget);
}

void MainWindow::onThreadStarted() {
    qDebug() << "[mkTransactionHandler] MainWindow::onThreadStarted()";
}

void MainWindow::onProgressUpdated(const CopyStats &stats) {
    // 1. Summary
    if (m_operationType == OperationType::Copy) {
        m_headerLabel->setText(tr("Copying %n item. (%1)", nullptr, stats.totalFiles).arg(formatAdaptiveSize(stats.totalBytes)));
    } else if (m_operationType == OperationType::Move) {
        m_headerLabel->setText(tr("Moving %n item. (%1)", nullptr, stats.totalFiles).arg(formatAdaptiveSize(stats.totalBytes)));
    } else if (m_operationType == OperationType::Link) {
        m_headerLabel->setText(tr("Linking %n item. (%1)", nullptr, stats.totalFiles).arg(formatAdaptiveSize(stats.totalBytes)));
    } else if (m_operationType == OperationType::Delete) {
        m_headerLabel->setText(tr("Deleting %n item. (%1)", nullptr, stats.totalFiles).arg(formatAdaptiveSize(stats.totalBytes)));
    } else if (m_operationType == OperationType::Recycle) {
        m_headerLabel->setText(tr("Recycling %n item. (%1)", nullptr, stats.totalFiles).arg(formatAdaptiveSize(stats.totalBytes)));
    }

    // 2. Pfade setzen
    m_nameLabel->setText(stats.currentName);
    m_sourceLabel->setText(stats.currentSourceDir);
    if (m_operationType != OperationType::Delete && m_operationType != OperationType::Recycle) {
        m_targetLabel->setText(stats.currentTargetDir);
    }

    // 3. Remaining Items und Bytes
    int processedFiles = stats.filesWritten + stats.filesSkipped + stats.filesError;
    int filesLeft = qMax(0, stats.totalFiles - processedFiles);
    qint64 bytesLeft = qMax(0ll, stats.totalBytes - stats.bytesWritten);

    m_itemsLeftLabel->setText(tr("%1 (%2)").arg(m_locale.toString(filesLeft), formatAdaptiveSize(bytesLeft)));

    double secondsElapsed = stats.elapsedMs / 1000.0;

    if (secondsElapsed > 0.1 && stats.bytesWritten > 0) {
        // Geschwindigkeit in Bytes pro Sekunde
        double bytesPerSecond = stats.bytesWritten / secondsElapsed;
        m_speedLabel->setText(tr("%1/s").arg(m_locale.formattedDataSize(bytesPerSecond)));

        // Geschätzte Restzeit (Time Remaining)
        double secondsLeft = bytesLeft / bytesPerSecond;

        // Schön formatieren (hh:mm:ss)
        int h = int(secondsLeft) / 3600;
        int m = (int(secondsLeft) % 3600) / 60;
        int s = int(secondsLeft) % 60;

        if (h > 0) {
            m_timeLabel->setText(tr("%1h %2m %3s").arg(h).arg(m).arg(s));
        } else if (m > 0) {
            m_timeLabel->setText(tr("%1m %2s").arg(m).arg(s));
        } else {
            m_timeLabel->setText(tr("%1s").arg(s));
        }
    } else {
        m_speedLabel->setText(tr("Calculating..."));
        m_timeLabel->setText(tr("Calculating..."));
    }

    // 4. Drive-Mode
    if (m_operationType != OperationType::Delete && m_operationType != OperationType::Recycle) {
        m_driveModeLabel->setText(stats.isSameDevice ? tr("Same Drive") : tr("Different Drives"));
    }

    // 5. Window Title
    double doneFileRate = double(stats.filesWritten + stats.filesSkipped + stats.filesError) / (stats.totalFiles + 0.01);
    double doneTransRate = double(stats.bytesWritten) / (stats.totalBytes + 0.01);
    double aveSize = double(stats.totalBytes) / (stats.totalFiles + 0.01);

    double coef = SizeToCoef(aveSize);
    double doneRate = (doneFileRate * coef + doneTransRate) / (coef + 1.0);

    int progressPercent = qBound(0, int(doneRate * 100), 100);

    if (m_operationType == OperationType::Copy) {
        setWindowTitle(tr("Copying... (%1%)").arg(progressPercent));
    } else if (m_operationType == OperationType::Move) {
        setWindowTitle(tr("Moving... (%1%)").arg(progressPercent));
    } else if (m_operationType == OperationType::Link) {
        setWindowTitle(tr("Linking... (%1%)").arg(progressPercent));
    } else if (m_operationType == OperationType::Delete) {
        setWindowTitle(tr("Deleting... (%1%)").arg(progressPercent));
    } else if (m_operationType == OperationType::Recycle) {
        setWindowTitle(tr("Recycling... (%1%)").arg(progressPercent));
    }
#ifdef Q_OS_WIN
    if (m_taskbarList) {
        // Native Windows-ID (HWND) des Fensters holen
        HWND hwnd = reinterpret_cast<HWND>(this->winId());

        if (stats.totalBytes > 0) {
            // Modus auf "Normal" (Grün) setzen und Werte übergeben
            m_taskbarList->SetProgressState(hwnd, TBPF_NORMAL);
            m_taskbarList->SetProgressValue(hwnd, stats.bytesWritten, stats.totalBytes);
        } else {
            // Wenn totalBytes noch 0 ist (z.B. beim Scannen), "Dauer-Animation" zeigen
            m_taskbarList->SetProgressState(hwnd, TBPF_INDETERMINATE);
        }
    }
#endif
}

void MainWindow::onConflictDetected(const Conflict &conflict) {
    // Falls die 2 Sekunden noch nicht rum sind, das Fenster aber
    // wegen eines Konflikts jetzt gebraucht wird: Sofort einblenden!
    if (!isVisible()) {
        show();
        QApplication::processEvents();
    }

    ConflictDialog dialog(conflict, this);
    dialog.exec();

    ConflictResult res = dialog.result();

    if (m_fileOp) {
        m_fileOp->resolveConflict(res.resolution, res.applyToAll);
    }
}

void MainWindow::onOperationFinished(int errorCount) {
    m_isFinished = true;

#ifdef Q_OS_WIN
    if (m_taskbarList) {
        HWND hwnd = reinterpret_cast<HWND>(this->winId());

        if (errorCount > 0) {
            m_taskbarList->SetProgressState(hwnd, TBPF_ERROR);      // Bei Fehlern: Balken rot einfärben
        } else {
            m_taskbarList->SetProgressState(hwnd, TBPF_NOPROGRESS); // Bei Erfolg: Fortschrittsanzeige ausblenden
        }
    }
#endif

    if (errorCount > 0) {
        if (!isVisible()) {
            show();
        }
        m_headerLabel->setText(tr("Finished with %n error.", nullptr, errorCount));
        m_cancelButton->setText(tr("Close"));
        disconnect(m_cancelButton, &QPushButton::clicked, this, &MainWindow::onCancelRequested);
        connect(m_cancelButton, &QPushButton::clicked, this, &QWidget::close);
    } else {
        if (isVisible()) {
            close();        // Das Fenster war schon offen (Aktion dauerte länger als 2 Sek) -> normal schließen
        } else {
            qApp->quit();   // Das Fenster war noch unsichtbar -> beende gesamte Qt-Applikation
        }
    }
}

void MainWindow::onCancelRequested() {
    m_headerLabel->setText(tr("Cancelling..."));
    m_cancelButton->setEnabled(false);

    // Rufe die zentrale Abbruch-Logik des Workers auf
    if (m_fileOp) {
        m_fileOp->cancel();
    }
}

QString MainWindow::formatAdaptiveSize(quint64 bytes) {
    if (bytes < 1024) {
        return m_locale.toString(bytes) + " Bytes";
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

double MainWindow::SizeToCoef(double aveSize) {
    if (aveSize < 4096.0) return 2.0;
    double ret = 65536.0 / aveSize; // 64 KiB
    if (ret >= 2.0) return 2.0;
    if (ret <= 0.1) return 0.1;
    return ret;
}
