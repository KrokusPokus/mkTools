#ifndef CONFLICTDIALOG_H
#define CONFLICTDIALOG_H

#include "fileoperation.h"

#include <QDialog>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QLocale>

struct ConflictResult {
    ConflictResolution resolution;
    bool applyToAll;
};

class ConflictDialog : public QDialog {
    Q_OBJECT

public:
    ConflictDialog(const Conflict &conflict, QWidget *parent = nullptr);
    ConflictResult result() const { return m_result; }

private:
    QString formatAdaptiveSize(quint64 bytes);
    QString getSourceTypeString(const QFileInfo &fileInfo);
    QPixmap generateThumbnail(const QFileInfo &fileInfo);

    ConflictResult m_result{ConflictResolution::Cancel, false};
    QLocale m_locale;
    QFileIconProvider m_iconProvider;
};

#endif // CONFLICTDIALOG_H
