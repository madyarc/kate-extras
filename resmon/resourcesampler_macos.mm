#include "resourcesampler.h"

#include <QString>

#include <mach/mach.h>
#include <sys/sysctl.h>

#import <CoreFoundation/CoreFoundation.h>
#import <IOKit/IOKitLib.h>

namespace
{
qint64 sysctlInt64(const char *name)
{
    qint64 value = 0;
    size_t size = sizeof(value);
    return sysctlbyname(name, &value, &size, nullptr, 0) == 0 ? value : 0;
}

QString toQString(CFTypeRef value)
{
    if (!value || CFGetTypeID(value) != CFStringGetTypeID()) {
        return {};
    }
    return QString::fromCFString(static_cast<CFStringRef>(value));
}

// The GPU's name, core count and current load, read from the IOKit registry. That is
// cheap and never powers up a sleeping GPU.
struct GpuInfo {
    QString model;
    int cores = -1;
    int utilization = -1;
};

GpuInfo readGpu()
{
    GpuInfo info;
    io_iterator_t iterator = IO_OBJECT_NULL;
    if (IOServiceGetMatchingServices(kIOMainPortDefault, IOServiceMatching("IOAccelerator"), &iterator) != KERN_SUCCESS) {
        return info;
    }
    for (io_registry_entry_t entry = IOIteratorNext(iterator); entry; entry = IOIteratorNext(iterator)) {
        const auto statistics =
            static_cast<CFDictionaryRef>(IORegistryEntryCreateCFProperty(entry, CFSTR("PerformanceStatistics"), kCFAllocatorDefault, 0));
        if (!statistics) {
            IOObjectRelease(entry);
            continue;
        }

        if (CFGetTypeID(statistics) == CFDictionaryGetTypeID()) {
            const auto utilization = static_cast<CFNumberRef>(CFDictionaryGetValue(statistics, CFSTR("Device Utilization %")));
            if (utilization && CFGetTypeID(utilization) == CFNumberGetTypeID()) {
                CFNumberGetValue(utilization, kCFNumberIntType, &info.utilization);
            }
        }
        CFRelease(statistics);

        if (const CFTypeRef model = IORegistryEntryCreateCFProperty(entry, CFSTR("model"), kCFAllocatorDefault, 0)) {
            info.model = toQString(model);
            CFRelease(model);
        }
        if (const auto cores = static_cast<CFNumberRef>(IORegistryEntryCreateCFProperty(entry, CFSTR("gpu-core-count"), kCFAllocatorDefault, 0))) {
            if (CFGetTypeID(cores) == CFNumberGetTypeID()) {
                CFNumberGetValue(cores, kCFNumberIntType, &info.cores);
            }
            CFRelease(cores);
        }

        IOObjectRelease(entry);
        break;
    }
    IOObjectRelease(iterator);
    return info;
}
}

void ResourceSampler::detectGpu()
{
    const GpuInfo gpu = readGpu();
    m_snapshot.gpuName = gpu.model.isEmpty() ? QStringLiteral("GPU") : gpu.model;
    if (gpu.cores > 0) {
        m_snapshot.gpuName += QStringLiteral(" (%1-core GPU)").arg(gpu.cores);
    }
}

void ResourceSampler::sampleMemory()
{
    static const mach_port_t host = mach_host_self();

    vm_size_t pageSize = 0;
    vm_statistics64_data_t vm = {};
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    if (host_page_size(host, &pageSize) != KERN_SUCCESS
        || host_statistics64(host, HOST_VM_INFO64, reinterpret_cast<host_info64_t>(&vm), &count) != KERN_SUCCESS) {
        return;
    }

    // Activity Monitor's "Memory Used": app memory + wired + compressed. Cached files
    // and purgeable memory can be reclaimed, so like `free` on Linux they are not "used"
    const qint64 appPages = qint64(vm.internal_page_count) - qint64(vm.purgeable_count);
    m_snapshot.ramTotal = sysctlInt64("hw.memsize");
    m_snapshot.ramUsed = (appPages + qint64(vm.wire_count) + qint64(vm.compressor_page_count)) * qint64(pageSize);

    xsw_usage swap = {};
    size_t size = sizeof(swap);
    if (sysctlbyname("vm.swapusage", &swap, &size, nullptr, 0) == 0) {
        m_snapshot.swapTotal = qint64(swap.xsu_total);
        m_snapshot.swapUsed = qint64(swap.xsu_used);
    }
}

void ResourceSampler::sampleGpu()
{
    // Macs have no dedicated VRAM worth showing (Apple silicon shares system memory),
    // so only the GPU's load is reported and the VRAM fields stay empty
    const GpuInfo gpu = readGpu();
    m_snapshot.gpuUtilization = gpu.utilization;
    m_snapshot.gpuState = gpu.utilization >= 0 ? ResourceSnapshot::GpuState::Ok : ResourceSnapshot::GpuState::None;
}
