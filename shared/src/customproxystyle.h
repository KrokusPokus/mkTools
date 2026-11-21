#ifndef CUSTOMPROXYSTYLE_H
#define CUSTOMPROXYSTYLE_H

#include <QPainter>
#include <QProxyStyle>
#include <QStyleOption>

class CustomProxyStyle : public QProxyStyle {
public:
    // The constructor passes the current system style through
    explicit CustomProxyStyle(QStyle *style = nullptr) : QProxyStyle(style) {}

    void drawPrimitive(PrimitiveElement element, const QStyleOption *option,
                       QPainter *painter, const QWidget *widget = nullptr) const override
    {
        // We intercept exactly the "ItemView drop indicator"
        if (element == QStyle::PE_IndicatorItemViewItemDrop) {
            if (option->rect.isNull()) return;

            painter->save();
            painter->setRenderHint(QPainter::Antialiasing);

            // Define your preferred color (e.g. a modern Windows or KDE blue)
            QColor dropAccentColor(0, 120, 215); // #0078d7

            // Heuristic: if the rectangle is very flat, it is a line BETWEEN items
            if (option->rect.height() <= 2) {
                QPen pen(dropAccentColor, 3); // Nice line, 3 pixels thick
                painter->setPen(pen);
                painter->drawLine(option->rect.topLeft(), option->rect.topRight());
            }
            // Otherwise it is a box AROUND an item (e.g. hovering over a folder)
            else {
                QPen pen(dropAccentColor, 2, Qt::SolidLine);
                painter->setPen(pen);

                // A slightly rounded frame looks more modern
                QRect targetRect = option->rect.adjusted(1, 1, -1, -1);
                painter->drawRoundedRect(targetRect, 4, 4);

                // Let the inside of the folder glow in a soft blue
                painter->fillRect(targetRect, QColor(0, 120, 215, 35)); // Alpha 35 = very subtle
            }

            painter->restore();
            return; // Important: stop here so that the stylesheet does not overwrite it!
        }
        else if (element == QStyle::PE_IndicatorHeaderArrow) {
            auto *headerOption = qstyleoption_cast<const QStyleOptionHeader *>(option);
            if (headerOption) {

                QRect r = headerOption->rect;
                if (r.isEmpty()) return; // Safety net

                painter->save();
                painter->setRenderHint(QPainter::Antialiasing);

                // Color for your sort arrow
                QColor arrowColor(133, 0, 247);
                painter->setPen(Qt::NoPen);
                painter->setBrush(arrowColor);

                // Define a fixed, harmonious target size for the triangle
                const int arrowWidth = 8;
                const int arrowHeight = 4;

                // Exact centering within the rect given by Qt
                double x = r.x() + (r.width() - arrowWidth) / 2.0;
                double y = r.y() + (r.height() - arrowHeight) / 2.0;

                QPolygonF triangle;
                if (headerOption->sortIndicator == QStyleOptionHeader::SortDown) {
                    // Arrow points down (tip at the bottom)
                    triangle << QPointF(x, y)
                             << QPointF(x + arrowWidth, y)
                             << QPointF(x + (arrowWidth / 2.0), y + arrowHeight);
                } else if (headerOption->sortIndicator == QStyleOptionHeader::SortUp) {
                    // Arrow points up (tip at the top)
                    triangle << QPointF(x, y + arrowHeight)
                             << QPointF(x + arrowWidth, y + arrowHeight)
                             << QPointF(x + (arrowWidth / 2.0), y);
                } else {
                    qDebug() << "headerOption->sortIndicator == QStyleOptionHeader::SortNone";
                }

                painter->drawPolygon(triangle);
                painter->restore();
                return; // Prevent Qt from doing the default drawing
            }
        }

        // Draw all other default elements (scrollbars, header, etc.) normally
        QProxyStyle::drawPrimitive(element, option, painter, widget);
    }
};

#endif // CUSTOMPROXYSTYLE_H
