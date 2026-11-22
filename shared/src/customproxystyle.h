#ifndef CUSTOMPROXYSTYLE_H
#define CUSTOMPROXYSTYLE_H

#include "customtablemodel.h"

#include <QAbstractItemView>
#include <QFrame>
#include <QPainter>
#include <QProxyStyle>
#include <QStyleOption>

class CustomProxyStyle : public QProxyStyle {
public:
    // The constructor passes the current system style through
    explicit CustomProxyStyle(QStyle *style = nullptr) : QProxyStyle(style) {}

    // Opt-in: draw hover/selection/focus rectangles of item view items with square corners.
    // Breeze rounds them in every item view except QTableView (and always rounds the focus frame).
    void setSquareItemViewPanels(bool square) { m_squareItemViewPanels = square; }

    void drawPrimitive(PrimitiveElement element, const QStyleOption *option,
                       QPainter *painter, const QWidget *widget = nullptr) const override
    {
        // No hover highlighting of item view items (in all views): draw the item as if the mouse were not over it
        if (element == QStyle::PE_PanelItemViewItem && (option->state & State_MouseOver)) {
            if (const auto *viewItemOption = qstyleoption_cast<const QStyleOptionViewItem *>(option)) {
                QStyleOptionViewItem noHoverOption(*viewItemOption);
                noHoverOption.state &= ~State_MouseOver;
                drawPrimitive(element, &noHoverOption, painter, widget);
                return;
            }
        }

        if (m_squareItemViewPanels) {
            if (element == QStyle::PE_PanelItemViewItem) {
                if (drawSquarePanelItemViewItem(option, painter, widget)) return;
            }
            else if (element == QStyle::PE_FrameFocusRect && qobject_cast<const QAbstractItemView *>(widget)) {
                // The focus frame is drawn in drawSquarePanelItemViewItem instead: QCommonStyle only passes the
                // SE_ItemViewItemFocusRect here (e.g. just the text area), not the whole icon+text item.
                return;
            }
        }

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

private:
    bool m_squareItemViewPanels = false;

    // Breeze 6 values (kstyle/breezemetrics.h), so that the square rectangles have the same size and colors
    static constexpr int itemMarginLeft = 2;
    static constexpr int itemMarginRight = 2;
    static constexpr int itemMarginTop = 1;
    static constexpr int itemMarginBottom = 1;
    static constexpr int firstItemMarginTop = 2;
    static constexpr qreal hoverAlpha = 0.3;  // Metrics::Blend_Value
    static constexpr int focusLighten = 110;  // Metrics::Focus_LightenColorValue
    static constexpr qreal selectedCoreAlpha = 0.6;       // fill of selected items (= hover fill)
    static constexpr qreal selectedFocusFrameAlpha = 1.0; // frame of the selected+focused item (= hover frame)

    // Same margins Breeze uses around an item's highlight (Helper::itemViewItemMargins for non-row selection)
    static QRect itemHighlightRect(const QRect &rect, const QModelIndex &index, const QWidget *widget) {
        const bool isFirst = index.isValid() && index.row() == 0;
        QMargins margins(itemMarginLeft, isFirst ? firstItemMarginTop : itemMarginTop, itemMarginRight, itemMarginBottom);

        const auto *frame = qobject_cast<const QFrame *>(widget);
        if (frame && frame->frameShape() == QFrame::StyledPanel) {
            margins -= QMargins(1, isFirst ? 1 : 0, 1, 0); // Breeze frame has one extra pixel
        }
        return rect.marginsRemoved(margins);
    }

    // Draws a filled rectangle with a 1px outline lying exactly on the pixel grid (no rounded corners)
    static void drawSquareRect(QPainter *painter, const QRect &rect, const QBrush &fill, const QColor &outline) {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, false);
        if (fill.style() != Qt::NoBrush) painter->fillRect(rect, fill);
        if (outline.isValid()) {
            painter->setPen(outline);
            painter->setBrush(Qt::NoBrush);
            painter->drawRect(rect.adjusted(0, 0, -1, -1));
        }
        painter->restore();
    }

    // Port of Breeze's drawPanelItemViewItemPrimitive, but always with square corners
    bool drawSquarePanelItemViewItem(const QStyleOption *option, QPainter *painter, const QWidget *widget) const {
        const auto *viewItemOption = qstyleoption_cast<const QStyleOptionViewItem *>(option);
        if (!viewItemOption) return false; // let the base style handle it

        if (!widget) widget = viewItemOption->widget;
        const auto *abstractItemView = qobject_cast<const QAbstractItemView *>(widget);

        const QStyle::State state = option->state;
        const bool mouseOver = (state & State_MouseOver) && (!abstractItemView || abstractItemView->selectionMode() != QAbstractItemView::NoSelection);
        const bool selected = state & State_Selected;
        const bool hasCustomBackground = viewItemOption->backgroundBrush.style() != Qt::NoBrush && !selected;
        const bool hasSolidBackground = !hasCustomBackground || viewItemOption->backgroundBrush.style() == Qt::SolidPattern;
        const bool hasAlternateBackground = viewItemOption->features & QStyleOptionViewItem::Alternate;
        // Like Breeze: focus frame only on unselected items (selected ones are recognizable anyway)
        const bool showFocusFrame = (state & State_HasFocus) && !selected;

        if (!(mouseOver || selected || hasCustomBackground || hasAlternateBackground || showFocusFrame)) return true;

        const QPalette::ColorGroup colorGroup = !(state & State_Enabled) ? QPalette::Disabled
                                              : (state & State_Active)   ? QPalette::Active
                                                                         : QPalette::Inactive;

        if (hasAlternateBackground) {
            painter->fillRect(option->rect, option->palette.color(colorGroup, QPalette::AlternateBase));
        }

        if (!(mouseOver || selected || hasCustomBackground)) {
            if (showFocusFrame) {
                // Same color as the hover frame of an unselected item, but without background fill.
                // (Breeze would use QStyleOptionFocusRect::backgroundColor = QPalette::Window -> grey frame)
                QColor focusColor = option->palette.color(colorGroup, QPalette::Highlight);
                focusColor.setAlphaF(0.8);
                drawSquareRect(painter, itemHighlightRect(option->rect, viewItemOption->index, widget), Qt::NoBrush, focusColor);
            }
            return true;
        }

        if (hasCustomBackground && !hasSolidBackground) {
            painter->save();
            painter->setBrushOrigin(option->rect.topLeft());
            painter->fillRect(option->rect, viewItemOption->backgroundBrush);
            painter->restore();
            return true;
        }

        // Selected items: translucent core like the hover rectangle of an unselected item.
        // Focused (current item, view has focus): with the more opaque hover frame; otherwise without
        // an own frame (the edge keeps the core color). The text is drawn in QPalette::Text (see ListItemDelegate).
        // Exception: items with UseRedBackgroundRole keep the opaque red selection with white text.
        if (selected && !viewItemOption->index.data(CustomTableModel::UseRedBackgroundRole).toBool()) {
            QColor coreColor = option->palette.color(colorGroup, QPalette::Highlight);
            coreColor.setAlphaF(selectedCoreAlpha);

            QColor frameColor; // invalid = no frame
            if (state & State_HasFocus) {
                frameColor = option->palette.color(colorGroup, QPalette::Highlight);
                frameColor.setAlphaF(selectedFocusFrameAlpha);
            }

            drawSquareRect(painter, itemHighlightRect(option->rect, viewItemOption->index, widget), coreColor, frameColor);
            return true;
        }

        QColor color = (hasCustomBackground && hasSolidBackground) ? viewItemOption->backgroundBrush.color()
                                                                   : option->palette.color(colorGroup, QPalette::Highlight);
        if (mouseOver && !hasCustomBackground) {
            if (!selected) {
                color.setAlphaF(hoverAlpha);
            } else {
                color = color.lighter(focusLighten);
            }
        }

        QColor outlineColor = color;
        if (!hasCustomBackground) {
            outlineColor.setAlphaF(selected ? 1.0 : 0.8);
        }

        drawSquareRect(painter, itemHighlightRect(option->rect, viewItemOption->index, widget), color, outlineColor);
        return true;
    }
};

#endif // CUSTOMPROXYSTYLE_H
