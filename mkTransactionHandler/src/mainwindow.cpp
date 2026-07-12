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
    connect(m_fileOp,       &FileOperation::wasContinued,     this,     &MainWindow::onOperationContinued);
    connect(m_fileOp,       &FileOperation::wasPaused,        this,     &MainWindow::onOperationPaused);
    //connect(m_fileOp,     &FileOperation::wasRetried,       this,     &MainWindow::onOperationRetried);
    connect(m_fileOp,       &FileOperation::wasFinished,      this,     &MainWindow::onOperationFinished);
    connect(m_fileOp,       &FileOperation::wasCanceled,      this,     &MainWindow::onOperationCanceled);

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

    delete m_fileOp;

#ifdef Q_OS_WIN
    if (m_taskbarList) {
        m_taskbarList->Release();
    }
#endif
}

void MainWindow::setupUi() {
    QString windowTitle;

    if (m_operationType == OperationType::Copy) {
        windowTitle =  tr("Copying");
    } else if (m_operationType == OperationType::Move) {
        windowTitle =  tr("Moving");
    } else if (m_operationType == OperationType::Link) {
        windowTitle =  tr("Linking");
    } else if (m_operationType == OperationType::Delete) {
        windowTitle =  tr("Deleting");
    } else if (m_operationType == OperationType::Recycle) {
        windowTitle =  tr("Recycling");
    } else {
        windowTitle =  tr("[unknown operation type]");
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

    // ==========================================
    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setTextVisible(false);
    m_progressBar->setFixedHeight(10);

    mainLayout->addWidget(m_progressBar);
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

    m_pauseButton = new QPushButton(tr("Pause"), this);
    m_pauseButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    buttonLayout->addWidget(m_pauseButton);

    m_cancelButton = new QPushButton(tr("Cancel"), this);
    m_cancelButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    buttonLayout->addWidget(m_cancelButton);

    bottomLayout->addLayout(buttonLayout);

    mainLayout->addLayout(bottomLayout);

    connect(m_pauseButton,  &QPushButton::clicked, this, &MainWindow::onPauseRequested);
    connect(m_cancelButton, &QPushButton::clicked, this, &MainWindow::onCancelRequested);

    setCentralWidget(centralWidget);
}

void MainWindow::onThreadStarted() {
    qDebug() << "[mkTransactionHandler] MainWindow::onThreadStarted()";
}

void MainWindow::onProgressUpdated(const CopyStats &stats) {
    // 1. Summary
    switch (m_operationType) {
        case OperationType::Copy:
            m_headerLabel->setText(tr("Copying %n file. (%1)", nullptr, stats.totalFiles).arg(formatAdaptiveSize(stats.totalBytes)));
            break;

        case OperationType::Move:
            m_headerLabel->setText(tr("Moving %n file. (%1)", nullptr, stats.totalFiles).arg(formatAdaptiveSize(stats.totalBytes)));
            break;

        case OperationType::Link:
            m_headerLabel->setText(tr("Linking %n file. (%1)", nullptr, stats.totalFiles).arg(formatAdaptiveSize(stats.totalBytes)));
            break;

        case OperationType::Delete:
            m_headerLabel->setText(tr("Deleting %n file. (%1)", nullptr, stats.totalFiles).arg(formatAdaptiveSize(stats.totalBytes)));
            break;

        case OperationType::Recycle:
            m_headerLabel->setText(tr("Recycling %n file. (%1)", nullptr, stats.totalFiles).arg(formatAdaptiveSize(stats.totalBytes)));
            break;

        default:
            m_headerLabel->setText("[unknown action]");
            break;
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

    int byteProgressPercent = 0;
    if (stats.totalBytes > 0) {
        double doneRate = static_cast<double>(stats.bytesWritten) / stats.totalBytes;
        byteProgressPercent = qBound(0, int(doneRate * 100), 100);
    } else {
        byteProgressPercent = 0;
    }

    // ==========================================

    if (stats.totalBytes > 0) {
        m_progressBar->setRange(0, 100);    // Zurück zum normalen Modus, falls vorher gescannt wurde
        m_progressBar->setValue(byteProgressPercent);
    } else {
        m_progressBar->setRange(0, 0);      // Während des Einlesens/Scannens (Dauer-Animation wie bei Windows)
    }

    // ==========================================
    // 5. Window Title

    switch (m_operationType) {
        case OperationType::Copy:
            setWindowTitle(tr("Copying %n file (%1) to '%2' at %3%", nullptr, stats.totalFiles).arg(formatAdaptiveSize(stats.totalBytes)).arg(stats.currentTargetDir).arg(byteProgressPercent));
            break;

        case OperationType::Move:
            setWindowTitle(tr("Moving %n file (%1) to '%2' at %3%", nullptr, stats.totalFiles).arg(formatAdaptiveSize(stats.totalBytes)).arg(stats.currentTargetDir).arg(byteProgressPercent));
            break;

        case OperationType::Link:
            setWindowTitle(tr("Linking %n file (%1) to '%2' at %3%", nullptr, stats.totalFiles).arg(formatAdaptiveSize(stats.totalBytes)).arg(stats.currentTargetDir).arg(byteProgressPercent));
            break;

        case OperationType::Delete:
            setWindowTitle(tr("Deleting %n file (%1) at %2%", nullptr, stats.totalFiles).arg(formatAdaptiveSize(stats.totalBytes)).arg(byteProgressPercent));
            break;

        case OperationType::Recycle:
            setWindowTitle(tr("Recycling %n file (%1) at %2%", nullptr, stats.totalFiles).arg(formatAdaptiveSize(stats.totalBytes)).arg(byteProgressPercent));
            break;

        default:
            setWindowTitle(tr("[unknown operation type]"));
            break;
    }


#ifdef Q_OS_WIN
    if (m_taskbarList) {
        // Native Windows-ID (HWND) des Fensters holen
        HWND hwnd = reinterpret_cast<HWND>(this->winId());

        if (stats.totalBytes > 0) {
            // Modus auf "Normal" (Grün) setzen und Werte übergeben
            m_taskbarList->SetProgressState(hwnd, TBPF_NORMAL);
            m_taskbarList->SetProgressValue(hwnd, byteProgressPercent, 100);
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
    }

    ConflictDialog dialog(conflict, this);
    dialog.exec();

    ConflictResult res = dialog.result();

    if (m_fileOp) {
        m_fileOp->resolveConflict(res.resolution, res.applyToAll);
    }
}

void MainWindow::onOperationContinued() {
    if (!isVisible()) show();

    m_pauseButton->setText(tr("Pause"));
    disconnect(m_pauseButton, &QPushButton::clicked, this, nullptr);
    connect(m_pauseButton, &QPushButton::clicked, this, &MainWindow::onPauseRequested);
    m_pauseButton->setEnabled(true);
}

void MainWindow::onOperationPaused() {
    if (!isVisible()) show();

    m_headerLabel->setText(tr("Paused"));

    m_pauseButton->setText(tr("Continue"));
    disconnect(m_pauseButton, &QPushButton::clicked, this, nullptr);
    connect(m_pauseButton, &QPushButton::clicked, this, &MainWindow::onContinueRequested);
    m_pauseButton->setEnabled(true);
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
        if (!isVisible()) show();

        m_headerLabel->setText(tr("Finished with %n error.", nullptr, errorCount));

        m_pauseButton->setText(tr("Retry"));
        disconnect(m_pauseButton, &QPushButton::clicked, this, nullptr);
        connect(m_pauseButton, &QPushButton::clicked, this, &MainWindow::onRetryRequested);
        m_pauseButton->setEnabled(true);

        m_cancelButton->setText(tr("Close"));
        disconnect(m_cancelButton, &QPushButton::clicked, this, nullptr);
        connect(m_cancelButton, &QPushButton::clicked, this, &QWidget::close);
        m_cancelButton->setEnabled(true);

    } else {
        if (isVisible()) {
            // Das Fenster war schon offen (Aktion dauerte länger als 2 Sek) -> normal schließen
            close();
        } else {
            // Das Fenster war noch unsichtbar -> beende gesamte Qt-Applikation
            connect(m_workerThread, &QThread::finished, qApp, &QCoreApplication::quit);
            m_workerThread->quit();
        }
    }
}

void MainWindow::onOperationCanceled(int errorCount) {
    m_isFinished = true;

#ifdef Q_OS_WIN
    if (m_taskbarList) {
        HWND hwnd = reinterpret_cast<HWND>(this->winId());
        m_taskbarList->SetProgressState(hwnd, TBPF_NOPROGRESS);
    }
#endif

    if (errorCount > 0) {
        if (!isVisible()) show();

        m_headerLabel->setText(tr("Canceled. %n error occurred before.", nullptr, errorCount));

        // Pause-Button deaktivieren/ausblenden, da Abbruch final ist
        m_pauseButton->setEnabled(false);
        m_pauseButton->setText(tr("Pause"));

        m_cancelButton->setText(tr("Close"));
        disconnect(m_cancelButton, &QPushButton::clicked, this, nullptr);
        connect(m_cancelButton, &QPushButton::clicked, this, &QWidget::close);
        m_cancelButton->setEnabled(true);

    } else {
        if (isVisible()) {
            // Das Fenster war schon offen (Aktion dauerte länger als 2 Sek) -> normal schließen
            close();
        } else {
            // Das Fenster war noch unsichtbar -> beende gesamte Qt-Applikation
            connect(m_workerThread, &QThread::finished, qApp, &QCoreApplication::quit);
            m_workerThread->quit();
        }
    }
}

void MainWindow::onContinueRequested() {
    m_pauseButton->setEnabled(false);

    if (m_fileOp) {
        m_fileOp->doContinue();
    }
}

void MainWindow::onPauseRequested() {
    m_pauseButton->setEnabled(false);

    if (m_fileOp) {
        m_fileOp->doPause();
    }
}

void MainWindow::onRetryRequested() {
    m_pauseButton->setEnabled(false);

    m_cancelButton->setText(tr("Cancel"));
    disconnect(m_cancelButton, &QPushButton::clicked, this, nullptr);
    connect(m_cancelButton, &QPushButton::clicked, this, &MainWindow::onCancelRequested);
    m_cancelButton->setEnabled(true);

    if (m_fileOp) {
        m_fileOp->doRetry();
    }
}

void MainWindow::onCancelRequested() {
    m_headerLabel->setText(tr("Cancelling..."));
    m_cancelButton->setEnabled(false);

    if (m_fileOp) {
        m_fileOp->doCancel();
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
