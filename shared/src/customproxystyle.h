#ifndef CUSTOMPROXYSTYLE_H
#define CUSTOMPROXYSTYLE_H

#include <QPainter>
#include <QProxyStyle>
#include <QStyleOption>

class CustomProxyStyle : public QProxyStyle {
public:
    // Konstruktor reicht den aktuellen System-Style durch
    explicit CustomProxyStyle(QStyle *style = nullptr) : QProxyStyle(style) {}

    void drawPrimitive(PrimitiveElement element, const QStyleOption *option,
                       QPainter *painter, const QWidget *widget = nullptr) const override
    {
        // Wir fangen exakt den "ItemView Drop-Indikator" ab
        if (element == QStyle::PE_IndicatorItemViewItemDrop) {
            if (option->rect.isNull()) return;

            painter->save();
            painter->setRenderHint(QPainter::Antialiasing);

            // Definiere deine Wunschfarbe (z.B. ein modernes Windows- oder KDE-Blau)
            QColor dropAccentColor(0, 120, 215); // #0078d7

            // Heuristik: Ist das Rechteck sehr flach, ist es eine Linie ZWISCHEN Items
            if (option->rect.height() <= 2) {
                QPen pen(dropAccentColor, 3); // Schöne, 3 Pixel dicke Linie
                painter->setPen(pen);
                painter->drawLine(option->rect.topLeft(), option->rect.topRight());
            }
            // Andernfalls ist es ein Kasten UM ein Item (z.B. Hover über einem Ordner)
            else {
                QPen pen(dropAccentColor, 2, Qt::SolidLine);
                painter->setPen(pen);

                // Ein leicht abgerundeter Rahmen sieht moderner aus
                QRect targetRect = option->rect.adjusted(1, 1, -1, -1);
                painter->drawRoundedRect(targetRect, 4, 4);

                // Das Innere des Ordners zart blau glühen lassen
                painter->fillRect(targetRect, QColor(0, 120, 215, 35)); // Alpha 35 = sehr dezent
            }

            painter->restore();
            return; // Wichtig: Hier abbrechen, damit das Stylesheet es nicht überschreibt!
        }
        else if (element == QStyle::PE_IndicatorHeaderArrow) {
            auto *headerOption = qstyleoption_cast<const QStyleOptionHeader *>(option);
            if (headerOption) {

                QRect r = headerOption->rect;
                if (r.isEmpty()) return; // Sicherheitsnetz

                painter->save();
                painter->setRenderHint(QPainter::Antialiasing);

                // Farbe für deinen Sortierpfeil
                QColor arrowColor(133, 0, 247);
                painter->setPen(Qt::NoPen);
                painter->setBrush(arrowColor);

                // Feste, harmonische Zielgröße für das Dreieck definieren
                const int arrowWidth = 8;
                const int arrowHeight = 4;

                // Exakte Zentrierung innerhalb des von Qt vorgegebenen Rects
                double x = r.x() + (r.width() - arrowWidth) / 2.0;
                double y = r.y() + (r.height() - arrowHeight) / 2.0;

                QPolygonF triangle;
                if (headerOption->sortIndicator == QStyleOptionHeader::SortDown) {
                    // Pfeil zeigt nach unten (Spitze unten)
                    triangle << QPointF(x, y)
                             << QPointF(x + arrowWidth, y)
                             << QPointF(x + (arrowWidth / 2.0), y + arrowHeight);
                } else if (headerOption->sortIndicator == QStyleOptionHeader::SortUp) {
                    // Pfeil zeigt nach oben (Spitze oben)
                    triangle << QPointF(x, y + arrowHeight)
                             << QPointF(x + arrowWidth, y + arrowHeight)
                             << QPointF(x + (arrowWidth / 2.0), y);
                } else {
                    qDebug() << "headerOption->sortIndicator == QStyleOptionHeader::SortNone";
                }

                painter->drawPolygon(triangle);
                painter->restore();
                return; // Qt das Standard-Zeichnen verbieten
            }
        }

        // Alle anderen Standard-Elemente (Scrollbars, Header, etc.) normal zeichnen
        QProxyStyle::drawPrimitive(element, option, painter, widget);
    }
};

#endif // CUSTOMPROXYSTYLE_H
