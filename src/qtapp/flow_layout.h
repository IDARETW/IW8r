// IW8 Radiant Suite - a layout that places its widgets left to right and starts a new line when the next one does not
// fit (the side section's panels are narrow: options wrap instead of widening the panel). Owner 2026-11-15: "sidebar
// utilities' menus ... need way too much horizontal space".
#pragma once

#include <QLayout>
#include <QList>

namespace iw8r::qt {

class FlowLayout : public QLayout {
public:
    explicit FlowLayout(QWidget* parent = nullptr, int hSpacing = 6, int vSpacing = 4);
    ~FlowLayout() override;

    void addItem(QLayoutItem* item) override;
    int count() const override { return int(items_.size()); }
    QLayoutItem* itemAt(int i) const override { return i >= 0 && i < items_.size() ? items_[i] : nullptr; }
    QLayoutItem* takeAt(int i) override { return i >= 0 && i < items_.size() ? items_.takeAt(i) : nullptr; }
    Qt::Orientations expandingDirections() const override { return {}; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override { return arrange(QRect(0, 0, width, 0), false); }
    QSize minimumSize() const override;
    QSize sizeHint() const override { return minimumSize(); }
    void setGeometry(const QRect& r) override {
        QLayout::setGeometry(r);
        arrange(r, true);
    }

private:
    int arrange(const QRect& r, bool place) const;  // the height used
    QList<QLayoutItem*> items_;
    int h_, v_;
};

}  // namespace iw8r::qt
