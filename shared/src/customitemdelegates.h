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

        QStyleOptionViewItem opt = option;

        /*
        // Eigenen Selektions- und Fokus-Hintergrund zeichnen
        bool isSelected = (opt.state & QStyle::State_Selected);
        bool hasFocus   = (opt.state & QStyle::State_HasFocus);
        if (isSelected || hasFocus) {
            QRectF rect = opt.rect;
            rect.adjust(1, 1, -1, -1);

            if (isSelected) {
                //QColor bgColor = QColor(255, 0, 255, 0);
                QColor bgColor = opt.palette.color(QPalette::Highlight);
                bgColor.setAlpha(hasFocus ? 100 : 100);
                painter->setBrush(bgColor);
            } else {
                painter->setBrush(Qt::NoBrush);
            }

            if (hasFocus) {
                QColor borderColor = opt.palette.color(QPalette::Highlight);
                painter->setPen(QPen(borderColor, 1, Qt::SolidLine));
            } else {
                painter->setPen(Qt::NoPen);
            }

            painter->drawRect(rect);
        }

        // 3. Das restliche Standard-Zeichnen modifizieren
        opt.state &= ~QStyle::State_HasFocus; // Standard-Fokusrahmen ausschalten
        opt.state &= ~QStyle::State_Selected; // Standard-Auswahlbalken ausschalten
        if (isSelected) {
            opt.palette.setColor(QPalette::Text, opt.palette.color(QPalette::WindowText));
        }
        */
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

    // Wird aufgerufen, sobald die EditBox geöffnet wird (egal ob durch F2 oder Klick)
    QWidget* createEditor(QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        QWidget *editor = QStyledItemDelegate::createEditor(parent, option, index);
        if (editor) {
            // Da diese Qt-Methode 'const' ist, müssen wir das Signal über const_cast abfeuern
            emit const_cast<ListItemDelegate*>(this)->editingStarted();
        }
        return editor;
    }

    void setEditorData(QWidget *editor, const QModelIndex &index) const override {
        QStyledItemDelegate::setEditorData(editor, index);

        if (QLineEdit *lineEdit = qobject_cast<QLineEdit*>(editor)) {
            QString text = lineEdit->text();
            QString displayName = index.data(CustomTableModel::DisplayNameRole).toString();

            // Prüfen, ob displayName nicht leer ist UND tatsächlich den Anfang von text bildet
            if (!displayName.isEmpty() && text.startsWith(displayName)) {
                int displayNameLength = displayName.length();
                QTimer::singleShot(0, lineEdit, [lineEdit, displayNameLength]() {
                    lineEdit->setSelection(0, displayNameLength);
                });
            } else {
                // Fallback für .desktop-Dateien oder abweichende DisplayNames
                lineEdit->selectAll();
            }
        }
    }

signals:
    void editingStarted();
    // Hinweis: Das Gegenstück 'closeEditor' erbt diese Klasse bereits automatisch von Qt!

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

        if (index.column() == 1) {
            QStyle *style = opt.widget ? opt.widget->style() : QApplication::style();

            QRect textRect = style->subElementRect(QStyle::SE_ItemViewItemText, &opt, opt.widget);

            int availableWidth = textRect.width() - 8;  // Substract a few px as buffer against CSS-Padding
            if (availableWidth > 0) {
                opt.text = opt.fontMetrics.elidedText(opt.text, Qt::ElideMiddle, availableWidth);
                opt.textElideMode = Qt::ElideNone;
            }
        }

        // 3. WICHTIG: Direkt den Style-Zeichenbefehl aufrufen!
        // Würden wir QStyledItemDelegate::paint() aufrufen, würde Qt unsere 'opt' wieder überschreiben.
        QStyle *style = opt.widget ? opt.widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);

        painter->restore();
    }

    // Wird aufgerufen, sobald die EditBox geöffnet wird (egal ob durch F2 oder Klick)
    QWidget* createEditor(QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        QWidget *editor = QStyledItemDelegate::createEditor(parent, option, index);
        if (editor) {
            // Da diese Qt-Methode 'const' ist, müssen wir das Signal über const_cast abfeuern
            emit const_cast<TableItemDelegate*>(this)->editingStarted();
        }
        return editor;
    }

    void setEditorData(QWidget *editor, const QModelIndex &index) const override {
        QStyledItemDelegate::setEditorData(editor, index);

        if (QLineEdit *lineEdit = qobject_cast<QLineEdit*>(editor)) {
            QString text = lineEdit->text();
            QString displayName = index.data(CustomTableModel::DisplayNameRole).toString();

            // Prüfen, ob displayName nicht leer ist UND tatsächlich den Anfang von text bildet
            if (!displayName.isEmpty() && text.startsWith(displayName)) {
                int displayNameLength = displayName.length();
                QTimer::singleShot(0, lineEdit, [lineEdit, displayNameLength]() {
                    lineEdit->setSelection(0, displayNameLength);
                });
            } else {
                // Fallback für .desktop-Dateien oder abweichende DisplayNames
                lineEdit->selectAll();
            }
        }
    }

signals:
    void editingStarted();
    // Hinweis: Das Gegenstück 'closeEditor' erbt diese Klasse bereits automatisch von Qt!

protected:
    void initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const override {
        QStyledItemDelegate::initStyleOption(option, index);

        if (index.column() == 1) {
            option->textElideMode = Qt::ElideMiddle;
        }

        if (index.data(CustomTableModel::UseRedTextRole).toBool()) {
            static const QColor exeRed(255, 74, 70);
            option->palette.setColor(QPalette::Text, exeRed);
            option->palette.setColor(QPalette::HighlightedText, exeRed);
        }
    }
};

class ThumbItemDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        // Feste Icon-Box berechnen (Nutzt die 96px aus der View)
        int iconBoxSize = option.decorationSize.isValid() ? option.decorationSize.height() : 96;

        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);

        bool isCut = index.data(CustomTableModel::IsCutRole).toBool();
        bool isHidden = index.data(CustomTableModel::IsHiddenRole).toBool();

        painter->save();

        // =====================================================================
        // Deine Deckkraft-Logik steuert jetzt das gesamte Custom-Rendering
        // =====================================================================
        if (isCut || isHidden) {
            if (isCut && isHidden) {
                painter->setOpacity(0.25);
            } else {
                painter->setOpacity(0.50);
            }
        }

        // 2. Hintergrund zeichnen (Hover, Selektion, Fokus-Rechteck)
        QStyle *style = opt.widget ? opt.widget->style() : QApplication::style();
        style->drawPrimitive(QStyle::PE_PanelItemViewItem, &opt, painter, opt.widget);

        QRect itemRect = opt.rect;

        QRect iconBoxRect(itemRect.left() + (itemRect.width() - iconBoxSize) / 2,
                          itemRect.top() + 4,
                          iconBoxSize,
                          iconBoxSize);

        // Icon innerhalb der festen 96x96 Box zentrieren und zeichnen
        if (!opt.icon.isNull()) {
            QSize actualSize = opt.icon.actualSize(QSize(iconBoxSize, iconBoxSize), (opt.state & QStyle::State_Selected) ? QIcon::Selected : QIcon::Normal);
            QRect targetIconRect = QStyle::alignedRect(opt.direction, Qt::AlignCenter, actualSize, iconBoxRect);
            opt.icon.paint(painter, targetIconRect, Qt::AlignCenter, (opt.state & QStyle::State_Enabled) ? QIcon::Normal : QIcon::Disabled);
        }

        // 4. Text-Box berechnen (strikt unterhalb der 96px Icon-Box)
        QRect textRect = itemRect;
        textRect.setTop(iconBoxRect.bottom() + 4); // 4px Abstand halten
        QRect elideRect = textRect.adjusted(4, 0, -4, 0);

        if (!opt.text.isEmpty()) {
            painter->setFont(opt.font);

            // Textfarbe ermitteln
            QPalette::ColorGroup cg = (opt.state & QStyle::State_Enabled) ? QPalette::Normal : QPalette::Disabled;
            if (opt.state & QStyle::State_Selected) {
                painter->setPen(opt.palette.color(cg, QPalette::HighlightedText));
            } else {
                painter->setPen(opt.palette.color(cg, QPalette::Text));
            }

            // =====================================================================
            // NEU: Intelligenter Umbruch (Wortgrenze ODER überall)
            // =====================================================================
            QTextOption textOption;
            textOption.setAlignment(Qt::AlignHCenter | Qt::AlignTop);
            textOption.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);

            // Wichtig: Überladung mit QRectF und QTextOption nutzen!
            painter->drawText(QRectF(elideRect), opt.text, textOption);
        }

        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        // 1. Icon-Box bestimmen
        int iconBoxSize = option.decorationSize.isValid() ? option.decorationSize.height() : 96;
        if (iconBoxSize <= 0) iconBoxSize = 96;

        int targetWidth = 104; // Deine Spaltenbreite
        int textPadding = 8;   // 4px links, 4px rechts
        int availableTextWidth = targetWidth - textPadding;

        // 2. Text holen
        QString text = index.data(Qt::DisplayRole).toString();

        // =====================================================================
        // NEU: Höhenberechnung via QTextLayout mit Smart-Wrap
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

            // Die Gesamthöhe des Textblocks auslesen (aufgerundet auf ganze Pixel)
            textHeight = static_cast<int>(qCeil(layout.boundingRect().height()));
        }

        // 3. Gesamthöhe = Icon-Box + Abstand + gemessene Texthöhe + Puffer unten
        int calculatedHeight = 4 + iconBoxSize + 4 + textHeight + 4;

        return QSize(targetWidth, calculatedHeight);
    }

    QWidget* createEditor(QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        QWidget *editor = QStyledItemDelegate::createEditor(parent, option, index);
        if (editor) {
            // Da diese Qt-Methode 'const' ist, müssen wir das Signal über const_cast abfeuern
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

            // Prüfen, ob displayName nicht leer ist UND tatsächlich den Anfang von text bildet
            if (!displayName.isEmpty() && text.startsWith(displayName)) {
                int displayNameLength = displayName.length();
                QTimer::singleShot(0, lineEdit, [lineEdit, displayNameLength]() {
                    lineEdit->setSelection(0, displayNameLength);
                });
            } else {
                // Fallback für .desktop-Dateien oder abweichende DisplayNames
                lineEdit->selectAll();
            }
        }
    }


signals:
    void editingStarted();
    // Hinweis: Das Gegenstück 'closeEditor' erbt diese Klasse bereits automatisch von Qt!

protected:
    void initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const override {
        QStyledItemDelegate::initStyleOption(option, index);

        option->displayAlignment = Qt::AlignHCenter | Qt::AlignBottom;

        if (index.data(CustomTableModel::UseRedTextRole).toBool()) {
            static const QColor exeRed(255, 74, 70);
            option->palette.setColor(QPalette::Text, exeRed);
            option->palette.setColor(QPalette::HighlightedText, exeRed);
        }
    }
};

#endif // CUSTOMITEMDELEGATES_H
