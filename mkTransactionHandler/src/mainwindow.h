#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "fileoperation.h"

#include <QFontMetrics>
#include <QLabel>
#include <QLabel>
#include <QMainWindow>
#include <QProgressBar>
#include <QPushButton>
#include <QResizeEvent>
#include <QThread>
#include <QUrl>

#ifdef Q_OS_WIN
#include <shobjidl.h> // Enthält ITaskbarList3
#endif

class CleanLabel : public QLabel {
    Q_OBJECT
public:
    using QLabel::QLabel;

    void setText(const QString &text) {
        triggerParentUpdate();

        QLabel::setText(text);

        triggerParentUpdate();
    }

private:
    void triggerParentUpdate() {
        if (parentWidget()) {
            parentWidget()->update(geometry().adjusted(-2, 0, 2, 0));
        }
    }
};


class ElidedLabel : public QLabel {
    Q_OBJECT
public:
    explicit ElidedLabel(QWidget *parent = nullptr) : QLabel(parent) {
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    }

    // Wir überschreiben setText, um den originalen, ungekürzten Text im Speicher zu behalten
    void setText(const QString &text) {
        triggerParentUpdate();

        m_fullText = text;
        updateElidedText();

        triggerParentUpdate();
    }

    QString fullText() const { return m_fullText; }

protected:
    void resizeEvent(QResizeEvent *event) override {
        triggerParentUpdate();
        QLabel::resizeEvent(event);
        updateElidedText();
    }

private:
    void updateElidedText() {
        QFontMetrics fm = fontMetrics();
        int availableWidth = contentsRect().width();
        if (availableWidth <= 0) availableWidth = width();

        QString elided = fm.elidedText(m_fullText, Qt::ElideMiddle, availableWidth);
        QLabel::setText(elided);
    }

    void triggerParentUpdate() {
        if (parentWidget()) {
            parentWidget()->update(geometry().adjusted(-2, 0, 2, 0));
        }
    }
    QString m_fullText;
};

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(OperationType opType, QList<QUrl> urls, QString targetDir, QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onThreadStarted();
    void onProgressUpdated(const CopyStats &stats);
    void onConflictDetected(const Conflict &conflict);

    void onOperationPaused();
    void onOperationContinued();
    void onOperationFinished(int errorCount);
    void onOperationCanceled(int errorCount);

    void onPauseRequested();
    void onContinueRequested();
    void onRetryRequested();
    void onCancelRequested();

private:
    void setupUi();
    QString formatAdaptiveSize(quint64 bytes);

    QLocale m_locale;
    bool m_isFinished;

    // Daten
    QList<QUrl> m_urls;
    QString m_targetDir;
    OperationType m_operationType;

    // UI-Elemente
    CleanLabel *m_headerLabel = nullptr;
    ElidedLabel *m_nameLabel = nullptr;
    ElidedLabel *m_sourceLabel = nullptr;
    ElidedLabel *m_targetLabel = nullptr;
    QProgressBar *m_progressBar = nullptr;
    CleanLabel *m_timeLabel = nullptr;
    CleanLabel *m_itemsLeftLabel = nullptr;
    CleanLabel *m_speedLabel = nullptr;
    CleanLabel *m_driveModeLabel = nullptr;
    QPushButton *m_pauseButton = nullptr;
    QPushButton *m_cancelButton = nullptr;

    // Threading
    QThread *m_workerThread;
    FileOperation *m_fileOp;

#ifdef Q_OS_WIN
    ITaskbarList3* m_taskbarList = nullptr;
#endif
};

#endif // MAINWINDOW_H
