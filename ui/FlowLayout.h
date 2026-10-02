#pragma once

#include <QLayout>
#include <QList>
#include <QRect>
#include <QSize>

class FlowLayout final : public QLayout {
public:
    explicit FlowLayout(QWidget* parent = nullptr, int margin = -1, int spacing = -1, int maxItemsPerRow = 0);
    ~FlowLayout() override;

    void addItem(QLayoutItem* item) override;
    int count() const override;
    QLayoutItem* itemAt(int index) const override;
    QLayoutItem* takeAt(int index) override;
    Qt::Orientations expandingDirections() const override;
    bool hasHeightForWidth() const override;
    int heightForWidth(int width) const override;
    QSize sizeHint() const override;
    QSize minimumSize() const override;
    void setGeometry(const QRect& rect) override;

private:
    QList<QLayoutItem*> items_;
    int maxItemsPerRow_;
    int doLayout(const QRect& rect, bool testOnly) const;
};
