#include "resourcestatusitem.h"
#include "resourcemeter.h"
#include "resourcesampler.h"

#include <KLocalizedString>

#include <QGuiApplication>
#include <QLocale>
#include <QPainter>
#include <QPixmap>
#include <QStyleHints>
#include <QSystemTrayIcon>
#include <QToolTip>

namespace
{
// Narrower than the in-window meter: menu bar space is scarce, especially next to a notch
constexpr int s_barWidth = 48;
constexpr qreal s_scale = 2.0;

QString usageLine(const QString &label, qint64 used, qint64 total)
{
    const QLocale locale;
    return QStringLiteral("%1: %2 / %3")
        .arg(label,
             locale.formattedDataSize(used, 1, QLocale::DataSizeTraditionalFormat),
             locale.formattedDataSize(total, 1, QLocale::DataSizeTraditionalFormat));
}
}

ResourceStatusItem::ResourceStatusItem(ResourceSampler *sampler, QObject *parent)
    : QObject(parent)
    , m_sampler(sampler)
    , m_trayIcon(new QSystemTrayIcon(this))
{
    connect(m_sampler, &ResourceSampler::updated, this, &ResourceStatusItem::update);
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, &ResourceStatusItem::update);
    connect(m_trayIcon, &QSystemTrayIcon::activated, this, &ResourceStatusItem::showDetails);

    update();
    m_trayIcon->show();
}

ResourceStatusItem::~ResourceStatusItem() = default;

void ResourceStatusItem::update()
{
    const ResourceSnapshot snapshot = m_sampler->snapshot();

    QFont font;
    font.setPixelSize(9);
    const QSize size = ResourceMeter::meterSize(font, s_barWidth);

    QPixmap pixmap(size * s_scale);
    pixmap.setDevicePixelRatio(s_scale);
    pixmap.fill(Qt::transparent);

    // The menu bar follows the system appearance, not Kate's colour scheme
    const bool dark = QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
    const QColor text = dark ? Qt::white : Qt::black;
    QColor disabledText = text;
    disabledText.setAlphaF(0.4);

    QPainter painter(&pixmap);
    painter.setFont(font);
    ResourceMeter::paintMeter(&painter, QRect(QPoint(), size), snapshot, text, disabledText, s_barWidth);
    painter.end();
    m_trayIcon->setIcon(QIcon(pixmap));

    // Native menu bar tooltips are plain text
    QStringList lines{usageLine(i18n("RAM"), snapshot.ramUsed, snapshot.ramTotal)};
    if (snapshot.gpuState == ResourceSnapshot::GpuState::Ok) {
        lines << usageLine(i18n("VRAM"), snapshot.vramUsed, snapshot.vramTotal);
    }
    m_trayIcon->setToolTip(lines.join(QLatin1Char('\n')));
}

void ResourceStatusItem::showDetails()
{
    const QRect geometry = m_trayIcon->geometry();
    QToolTip::showText(geometry.isValid() ? geometry.bottomLeft() : QPoint(), ResourceMeter::toolTipHtml(m_sampler->snapshot()));
}
