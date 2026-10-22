#pragma once

#include <QWidget>

class ResourceSampler;

// Compact two-row RAM / VRAM bar for the menu bar corner, in the spirit of Colab's header meter
class ResourceMeter : public QWidget
{
    Q_OBJECT

public:
    explicit ResourceMeter(ResourceSampler *sampler, QWidget *parent = nullptr);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void updateToolTip();
    QFont meterFont() const;

    ResourceSampler *m_sampler;
};
