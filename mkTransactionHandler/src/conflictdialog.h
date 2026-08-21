#ifndef CONFLICTDIALOG_H
#define CONFLICTDIALOG_H

#include "fileoperation.h"

#include <QDialog>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QLabel>
#include <QLocale>
#include <QPainter>

struct ConflictResult {
    ConflictResolution resolution;
    bool applyToAll;
};


class UnderlinedLabel : public QLabel {
    Q_OBJECT

public:
    explicit UnderlinedLabel(QWidget *parent = nullptr)
        : QLabel(parent) {}

    explicit UnderlinedLabel(const QString &text, QWidget *parent = nullptr)
        : QLabel(text, parent) {}

    // Farbe der Unterstreichung festlegen
    void setUnderlineColor(const QColor &color) {
        if (m_underlineColor != color) {
            m_underlineColor = color;
            update();
        }
    }

    QColor underlineColor() const { return m_underlineColor; }

    // Steuerung, ob die Linie gezeichnet wird oder nicht
    void setUnderlineVisible(bool visible) {
        if (m_underlineVisible != visible) {
            m_underlineVisible = visible;
            update();
        }
    }

    bool isUnderlineVisible() const { return m_underlineVisible; }

protected:
    void paintEvent(QPaintEvent *event) override {
        // Standard-Textzeichnen durch QLabel (nutzt Theme-Farben)
        QLabel::paintEvent(event);

        // Linie nur zeichnen, wenn sichtbar geschaltet, Text vorhanden & Farbe gültig
        if (m_underlineVisible && !text().isEmpty() && m_underlineColor.isValid() && m_underlineColor.alpha() > 0) {
            QPainter painter(this);
            painter.setPen(QPen(m_underlineColor, 1));

            int textWidth = fontMetrics().horizontalAdvance(text());
            int y = height() - 1;

            int x = 0;
            if (alignment() & Qt::AlignHCenter) {
                x = (width() - textWidth) / 2;
            } else if (alignment() & Qt::AlignRight) {
                x = width() - textWidth;
            }

            painter.drawLine(x, y, x + textWidth, y);
        }
    }

private:
    QColor m_underlineColor = QColor("#ff4040");
    bool m_underlineVisible = true; // Standardmäßig aktiviert
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
