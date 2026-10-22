#include "resourcestatusitem.h"
#include "resourcesampler.h"

#include <KLocalizedString>

#include <QLocale>

#import <AppKit/AppKit.h>

namespace
{
// Menu bar text is about 13 pt, so a bar this size sits level with the label
constexpr CGFloat s_barWidth = 40;
constexpr CGFloat s_barHeight = 7;

enum MenuItem {
    MemoryItem,
    SwapItem,
    StorageItem,
    GpuSeparator,
    GpuItem,
};

double fractionOf(qint64 used, qint64 total)
{
    return total > 0 ? qBound(0.0, double(used) / double(total), 1.0) : 0.0;
}

// Same thresholds and colours as the in-window meter
NSColor *usageColor(double fraction)
{
    if (fraction >= 0.9) {
        return [NSColor colorWithSRGBRed:0xe5 / 255.0 green:0x39 / 255.0 blue:0x35 / 255.0 alpha:1];
    }
    if (fraction >= 0.7) {
        return [NSColor colorWithSRGBRed:0xfb / 255.0 green:0x8c / 255.0 blue:0x00 / 255.0 alpha:1];
    }
    return [NSColor colorWithSRGBRed:0x43 / 255.0 green:0xa0 / 255.0 blue:0x47 / 255.0 alpha:1];
}

// Drawn when AppKit renders it, so it is sharp on every display
NSImage *barImage(double fraction)
{
    return [NSImage imageWithSize:NSMakeSize(s_barWidth, s_barHeight)
                          flipped:NO
                   drawingHandler:^BOOL(NSRect rect) {
                       NSBezierPath *track = [NSBezierPath bezierPathWithRoundedRect:rect xRadius:s_barHeight / 2 yRadius:s_barHeight / 2];
                       // Mid grey reads on both light and dark menu bars
                       [[NSColor colorWithWhite:0.5 alpha:0.4] setFill];
                       [track fill];
                       [track addClip];
                       [usageColor(fraction) setFill];
                       NSRectFill(NSMakeRect(rect.origin.x, rect.origin.y, rect.size.width * fraction, rect.size.height));
                       return YES;
                   }];
}

QString usageText(const QString &label, qint64 used, qint64 total)
{
    const QLocale locale;
    return i18nc("@info menu bar resource usage, e.g. Memory: 9.7 GB of 24.0 GB (40%)",
                 "%1: %2 of %3 (%4%)",
                 label,
                 locale.formattedDataSize(used, 1, QLocale::DataSizeTraditionalFormat),
                 locale.formattedDataSize(total, 1, QLocale::DataSizeTraditionalFormat),
                 qRound(fractionOf(used, total) * 100));
}

NSStatusItem *statusItemOf(void *item)
{
    return static_cast<NSStatusItem *>(item);
}
}

ResourceStatusItem::ResourceStatusItem(ResourceSampler *sampler, QObject *parent)
    : QObject(parent)
    , m_sampler(sampler)
{
    NSStatusItem *item = [[NSStatusBar.systemStatusBar statusItemWithLength:NSVariableStatusItemLength] retain];
    item.button.title = i18n("Memory").toNSString();
    item.button.imagePosition = NSImageTrailing;

    // Plain informational rows: clicking one only closes the menu
    NSMenu *menu = [[[NSMenu alloc] init] autorelease];
    menu.autoenablesItems = NO;
    for (int i = MemoryItem; i <= GpuItem; ++i) {
        [menu addItem:i == GpuSeparator ? [NSMenuItem separatorItem] : [[[NSMenuItem alloc] init] autorelease]];
    }
    item.menu = menu;

    m_statusItem = item;

    connect(m_sampler, &ResourceSampler::updated, this, &ResourceStatusItem::update);
    update();
}

ResourceStatusItem::~ResourceStatusItem()
{
    NSStatusItem *item = statusItemOf(m_statusItem);
    [NSStatusBar.systemStatusBar removeStatusItem:item];
    [item release];
}

void ResourceStatusItem::update()
{
    const ResourceSnapshot snapshot = m_sampler->snapshot();
    NSStatusItem *item = statusItemOf(m_statusItem);

    @autoreleasepool {
        const QString memory = usageText(i18n("Memory"), snapshot.ramUsed, snapshot.ramTotal);
        item.button.image = barImage(fractionOf(snapshot.ramUsed, snapshot.ramTotal));
        item.button.toolTip = memory.toNSString();

        NSMenu *menu = item.menu;
        [menu itemAtIndex:MemoryItem].title = memory.toNSString();

        NSMenuItem *swap = [menu itemAtIndex:SwapItem];
        swap.hidden = snapshot.swapTotal <= 0;
        swap.title = usageText(i18n("Swap"), snapshot.swapUsed, snapshot.swapTotal).toNSString();

        [menu itemAtIndex:StorageItem].title = usageText(i18n("Storage"), snapshot.diskUsed, snapshot.diskTotal).toNSString();

        const bool hasGpu = snapshot.gpuState == ResourceSnapshot::GpuState::Ok;
        [menu itemAtIndex:GpuSeparator].hidden = !hasGpu;
        NSMenuItem *gpu = [menu itemAtIndex:GpuItem];
        gpu.hidden = !hasGpu;
        gpu.title = i18nc("@info GPU name and load", "%1 · %2% busy", snapshot.gpuName, snapshot.gpuUtilization).toNSString();
    }
}
