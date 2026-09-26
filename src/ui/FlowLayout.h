#pragma once

#include <QLayout>
#include <QList>

// Lays items out left to right and wraps onto new rows when they don't fit.
//
// EqualColumns: every item gets the same width (design.pen's
// `width: fill_container` siblings), with as many columns as fit at
// `minItemWidth` — so a row of cards shares the width like an HBox when
// there's room and wraps instead of crushing its contents when there isn't.
// Natural: items keep their own sizeHint width (chip/button rows).
class FlowLayout : public QLayout {
public:
    enum class Mode { EqualColumns, Natural };

    explicit FlowLayout(Mode mode, int spacing, int minItemWidth = 0, QWidget *parent = nullptr);
    ~FlowLayout() override;

    // Rows are top-aligned; by default each item keeps its own height (like
    // design.pen's hug-content cards). Stretch makes every item in a row as
    // tall as the tallest.
    void setStretchRows(bool stretch) { m_stretchRows = stretch; }
    // Natural mode: spread a row's items across the full width (first item
    // flush left, last flush right), like design.pen's space_between.
    void setSpaceBetween(bool spread) { m_spaceBetween = spread; }
    // Vertically centre items within their row (a toggle next to a taller
    // field) instead of top-aligning them.
    void setCenterItems(bool center) { m_centerItems = center; }
    // EqualColumns: by default a short row stretches its few items across
    // the width (cards of a section). For galleries, keep the column count
    // (and so the tile size) independent of how many items there are.
    void setColumnsFromWidthOnly(bool widthOnly) { m_widthOnly = widthOnly; }

    void addItem(QLayoutItem *item) override;
    int count() const override;
    QLayoutItem *itemAt(int index) const override;
    QLayoutItem *takeAt(int index) override;
    Qt::Orientations expandingDirections() const override;
    bool hasHeightForWidth() const override;
    int heightForWidth(int width) const override;
    QSize minimumSize() const override;
    QSize sizeHint() const override;
    void setGeometry(const QRect &rect) override;

private:
    int doLayout(const QRect &rect, bool apply) const;
    int itemHeight(QLayoutItem *item, int width) const;

    QList<QLayoutItem *> m_items;
    Mode m_mode;
    int m_spacing;
    int m_minItemWidth;
    bool m_stretchRows = false;
    bool m_spaceBetween = false;
    bool m_centerItems = false;
    bool m_widthOnly = false;
};
