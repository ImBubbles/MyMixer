#include "FlowLayout.h"

FlowLayout::FlowLayout(QWidget* parent, const int margin, const int spacing, const int maxItemsPerRow)
    : QLayout(parent), maxItemsPerRow_(maxItemsPerRow) {
    setContentsMargins(margin, margin, margin, margin);
    setSpacing(spacing);
}

FlowLayout::~FlowLayout() {
    while (QLayoutItem* item = takeAt(0)) {
        delete item;
    }
}

void FlowLayout::addItem(QLayoutItem* item) {
    items_.append(item);
}

int FlowLayout::count() const {
    return items_.size();
}

QLayoutItem* FlowLayout::itemAt(const int index) const {
    return items_.value(index, nullptr);
}

QLayoutItem* FlowLayout::takeAt(const int index) {
    return index >= 0 && index < items_.size() ? items_.takeAt(index) : nullptr;
}

Qt::Orientations FlowLayout::expandingDirections() const {
    return Qt::Orientations();
}

bool FlowLayout::hasHeightForWidth() const {
    return true;
}

int FlowLayout::heightForWidth(const int width) const {
    return doLayout(QRect(0, 0, width, 0), true);
}

QSize FlowLayout::sizeHint() const {
    return minimumSize();
}

QSize FlowLayout::minimumSize() const {
    QSize size;
    for (const QLayoutItem* item : items_) {
        size = size.expandedTo(item->minimumSize());
    }
    const QMargins margins = contentsMargins();
    size += QSize(margins.left() + margins.right(), margins.top() + margins.bottom());
    return size;
}

void FlowLayout::setGeometry(const QRect& rect) {
    QLayout::setGeometry(rect);
    doLayout(rect, false);
}

int FlowLayout::doLayout(const QRect& rect, const bool testOnly) const {
    const QMargins margins = contentsMargins();
    const QRect effectiveRect = rect.adjusted(margins.left(), margins.top(), -margins.right(), -margins.bottom());
    int x = effectiveRect.x();
    int y = effectiveRect.y();
    int rowHeight = 0;
    int rowItems = 0;

    for (QLayoutItem* item : items_) {
        const int hSpace = spacing();
        const int vSpace = spacing();
        const QSize itemSize = item->sizeHint();
        const int nextX = x + itemSize.width() + hSpace;

        if ((nextX - hSpace > effectiveRect.right() + 1 ||
             (maxItemsPerRow_ > 0 && rowItems >= maxItemsPerRow_)) && rowHeight > 0) {
            x = effectiveRect.x();
            y += rowHeight + vSpace;
            rowHeight = 0;
            rowItems = 0;
        }

        if (!testOnly) {
            item->setGeometry(QRect(QPoint(x, y), itemSize));
        }
        x += itemSize.width() + hSpace;
        rowHeight = qMax(rowHeight, itemSize.height());
        ++rowItems;
    }

    return y + rowHeight + margins.bottom() - rect.y();
}
