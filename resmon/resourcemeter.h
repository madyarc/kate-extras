#pragma once

#include <QWidget>

class ResourceSampler;
struct ResourceSnapshot;

// Compact two-row RAM / VRAM bar for the menu bar corner, in the spirit of Colab's header meter
class ResourceMeter : public QWidget
{
    Q_OBJECT

public:
    static constexpr int defaultBarWidth = 90;

    explicit ResourceMeter(ResourceSampler *sampler, QWidget *parent = nullptr);

    QSize sizeHint() const override;

    // Shared with ResourceStatusItem, which draws the same meter into a tray icon
    static QSize meterSize(const QFont &font, int barWidth = defaultBarWidth);
    static void paintMeter(QPainter *painter,
                           const QRect &rect,
                           const ResourceSnapshot &snapshot,
                           const QColor &textColor,
                           const QColor &disabledTextColor,
                           int barWidth = defaultBarWidth);
    static QString toolTipHtml(const ResourceSnapshot &snapshot);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QFont meterFont() const;

    ResourceSampler *m_sampler;
};
