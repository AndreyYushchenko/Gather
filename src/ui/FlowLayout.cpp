#include "FlowLayout.h"

#include <QWidget>

FlowLayout::FlowLayout(Mode mode, int spacing, int minItemWidth, QWidget *parent)
    : QLayout(parent)
    , m_mode(mode)
    , m_spacing(spacing)
    , m_minItemWidth(minItemWidth)
{
    setContentsMargins(0, 0, 0, 0);
}

FlowLayout::~FlowLayout()
{
    while (QLayoutItem *item = takeAt(0))
        delete item;
}

void FlowLayout::addItem(QLayoutItem *item)
{
    m_items.append(item);
}

int FlowLayout::count() const
{
    return m_items.size();
}

QLayoutItem *FlowLayout::itemAt(int index) const
{
    return m_items.value(index);
}

QLayoutItem *FlowLayout::takeAt(int index)
{
    return index >= 0 && index < m_items.size() ? m_items.takeAt(index) : nullptr;
}

Qt::Orientations FlowLayout::expandingDirections() const
{
    return Qt::Horizontal;
}

bool FlowLayout::hasHeightForWidth() const
{
    return true;
}

int FlowLayout::heightForWidth(int width) const
{
    return doLayout(QRect(0, 0, width, 0), false);
}

QSize FlowLayout::minimumSize() const
{
    // Only the narrowest single item: the real height comes from
    // heightForWidth(). Reporting the fully wrapped (one column) height here
    // made parents reserve that much space at every width, leaving a large
    // blank area under the content.
    int width = 0;
    int height = 0;
    for (QLayoutItem *item : m_items) {
        const int itemWidth = m_mode == Mode::EqualColumns ? m_minItemWidth : item->minimumSize().width();
        width = qMax(width, itemWidth);
        height = qMax(height, item->minimumSize().height());
    }
    const QMargins margins = contentsMargins();
    return QSize(width + margins.left() + margins.right(), height + margins.top() + margins.bottom());
}

QSize FlowLayout::sizeHint() const
{
    if (m_mode == Mode::EqualColumns)
        return minimumSize();
    // Natural: everything on one line is what this layout would like; it
    // only wraps when it gets less than that.
    int width = 0;
    int height = 0;
    int visible = 0;
    for (QLayoutItem *item : m_items) {
        if (item->isEmpty())
            continue;
        width += item->sizeHint().width();
        height = qMax(height, item->sizeHint().height());
        ++visible;
    }
    const QMargins margins = contentsMargins();
    return QSize(width + m_spacing * qMax(0, visible - 1) + margins.left() + margins.right(),
                 height + margins.top() + margins.bottom());
}

void FlowLayout::setGeometry(const QRect &rect)
{
    QLayout::setGeometry(rect);
    doLayout(rect, true);
}

int FlowLayout::itemHeight(QLayoutItem *item, int width) const
{
    return item->hasHeightForWidth() ? item->heightForWidth(width) : item->sizeHint().height();
}

int FlowLayout::doLayout(const QRect &rect, bool apply) const
{
    const QRect area = rect.marginsRemoved(contentsMargins());
    QList<QLayoutItem *> visible;
    for (QLayoutItem *item : m_items) {
        if (!item->isEmpty())
            visible << item;
    }
    if (visible.isEmpty())
        return 0;

    // Split into rows, each row a list of (item, width).
    using Row = QList<QPair<QLayoutItem *, int>>;
    QList<Row> rows;
    if (m_mode == Mode::EqualColumns) {
        const int fit = qMax(1, (area.width() + m_spacing) / qMax(1, m_minItemWidth + m_spacing));
        const int columns = m_widthOnly ? fit : qMin(fit, int(visible.size()));
        const int itemWidth = qMax(0, (area.width() - m_spacing * (columns - 1)) / columns);
        for (int i = 0; i < visible.size(); ++i) {
            if (i % columns == 0)
                rows.append(Row());
            rows.last().append(qMakePair(visible.at(i), itemWidth));
        }
    } else {
        int x = 0;
        rows.append(Row());
        for (QLayoutItem *item : std::as_const(visible)) {
            const int itemWidth = qMin(area.width(), item->sizeHint().width());
            if (!rows.last().isEmpty() && x + itemWidth > area.width()) {
                rows.append(Row());
                x = 0;
            }
            rows.last().append(qMakePair(item, itemWidth));
            x += itemWidth + m_spacing;
        }
    }

    int y = area.y();
    for (const auto &row : std::as_const(rows)) {
        int rowHeight = 0;
        for (const auto &entry : row)
            rowHeight = qMax(rowHeight, itemHeight(entry.first, entry.second));
        if (apply) {
            int gap = m_spacing;
            if (m_spaceBetween && m_mode == Mode::Natural && row.size() > 1) {
                int used = 0;
                for (const auto &entry : row)
                    used += entry.second;
                gap = qMax(m_spacing, (area.width() - used) / int(row.size() - 1));
            }
            int x = area.x();
            // A group that wrapped onto its own row keeps its space_between
            // side: flush right.
            if (m_spaceBetween && m_mode == Mode::Natural && row.size() == 1 && &row != &rows.first())
                x = area.right() + 1 - row.first().second;
            for (const auto &entry : row) {
                const int height = m_stretchRows ? rowHeight : itemHeight(entry.first, entry.second);
                const int top = m_centerItems ? y + (rowHeight - height) / 2 : y;
                entry.first->setGeometry(QRect(x, top, entry.second, height));
                x += entry.second + gap;
            }
        }
        y += rowHeight + m_spacing;
    }
    const QMargins margins = contentsMargins();
    return y - m_spacing - area.y() + margins.top() + margins.bottom();
}
