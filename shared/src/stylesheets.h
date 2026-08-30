#ifndef STYLESHEETS_H
#define STYLESHEETS_H

#include <QString>

namespace Styles {

    using namespace Qt::StringLiterals;

    inline const auto tableViewElevatedLinux = uR"(
            QTableView {
                background-color: #aa0000;
                alternate-background-color: #880000;
                color: #ffffff;
            }
            QTableView::item:hover {
                background-color: #aa0000;
            }
            QTableView::item:alternate:hover {
                background-color: #880000;
            }
        )"_s;

    inline const auto tableViewElevated = uR"(
            QTableView {
                border: none;
                background-color: #aa0000;
                alternate-background-color: #880000;
                color: white;
                selection-color: white;
                outline: none;
            }
            QTableView::item:hover {
                background-color: #aa0000;
            }
            QTableView::item:alternate:hover {
                background-color: #880000;
            }
            QTableView::item:selected:active,
            QTableView::item:selected:active:hover {
                background-color: #6b69d6;
            }
            QTableView::item:selected:!active,
            QTableView::item:selected:!active:hover {
                background-color: #a0a0a0;
            }
            /* Styling für die Ecke unten rechts */
            QAbstractScrollArea::corner {
                background: #1b1b1b;
                border: none;
            }
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

    inline const auto tableViewDark = uR"(
            QTableView {
                border: none;
                background-color: #2e2e2e;
                alternate-background-color: #282828;
                color: white;
                selection-color: white;
                outline: none;
            }
            QTableView::item:hover {
                background-color: #2e2e2e;
            }
            QTableView::item:alternate:hover {
                background-color: #282828;
            }
            QTableView::item:selected:active,
            QTableView::item:selected:active:hover {
                background-color: #6b69d6;
            }
            QTableView::item:selected:!active,
            QTableView::item:selected:!active:hover {
                background-color: #505050;
            }
            /* Styling für die Ecke unten rechts */
            QAbstractScrollArea::corner {
                background: #1b1b1b;
                border: none;
            }
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

    inline const auto tableViewLight = uR"(
            QTableView {
                border: none;
                background-color: white;
                alternate-background-color: #f0f0f0;
                color: black;
                selection-color: black;
                outline: none;
            }
            QTableView::item:hover {
                background-color: white;
            }
            QTableView::item:alternate:hover {
                background-color: #f0f0f0;
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

    inline const auto listViewElevated = uR"(
            QListView {
                border: none;
                background-color: #aa0000;
                alternate-background-color: #880000;
                color: white;
                selection-color: white;
                outline: none;
            }
            QListView::item {
                padding-top: 0px;
                padding-bottom: 0px;
                height: 18px;
            }
            QListView::item:hover {
                background-color: #aa0000;
            }
            QListView::item:selected:active,
            QListView::item:selected:active:hover {
                background-color: #6b69d6;
            }
            QListView::item:selected:!active,
            QListView::item:selected:!active:hover {
                background-color: #a0a0a0;
            }
            /* Styling für die Ecke unten rechts */
            QAbstractScrollArea::corner {
                background: #1b1b1b;
                border: none;
            }
        )"_s;

    inline const auto listViewDark = uR"(
            QListView {
                border: none;
                background-color: #2e2e2e;
                color: white;
                selection-color: white;
                outline: none;
            }
            QListView::item {
                padding-top: 0px;
                padding-bottom: 0px;
                height: 18px;
            }
            QListView::item:hover {
                background-color: #2e2e2e;
            }
            QListView::item:selected:active,
            QListView::item:selected:active:hover {
                background-color: #6b69d6;
            }
            QListView::item:selected:!active,
            QListView::item:selected:!active:hover {
                background-color: #505050;
            }
            /* Styling für die Ecke unten rechts */
            QAbstractScrollArea::corner {
                background: #1b1b1b;
                border: none;
            }
        )"_s;

    inline const auto listViewLight = uR"(
            QListView {
                border: none;
                background-color: white;
                color: black;
                selection-color: black;
                outline: none;
            }
            QListView::item {
                padding-top: 0px;
                padding-bottom: 0px;
                height: 18px;
            }
            QListView::item:hover {
                background-color: white;
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

    inline const auto listViewElevatedLinux = uR"(
            QListView {
                background-color: #aa0000;
                color: #ffffff;
            }
            QListView::item {
                padding-top: 0px;
                padding-bottom: 0px;
                height: 18px;
            }
        )"_s;

    inline const auto listViewLinux = uR"(
            QListView::item {
                padding-top: 0px;
                padding-bottom: 0px;
                height: 18px;
            }
        )"_s;

    inline const auto thumbnailViewElevated = uR"(
            QListView {
                border: none;
                background-color: #aa0000;
                color: white;
                selection-color: white;
                outline: none;
            }
            QListView::item:hover {
                background-color: #aa0000;
            }
            QListView::item:selected:active,
            QListView::item:selected:active:hover {
                background-color: #6b69d6;
            }
            QListView::item:selected:!active,
            QListView::item:selected:!active:hover {
                background-color: #a0a0a0;
            }
            /* Styling für die Ecke unten rechts */
            QAbstractScrollArea::corner {
                background: #1b1b1b;
                border: none;
            }
        )"_s;

    inline const auto thumbnailViewDark = uR"(
            QListView {
                border: none;
                background-color: #2e2e2e;
                color: white;
                selection-color: white;
                outline: none;
            }
            QListView::item:hover {
                background-color: #2e2e2e;
            }
            QListView::item:selected:active,
            QListView::item:selected:active:hover {
                background-color: #6b69d6;
            }
            QListView::item:selected:!active,
            QListView::item:selected:!active:hover {
                background-color: #505050;
            }
            /* Styling für die Ecke unten rechts */
            QAbstractScrollArea::corner {
                background: #1b1b1b;
                border: none;
            }
        )"_s;

    inline const auto thumbnailViewLight = uR"(
            QListView {
                border: none;
                background-color: white;
                color: black;
                selection-color: black;
                outline: none;
            }
            QListView::item:hover {
                background-color: white;
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

    inline const auto thumbnailViewElevatedLinux = uR"(
            QListView {
                background-color: #aa0000;
                color: #ffffff;
            }
        )"_s;

} // namespace Styles

#endif // STYLESHEETS_H
