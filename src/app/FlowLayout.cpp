#include "app/FlowLayout.h"

#include <QHBoxLayout>
#include <QStyle>
#include <QWidget>
#include <algorithm>

namespace qr
{

FlowLayout::FlowLayout(QWidget* parent, int hSpacing, int vSpacing)
    : QLayout(parent)
    , m_hSpacing(hSpacing)
    , m_vSpacing(vSpacing)
{
    setContentsMargins(0, 0, 0, 0);
}

FlowLayout::~FlowLayout()
{
    while(QLayoutItem* item = takeAt(0))
        delete item;
}

void FlowLayout::addItem(QLayoutItem* item)
{
    m_items << item;
    invalidate();
}

void FlowLayout::addLayout(QLayout* layout)
{
    addChildLayout(layout);
    addItem(layout);
}

int FlowLayout::count() const
{
    return int(m_items.size());
}

QLayoutItem* FlowLayout::itemAt(int index) const
{
    return m_items.value(index);
}

QLayoutItem* FlowLayout::takeAt(int index)
{
    if(index < 0 || index >= m_items.size())
        return nullptr;
    invalidate();
    return m_items.takeAt(index);
}

Qt::Orientations FlowLayout::expandingDirections() const
{
    for(const QLayoutItem* item : m_items)
        if(item->expandingDirections() & Qt::Horizontal)
            return Qt::Horizontal;
    return {};
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
    QSize size;
    for(const QLayoutItem* item : m_items)
        size = size.expandedTo(item->minimumSize());
    const QMargins m = contentsMargins();
    return size + QSize(m.left() + m.right(), m.top() + m.bottom());
}

QSize FlowLayout::sizeHint() const
{
    // Everything on one line.
    int w = 0;
    int h = 0;
    int n = 0;
    for(const QLayoutItem* item : m_items)
    {
        if(item->isEmpty())
            continue;
        w += item->sizeHint().width();
        h = std::max(h, item->sizeHint().height());
        ++n;
    }
    w += std::max(0, n - 1) * spacingFor(Qt::Horizontal);
    const QMargins m = contentsMargins();
    return {w + m.left() + m.right(), h + m.top() + m.bottom()};
}

void FlowLayout::setGeometry(const QRect& rect)
{
    QLayout::setGeometry(rect);
    doLayout(rect, true);
}

int FlowLayout::spacingFor(Qt::Orientation o) const
{
    const int explicitSpacing = o == Qt::Horizontal ? m_hSpacing : m_vSpacing;
    if(explicitSpacing >= 0)
        return explicitSpacing;
    if(const auto* w = qobject_cast<const QWidget*>(parent()))
        return w->style()->pixelMetric(o == Qt::Horizontal ? QStyle::PM_LayoutHorizontalSpacing
                                                           : QStyle::PM_LayoutVerticalSpacing,
                                       nullptr, w);
    if(const auto* l = qobject_cast<const QLayout*>(parent()))
        return std::max(0, l->spacing());
    return 6;
}

int FlowLayout::doLayout(const QRect& rect, bool apply) const
{
    const QRect area = rect.marginsRemoved(contentsMargins());
    const int hs = spacingFor(Qt::Horizontal);
    const int vs = spacingFor(Qt::Vertical);

    struct Placed
    {
        QLayoutItem* item;
        int width;
    };
    // Break into lines at preferred widths (an item wider than the area gets the area).
    QList<QList<Placed>> lines(1);
    int x = 0;
    for(QLayoutItem* item : m_items)
    {
        if(item->isEmpty())
            continue;
        const int w = std::max(item->minimumSize().width(), std::min(item->sizeHint().width(), area.width()));
        if(!lines.last().isEmpty() && x + w > area.width())
        {
            lines.append(QList<Placed>());
            x = 0;
        }
        lines.last().append({item, w});
        x += w + hs;
    }

    int y = area.top();
    for(QList<Placed>& line : lines)
    {
        if(line.isEmpty())
            continue;
        // Spare width goes to the items that want to grow.
        int used = (int(line.size()) - 1) * hs;
        int growing = 0;
        for(const Placed& p : line)
        {
            used += p.width;
            growing += (p.item->expandingDirections() & Qt::Horizontal) ? 1 : 0;
        }
        const int spare = std::max(0, area.width() - used);
        for(Placed& p : line)
            if(growing && (p.item->expandingDirections() & Qt::Horizontal))
                p.width = std::min(p.width + spare / growing, std::max(p.width, p.item->maximumSize().width()));

        int lineHeight = 0;
        QList<int> heights;
        for(const Placed& p : line)
        {
            const int h = p.item->hasHeightForWidth() ? p.item->heightForWidth(p.width) : p.item->sizeHint().height();
            heights << h;
            lineHeight = std::max(lineHeight, h);
        }
        if(apply)
        {
            int lx = area.left();
            for(int i = 0; i < line.size(); ++i)
            {
                line[i].item->setGeometry(QRect(lx, y + (lineHeight - heights[i]) / 2, line[i].width, heights[i]));
                lx += line[i].width + hs;
            }
        }
        y += lineHeight + vs;
    }
    const int contentHeight = std::max(0, y - vs - area.top());
    return contentHeight + contentsMargins().top() + contentsMargins().bottom();
}

QLayout* group(std::initializer_list<QWidget*> widgets, int spacing)
{
    auto* row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(spacing);
    for(QWidget* w : widgets)
        row->addWidget(w);
    return row;
}

} // namespace qr
