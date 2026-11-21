#ifndef CUSTOMITEMDELEGATES_H
#define CUSTOMITEMDELEGATES_H

#include "customtablemodel.h"

#include <QApplication>
#include <QLineEdit>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QTextLayout>
#include <QTimer>

class ListItemDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        bool isCut = index.data(CustomTableModel::IsCutRole).toBool();
        bool isHidden = index.data(CustomTableModel::IsHiddenRole).toBool();

        // Reference instead of a copy, since opt is not modified (paint() runs for every cell).
        const QStyleOptionViewItem &opt = option;

        if (isCut || isHidden) {
            painter->save();
            if (isCut && isHidden) {
                painter->setOpacity(0.25);
            } else {
                painter->setOpacity(0.50);
            }
            QStyledItemDelegate::paint(painter, opt, index);
            painter->restore();
        } else {
            QStyledItemDelegate::paint(painter, opt, index);
        }
    }

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        QVariant sizeHintVariant = index.data(CustomTableModel::ListViewSizeHintRole); // use precalculated value
        if (sizeHintVariant.isValid()) {
            QSize sizeHintCached = sizeHintVariant.toSize();
            if (sizeHintCached.isValid())
                return sizeHintCached;
        }
        return QStyledItemDelegate::sizeHint(option, index); // fallback
    }

    // Called as soon as the edit box is opened (no matter whether by F2 or by a click)
    QWidget* createEditor(QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        QWidget *editor = QStyledItemDelegate::createEditor(parent, option, index);
        if (editor) {
            // Since this Qt method is 'const', we have to fire the signal via const_cast
            emit const_cast<ListItemDelegate*>(this)->editingStarted();
        }
        return editor;
    }

    void setEditorData(QWidget *editor, const QModelIndex &index) const override {
        QStyledItemDelegate::setEditorData(editor, index);

        if (QLineEdit *lineEdit = qobject_cast<QLineEdit*>(editor)) {
            QString text = lineEdit->text();
            QString displayName = index.data(CustomTableModel::DisplayNameRole).toString();

            // Check whether displayName is not empty AND really is the beginning of text
            if (!displayName.isEmpty() && text.startsWith(displayName)) {
                int displayNameLength = displayName.length();
                QTimer::singleShot(0, lineEdit, [lineEdit, displayNameLength]() {
                    lineEdit->setSelection(0, displayNameLength);
                });
            } else {
                // Fallback for .desktop files or differing display names
                lineEdit->selectAll();
            }
        }
    }

signals:
    void editingStarted();
    // Note: This class already inherits the counterpart 'closeEditor' automatically from Qt!

protected:
    void initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const override {
        QStyledItemDelegate::initStyleOption(option, index);

        option->decorationPosition = QStyleOptionViewItem::Left;
        option->displayAlignment = Qt::AlignLeft | Qt::AlignVCenter;

        if (index.data(CustomTableModel::UseRedTextRole).toBool()) {
            static const QColor exeRed(255, 74, 70);
            option->palette.setColor(QPalette::Text, exeRed);
            option->palette.setColor(QPalette::HighlightedText, exeRed);
        }

        if (index.data(CustomTableModel::UseRedBackgroundRole).toBool()) {
            static const QColor pureRed(255, 0, 0);
            option->palette.setColor(QPalette::Highlight, pureRed);
            option->palette.setColor(QPalette::HighlightedText, Qt::white);
        }
    }
};

class TableItemDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        bool isCut = index.data(CustomTableModel::IsCutRole).toBool();
        bool isHidden = index.data(CustomTableModel::IsHiddenRole).toBool();

        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);

        painter->save();
        if (isCut && isHidden) {
            painter->setOpacity(0.25);
        } else if (isCut || isHidden) {
            painter->setOpacity(0.50);
        }

        if (index.column() == CustomTableModel::eColPath) {
            QStyle *style = opt.widget ? opt.widget->style() : QApplication::style();

            QRect textRect = style->subElementRect(QStyle::SE_ItemViewItemText, &opt, opt.widget);

            int availableWidth = textRect.width() - 8;  // Substract a few px as buffer against CSS-Padding
            if (availableWidth > 0) {
                opt.text = opt.fontMetrics.elidedText(opt.text, Qt::ElideMiddle, availableWidth);
                opt.textElideMode = Qt::ElideNone;
            }
        }

        // 3. IMPORTANT: Call the style drawing command directly!
        // If we called QStyledItemDelegate::paint(), Qt would overwrite our 'opt' again.
        QStyle *style = opt.widget ? opt.widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);

        painter->restore();
    }

    // Called as soon as the edit box is opened (no matter whether by F2 or by a click)
    QWidget* createEditor(QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        QWidget *editor = QStyledItemDelegate::createEditor(parent, option, index);
        if (editor) {
            // Since this Qt method is 'const', we have to fire the signal via const_cast
            emit const_cast<TableItemDelegate*>(this)->editingStarted();
        }
        return editor;
    }

    void setEditorData(QWidget *editor, const QModelIndex &index) const override {
        QStyledItemDelegate::setEditorData(editor, index);

        if (QLineEdit *lineEdit = qobject_cast<QLineEdit*>(editor)) {
            QString text = lineEdit->text();
            QString displayName = index.data(CustomTableModel::DisplayNameRole).toString();

            // Check whether displayName is not empty AND really is the beginning of text
            if (!displayName.isEmpty() && text.startsWith(displayName)) {
                int displayNameLength = displayName.length();
                QTimer::singleShot(0, lineEdit, [lineEdit, displayNameLength]() {
                    lineEdit->setSelection(0, displayNameLength);
                });
            } else {
                // Fallback for .desktop files or differing display names
                lineEdit->selectAll();
            }
        }
    }

signals:
    void editingStarted();
    // Note: This class already inherits the counterpart 'closeEditor' automatically from Qt!

protected:
    void initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const override {
        QStyledItemDelegate::initStyleOption(option, index);

        if (index.column() == CustomTableModel::eColPath) {
            option->textElideMode = Qt::ElideMiddle;
        }

        if (index.data(CustomTableModel::UseRedTextRole).toBool()) {
            static const QColor exeRed(255, 74, 70);
            option->palette.setColor(QPalette::Text, exeRed);
            option->palette.setColor(QPalette::HighlightedText, exeRed);
        }

        if (index.data(CustomTableModel::UseRedBackgroundRole).toBool()) {
            static const QColor pureRed(255, 0, 0);
            option->palette.setColor(QPalette::Highlight, pureRed);
            option->palette.setColor(QPalette::HighlightedText, Qt::white);
        }
    }
};

class ThumbItemDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        // Compute a fixed icon box (uses the 96px from the view)
        int iconBoxSize = option.decorationSize.isValid() ? option.decorationSize.height() : 96;

        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);

        bool isCut = index.data(CustomTableModel::IsCutRole).toBool();
        bool isHidden = index.data(CustomTableModel::IsHiddenRole).toBool();

        painter->save();

        // =====================================================================
        // Your opacity logic now controls the whole custom rendering
        // =====================================================================
        if (isCut || isHidden) {
            if (isCut && isHidden) {
                painter->setOpacity(0.25);
            } else {
                painter->setOpacity(0.50);
            }
        }

        // 2. Draw the background (hover, selection, focus rectangle)
        QStyle *style = opt.widget ? opt.widget->style() : QApplication::style();
        style->drawPrimitive(QStyle::PE_PanelItemViewItem, &opt, painter, opt.widget);

        QRect itemRect = opt.rect;

        QRect iconBoxRect(itemRect.left() + (itemRect.width() - iconBoxSize) / 2,
                          itemRect.top() + 4,
                          iconBoxSize,
                          iconBoxSize);

        // Center and draw the icon within the fixed 96x96 box
        if (!opt.icon.isNull()) {
            QSize actualSize = opt.icon.actualSize(QSize(iconBoxSize, iconBoxSize), (opt.state & QStyle::State_Selected) ? QIcon::Selected : QIcon::Normal);
            QRect targetIconRect = QStyle::alignedRect(opt.direction, Qt::AlignCenter, actualSize, iconBoxRect);
            opt.icon.paint(painter, targetIconRect, Qt::AlignCenter, (opt.state & QStyle::State_Enabled) ? QIcon::Normal : QIcon::Disabled);
        }

        // 4. Compute the text box (strictly below the 96px icon box)
        QRect textRect = itemRect;
        textRect.setTop(iconBoxRect.bottom() + 4); // Keep 4px spacing
        QRect elideRect = textRect.adjusted(4, 0, -4, 0);

        if (!opt.text.isEmpty()) {
            painter->setFont(opt.font);

            // Determine the text color
            QPalette::ColorGroup cg = (opt.state & QStyle::State_Enabled) ? QPalette::Normal : QPalette::Disabled;
            if (opt.state & QStyle::State_Selected) {
                painter->setPen(opt.palette.color(cg, QPalette::HighlightedText));
            } else {
                painter->setPen(opt.palette.color(cg, QPalette::Text));
            }

            // =====================================================================
            // NEW: Smart wrapping (at word boundaries OR anywhere)
            // =====================================================================
            QTextOption textOption;
            textOption.setAlignment(Qt::AlignHCenter | Qt::AlignTop);
            textOption.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);

            // Important: use the overload with QRectF and QTextOption!
            painter->drawText(QRectF(elideRect), opt.text, textOption);
        }

        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        // 1. Determine the icon box
        int iconBoxSize = option.decorationSize.isValid() ? option.decorationSize.height() : 96;
        if (iconBoxSize <= 0) iconBoxSize = 96;

        int targetWidth = 104; // Your column width
        int textPadding = 8;   // 4px left, 4px right
        int availableTextWidth = targetWidth - textPadding;

        // 2. Get the text
        QString text = index.data(Qt::DisplayRole).toString();

        // =====================================================================
        // NEW: Height calculation via QTextLayout with smart wrap
        // =====================================================================
        int textHeight = 0;
        if (!text.isEmpty()) {
            QTextLayout layout(text, option.font);

            QTextOption textOption;
            textOption.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
            layout.setTextOption(textOption);

            layout.beginLayout();
            double y = 0;
            while (true) {
                QTextLine line = layout.createLine();
                if (!line.isValid())
                    break;

                line.setLineWidth(availableTextWidth);
                line.setPosition(QPointF(0, y));
                y += line.height();
            }
            layout.endLayout();

            // Read the total height of the text block (rounded up to whole pixels)
            textHeight = static_cast<int>(qCeil(layout.boundingRect().height()));
        }

        // 3. Total height = icon box + spacing + measured text height + bottom padding
        int calculatedHeight = 4 + iconBoxSize + 4 + textHeight + 4;

        return QSize(targetWidth, calculatedHeight);
    }

    QWidget* createEditor(QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        QWidget *editor = QStyledItemDelegate::createEditor(parent, option, index);
        if (editor) {
            // Since this Qt method is 'const', we have to fire the signal via const_cast
            emit const_cast<ThumbItemDelegate*>(this)->editingStarted();
        }
        if (auto *lineEdit = qobject_cast<QLineEdit*>(editor)) {
            lineEdit->setAlignment(Qt::AlignCenter);
        }
        return editor;
    }

    void setEditorData(QWidget *editor, const QModelIndex &index) const override {
        QStyledItemDelegate::setEditorData(editor, index);

        if (QLineEdit *lineEdit = qobject_cast<QLineEdit*>(editor)) {
            QString text = lineEdit->text();
            QString displayName = index.data(CustomTableModel::DisplayNameRole).toString();

            // Check whether displayName is not empty AND really is the beginning of text
            if (!displayName.isEmpty() && text.startsWith(displayName)) {
                int displayNameLength = displayName.length();
                QTimer::singleShot(0, lineEdit, [lineEdit, displayNameLength]() {
                    lineEdit->setSelection(0, displayNameLength);
                });
            } else {
                // Fallback for .desktop files or differing display names
                lineEdit->selectAll();
            }
        }
    }


signals:
    void editingStarted();
    // Note: This class already inherits the counterpart 'closeEditor' automatically from Qt!

protected:
    void initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const override {
        QStyledItemDelegate::initStyleOption(option, index);

        option->displayAlignment = Qt::AlignHCenter | Qt::AlignBottom;

        if (index.data(CustomTableModel::UseRedTextRole).toBool()) {
            static const QColor exeRed(255, 74, 70);
            option->palette.setColor(QPalette::Text, exeRed);
            option->palette.setColor(QPalette::HighlightedText, exeRed);
        }

        if (index.data(CustomTableModel::UseRedBackgroundRole).toBool()) {
            static const QColor pureRed(255, 0, 0);
            option->palette.setColor(QPalette::Highlight, pureRed);
            option->palette.setColor(QPalette::HighlightedText, Qt::white);
        }
    }
};

#endif // CUSTOMITEMDELEGATES_H
