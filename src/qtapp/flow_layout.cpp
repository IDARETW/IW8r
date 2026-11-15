#include "flow_layout.h"

#include <QWidget>

#include <algorithm>

namespace iw8r::qt {

FlowLayout::FlowLayout(QWidget* parent, int hSpacing, int vSpacing) : QLayout(parent), h_(hSpacing), v_(vSpacing) {
    setContentsMargins(0, 0, 0, 0);
}

FlowLayout::~FlowLayout() {
    while (QLayoutItem* it = takeAt(0)) delete it;
}

void FlowLayout::addItem(QLayoutItem* item) { items_.append(item); }

QSize FlowLayout::minimumSize() const {
    // As narrow as the widest single item: everything else wraps.
    QSize s;
    for (const QLayoutItem* it : items_)
        if (!it->isEmpty()) s = s.expandedTo(it->minimumSize());
    const QMargins m = contentsMargins();
    return s + QSize(m.left() + m.right(), m.top() + m.bottom());
}

int FlowLayout::arrange(const QRect& r, bool place) const {
    const QMargins m = contentsMargins();
    const QRect area = r.adjusted(m.left(), m.top(), -m.right(), -m.bottom());
    int x = area.x(), y = area.y(), lineH = 0;
    for (QLayoutItem* it : items_) {
        if (it->isEmpty()) continue;  // hidden widgets take no room
        const QSize s = it->sizeHint().boundedTo(QSize(std::max(1, area.width()), 1 << 20));
        if (x > area.x() && x + s.width() > area.right() + 1) {
            x = area.x();
            y += lineH + v_;
            lineH = 0;
        }
        if (place) it->setGeometry(QRect(QPoint(x, y), s));
        x += s.width() + h_;
        lineH = std::max(lineH, s.height());
    }
    return y + lineH - r.y() + m.bottom();
}

}  // namespace iw8r::qt
