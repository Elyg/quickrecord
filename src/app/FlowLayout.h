#pragma once

#include <QLayout>
#include <QList>
#include <initializer_list>

namespace qr
{

// Lays items out left to right and wraps them onto further lines when the width runs out,
// so control rows still fit in narrow (e.g. tiled) windows. A nested layout added with
// addLayout() stays together on one line; items that expand horizontally (a line edit)
// share the spare width of their line.
class FlowLayout : public QLayout
{
public:
    explicit FlowLayout(QWidget* parent = nullptr, int hSpacing = -1, int vSpacing = -1);
    ~FlowLayout() override;

    void addItem(QLayoutItem* item) override;
    void addLayout(QLayout* layout);
    int count() const override;
    QLayoutItem* itemAt(int index) const override;
    QLayoutItem* takeAt(int index) override;

    Qt::Orientations expandingDirections() const override;
    bool hasHeightForWidth() const override;
    int heightForWidth(int width) const override;
    QSize minimumSize() const override;
    QSize sizeHint() const override;
    void setGeometry(const QRect& rect) override;

private:
    int doLayout(const QRect& rect, bool apply) const;
    int spacingFor(Qt::Orientation o) const;

    QList<QLayoutItem*> m_items;
    int m_hSpacing;
    int m_vSpacing;
};

// Widgets that a FlowLayout keeps together on one line (add it with addLayout()).
QLayout* group(std::initializer_list<QWidget*> widgets, int spacing = 4);

} // namespace qr
