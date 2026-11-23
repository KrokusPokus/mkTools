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

    // Fixed row height for the list view (not for thumbnails!)
    inline const auto listViewItemHeight = uR"(
            QListView::item {
                padding-top: 0px;
                padding-bottom: 0px;
                height: 18px;
            }
        )"_s;

    // =========================================================================
    // 2. SONSTIGES
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
