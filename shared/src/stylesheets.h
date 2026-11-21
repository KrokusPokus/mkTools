#ifndef STYLESHEETS_H
#define STYLESHEETS_H

#include <QString>
#include <QStringBuilder>

// The style sheets are split into small, non-overlapping building blocks that are combined in the
// MainWindows with operator% (QStringBuilder). Every combination of
// selector and property appears in exactly one building block, so the order
// of the building blocks does not matter when combining them.
//
// Deliberately no combined selectors like "QTableView, QListView": a style sheet also affects
// child widgets (e.g. editors), so every view only gets rules for its own type.

namespace Styles {

    using namespace Qt::StringLiterals;

    // =========================================================================
    // 1. STRUCTURE (frame, spacing, row height)
    // =========================================================================

    inline const auto tableViewBase = uR"(
            QTableView {
                border: none;
                outline: none;
            }
        )"_s;

    inline const auto listViewBase = uR"(
            QListView {
                border: none;
                outline: none;
            }
        )"_s;

    // Fixed row height for the list view (not for thumbnails!)
    inline const auto listViewItemHeight = uR"(
            QListView::item {
                padding-top: 0px;
                padding-bottom: 0px;
                height: 18px;
            }
        )"_s;

    // Dark table header
    inline const auto headerViewDark = uR"(
            QHeaderView {
                background-color: #404040;
                border: none;
            }
            QHeaderView::section {
                border: none;
                background-color: #404040;
                color: #ffffff;
                font-weight: normal;
                border-bottom: 1px solid #616161;
                border-right: 1px solid #616161;
                padding-left: 6px;
                padding-right: 6px;
            }
        )"_s;

    // Bottom right corner between the scrollbars
    inline const auto scrollAreaCornerDark = uR"(
            QAbstractScrollArea::corner {
                background: #1b1b1b;
                border: none;
            }
        )"_s;

    // =========================================================================
    // 2. COLORS (background, alternating rows, text)
    // =========================================================================

    inline const auto tableViewColorsElevated = uR"(
            QTableView {
                background-color: #aa0000;
                alternate-background-color: #880000;
                color: #ffffff;
            }
        )"_s;

    inline const auto tableViewColorsDark = uR"(
            QTableView {
                background-color: #2e2e2e;
                alternate-background-color: #282828;
                color: white;
            }
        )"_s;

    inline const auto tableViewColorsLight = uR"(
            QTableView {
                background-color: white;
                alternate-background-color: #f0f0f0;
                color: black;
            }
        )"_s;

    inline const auto listViewColorsElevated = uR"(
            QListView {
                background-color: #aa0000;
                color: #ffffff;
            }
        )"_s;

    inline const auto listViewColorsDark = uR"(
            QListView {
                background-color: #2e2e2e;
                color: white;
            }
        )"_s;

    inline const auto listViewColorsLight = uR"(
            QListView {
                background-color: white;
                color: black;
            }
        )"_s;

    // =========================================================================
    // 3. HOVER (suppress hover highlighting: the background stays unchanged)
    // =========================================================================

    inline const auto tableViewHoverElevated = uR"(
            QTableView::item:hover {
                background-color: #aa0000;
            }
            QTableView::item:alternate:hover {
                background-color: #880000;
            }
        )"_s;

    inline const auto tableViewHoverDark = uR"(
            QTableView::item:hover {
                background-color: #2e2e2e;
            }
            QTableView::item:alternate:hover {
                background-color: #282828;
            }
        )"_s;

    inline const auto tableViewHoverLight = uR"(
            QTableView::item:hover {
                background-color: white;
            }
            QTableView::item:alternate:hover {
                background-color: #f0f0f0;
            }
        )"_s;

    inline const auto listViewHoverElevated = uR"(
            QListView::item:hover {
                background-color: #aa0000;
            }
        )"_s;

    inline const auto listViewHoverDark = uR"(
            QListView::item:hover {
                background-color: #2e2e2e;
            }
        )"_s;

    inline const auto listViewHoverLight = uR"(
            QListView::item:hover {
                background-color: white;
            }
        )"_s;

    // =========================================================================
    // 4. SELECTION (text color and background of selected entries)
    // =========================================================================

    inline const auto tableViewSelectionElevated = uR"(
            QTableView {
                selection-color: white;
            }
            QTableView::item:selected:active,
            QTableView::item:selected:active:hover {
                background-color: #6b69d6;
            }
            QTableView::item:selected:!active,
            QTableView::item:selected:!active:hover {
                background-color: #a0a0a0;
            }
        )"_s;

    inline const auto tableViewSelectionDark = uR"(
            QTableView {
                selection-color: white;
            }
            QTableView::item:selected:active,
            QTableView::item:selected:active:hover {
                background-color: #6b69d6;
            }
            QTableView::item:selected:!active,
            QTableView::item:selected:!active:hover {
                background-color: #505050;
            }
        )"_s;

    inline const auto tableViewSelectionLight = uR"(
            QTableView {
                selection-color: black;
            }
            QTableView::item:selected:active,
            QTableView::item:selected:active:hover {
                background-color: #cce8ff;
            }
            QTableView::item:selected:!active,
            QTableView::item:selected:!active:hover {
                background-color: #d9d9d9;
            }
        )"_s;

    inline const auto listViewSelectionElevated = uR"(
            QListView {
                selection-color: white;
            }
            QListView::item:selected:active,
            QListView::item:selected:active:hover {
                background-color: #6b69d6;
            }
            QListView::item:selected:!active,
            QListView::item:selected:!active:hover {
                background-color: #a0a0a0;
            }
        )"_s;

    inline const auto listViewSelectionDark = uR"(
            QListView {
                selection-color: white;
            }
            QListView::item:selected:active,
            QListView::item:selected:active:hover {
                background-color: #6b69d6;
            }
            QListView::item:selected:!active,
            QListView::item:selected:!active:hover {
                background-color: #505050;
            }
        )"_s;

    inline const auto listViewSelectionLight = uR"(
            QListView {
                selection-color: black;
            }
            QListView::item:selected:active,
            QListView::item:selected:active:hover {
                background-color: #cce8ff;
            }
            QListView::item:selected:!active,
            QListView::item:selected:!active:hover {
                background-color: #d9d9d9;
            }
        )"_s;

    // =========================================================================
    // 5. SCROLLBARS
    // =========================================================================

    inline const auto verticalScrollBarDark = uR"(
            QScrollBar:vertical {
                border: none;
                background: #1b1b1b;
                width: 17px;
                margin: 5px 0px 5px 0px;
            }

            QScrollBar::handle:vertical {
                background: #3f3f3f;
                min-height: 20px;
                margin-left: 5px;
                margin-right: 5px;
                border-radius: 3px;
            }

            QScrollBar::handle:vertical:hover {
                background: #6966f7;
            }

            QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {
                background: none;
            }

            QScrollBar::sub-line:vertical {
                border: none;
                background: #1b1b1b;
                height: 5px;
                subcontrol-position: top;
                subcontrol-origin: margin;
            }

            QScrollBar::add-line:vertical {
                border: none;
                background: #1b1b1b;
                height: 5px;
                subcontrol-position: bottom;
                subcontrol-origin: margin;
            }
        )"_s;

    inline const auto horizontalScrollBarDark = uR"(
            QScrollBar:horizontal {
                border: none;
                background: #1b1b1b;
                height: 17px;
                margin: 0px 5px 0px 5px;
            }

            QScrollBar::handle:horizontal {
                background: #3f3f3f;
                min-width: 20px;
                margin-top: 5px;
                margin-bottom: 5px;
                border-radius: 3px;
            }

            QScrollBar::handle:horizontal:hover {
                background: #6966f7;
            }

            QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal {
                background: none;
            }

            QScrollBar::sub-line:horizontal {
                border: none;
                background: #1b1b1b;
                width: 5px;
                subcontrol-position: left;
                subcontrol-origin: margin;
            }

            QScrollBar::add-line:horizontal {
                border: none;
                background: #1b1b1b;
                width: 5px;
                subcontrol-position: right;
                subcontrol-origin: margin;
            }
        )"_s;

    // =========================================================================
    // 6. SONSTIGES
    // =========================================================================

    inline const auto groupBox = uR"(
        QGroupBox {
            border: 1px solid #565656;
            border-radius: 5px;
            background-color: palette(base);
        }
    )"_s;

} // namespace Styles

#endif // STYLESHEETS_H
