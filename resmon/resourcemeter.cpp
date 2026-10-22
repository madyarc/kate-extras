#include "resourcemeter.h"
#include "resourcesampler.h"

#include <KLocalizedString>

#include <QLocale>
#include <QPainter>
#include <QPainterPath>

namespace
{
constexpr int s_barWidth = 90;
constexpr int s_margin = 4;

QColor usageColor(double fraction)
{
    if (fraction >= 0.9) {
        return QColor(0xe5, 0x39, 0x35);
    }
    if (fraction >= 0.7) {
        return QColor(0xfb, 0x8c, 0x00);
    }
    return QColor(0x43, 0xa0, 0x47);
}

double fractionOf(qint64 used, qint64 total)
{
    return total > 0 ? qBound(0.0, double(used) / double(total), 1.0) : 0.0;
}

QString formatSize(qint64 bytes)
{
    return QLocale().formattedDataSize(bytes, 1, QLocale::DataSizeTraditionalFormat);
}

QString usageRow(const QString &label, qint64 used, qint64 total)
{
    return QStringLiteral("<tr><td><b>%1</b></td><td align=\"right\">%2 / %3</td><td align=\"right\">&nbsp;%4%</td></tr>")
        .arg(label, formatSize(used), formatSize(total))
        .arg(qRound(fractionOf(used, total) * 100));
}
}

ResourceMeter::ResourceMeter(ResourceSampler *sampler, QWidget *parent)
    : QWidget(parent)
    , m_sampler(sampler)
{
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    connect(m_sampler, &ResourceSampler::updated, this, [this]() {
        updateToolTip();
        update();
    });
    updateToolTip();
}

QFont ResourceMeter::meterFont() const
{
    QFont small = font();
    small.setPointSizeF(small.pointSizeF() * 0.8);
    return small;
}

QSize ResourceMeter::sizeHint() const
{
    const QFontMetrics metrics(meterFont());
    const int labelWidth = metrics.horizontalAdvance(QStringLiteral("VRAM")) + s_margin;
    return QSize(labelWidth + s_barWidth + 3 * s_margin, 2 * metrics.height() + 2);
}

void ResourceMeter::paintEvent(QPaintEvent *)
{
    const ResourceSnapshot snapshot = m_sampler->snapshot();

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setFont(meterFont());

    const QFontMetrics metrics(painter.font());
    const int labelWidth = metrics.horizontalAdvance(QStringLiteral("VRAM")) + s_margin;
    const int rowHeight = height() / 2;

    struct Row {
        QString label;
        double fraction;
        bool active;
    };
    const bool gpuActive = snapshot.gpuState == ResourceSnapshot::GpuState::Ok;
    const Row rows[] = {
        {QStringLiteral("RAM"), fractionOf(snapshot.ramUsed, snapshot.ramTotal), true},
        {QStringLiteral("VRAM"), gpuActive ? fractionOf(snapshot.vramUsed, snapshot.vramTotal) : 0.0, gpuActive},
    };

    QColor trackColor = palette().color(QPalette::WindowText);
    trackColor.setAlphaF(0.15);

    for (int i = 0; i < 2; ++i) {
        const Row &row = rows[i];
        const QRect rowRect(s_margin, i * rowHeight, width() - 2 * s_margin, rowHeight);

        painter.setPen(palette().color(row.active ? QPalette::Active : QPalette::Disabled, QPalette::WindowText));
        painter.drawText(QRect(rowRect.left(), rowRect.top(), labelWidth, rowRect.height()), Qt::AlignVCenter | Qt::AlignLeft, row.label);

        const int barHeight = qMax(4, rowHeight / 2);
        const QRectF track(rowRect.left() + labelWidth, rowRect.center().y() - barHeight / 2.0 + 1, s_barWidth, barHeight);
        const qreal radius = barHeight / 2.0;

        QPainterPath trackPath;
        trackPath.addRoundedRect(track, radius, radius);
        painter.fillPath(trackPath, trackColor);

        if (row.active && row.fraction > 0) {
            painter.save();
            painter.setClipPath(trackPath);
            painter.fillRect(QRectF(track.left(), track.top(), track.width() * row.fraction, track.height()), usageColor(row.fraction));
            painter.restore();
        }
    }
}

void ResourceMeter::updateToolTip()
{
    const ResourceSnapshot snapshot = m_sampler->snapshot();

    QString html = QStringLiteral("<table cellspacing=\"2\">");
    html += usageRow(i18n("RAM"), snapshot.ramUsed, snapshot.ramTotal);
    if (snapshot.swapTotal > 0) {
        html += usageRow(i18n("Swap"), snapshot.swapUsed, snapshot.swapTotal);
    }
    if (snapshot.gpuState == ResourceSnapshot::GpuState::Ok) {
        html += usageRow(i18n("VRAM"), snapshot.vramUsed, snapshot.vramTotal);
    }
    html += usageRow(i18n("Disk"), snapshot.diskUsed, snapshot.diskTotal);
    html += QStringLiteral("</table>");

    switch (snapshot.gpuState) {
    case ResourceSnapshot::GpuState::Ok: {
        QStringList details;
        if (snapshot.gpuUtilization >= 0) {
            details << i18n("%1% busy", snapshot.gpuUtilization);
        }
        if (snapshot.gpuTemperature >= 0) {
            details << i18n("%1 °C", snapshot.gpuTemperature);
        }
        html += QStringLiteral("<br/>%1").arg(snapshot.gpuName.toHtmlEscaped());
        if (!details.isEmpty()) {
            html += QStringLiteral(" · %1").arg(details.join(QStringLiteral(" · ")));
        }
        break;
    }
    case ResourceSnapshot::GpuState::Asleep:
        html += QStringLiteral("<br/>%1").arg(i18n("Discrete GPU is powered down (not polled, to save battery)."));
        break;
    case ResourceSnapshot::GpuState::Error:
        html += QStringLiteral("<br/>%1").arg(i18n("Could not read GPU memory."));
        break;
    case ResourceSnapshot::GpuState::None:
        html += QStringLiteral("<br/>%1").arg(i18n("No discrete GPU with dedicated VRAM found."));
        break;
    }

    setToolTip(html);
}
