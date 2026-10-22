#include "resourcesampler.h"

#include <QString>

#include <mach/mach.h>
#include <sys/sysctl.h>

#import <CoreFoundation/CoreFoundation.h>
#import <IOKit/IOKitLib.h>
#import <Metal/Metal.h>

namespace
{
qint64 sysctlInt64(const char *name)
{
    qint64 value = 0;
    size_t size = sizeof(value);
    return sysctlbyname(name, &value, &size, nullptr, 0) == 0 ? value : 0;
}

qint64 dictionaryInt64(CFDictionaryRef dictionary, const char *key)
{
    const CFStringRef cfKey = CFStringCreateWithCString(kCFAllocatorDefault, key, kCFStringEncodingUTF8);
    const auto number = static_cast<CFNumberRef>(CFDictionaryGetValue(dictionary, cfKey));
    CFRelease(cfKey);

    qint64 value = -1;
    if (!number || CFGetTypeID(number) != CFNumberGetTypeID() || !CFNumberGetValue(number, kCFNumberSInt64Type, &value)) {
        return -1;
    }
    return value;
}

// The first GPU that publishes performance statistics; reading these from the IOKit
// registry is cheap and never powers up a sleeping GPU
template<typename Function>
void withGpuStatistics(Function &&function)
{
    io_iterator_t iterator = IO_OBJECT_NULL;
    if (IOServiceGetMatchingServices(kIOMainPortDefault, IOServiceMatching("IOAccelerator"), &iterator) != KERN_SUCCESS) {
        return;
    }
    for (io_registry_entry_t entry = IOIteratorNext(iterator); entry; entry = IOIteratorNext(iterator)) {
        const auto statistics =
            static_cast<CFDictionaryRef>(IORegistryEntryCreateCFProperty(entry, CFSTR("PerformanceStatistics"), kCFAllocatorDefault, 0));
        const auto cores = static_cast<CFNumberRef>(IORegistryEntryCreateCFProperty(entry, CFSTR("gpu-core-count"), kCFAllocatorDefault, 0));
        IOObjectRelease(entry);

        int coreCount = -1;
        if (cores) {
            CFNumberGetValue(cores, kCFNumberIntType, &coreCount);
            CFRelease(cores);
        }
        if (statistics) {
            const bool found = CFGetTypeID(statistics) == CFDictionaryGetTypeID();
            if (found) {
                function(statistics, coreCount);
            }
            CFRelease(statistics);
            if (found) {
                break;
            }
        }
    }
    IOObjectRelease(iterator);
}
}

void ResourceSampler::detectGpu()
{
    // MTLCopyAllDevices, unlike MTLCreateSystemDefaultDevice, does not switch a
    // dual-GPU Intel Mac over to its discrete GPU
    @autoreleasepool {
        NSArray<id<MTLDevice>> *devices = MTLCopyAllDevices();
        for (id<MTLDevice> device in devices) {
            if (device.hasUnifiedMemory) {
                m_snapshot.unifiedMemory = true;
                m_snapshot.gpuName = QString::fromNSString(device.name);
                m_gpuWorkingSetLimit = qint64(device.recommendedMaxWorkingSetSize);
                break;
            }
        }
        [devices release];
    }

    if (m_snapshot.unifiedMemory) {
        withGpuStatistics([this](CFDictionaryRef, int coreCount) {
            if (coreCount > 0) {
                m_snapshot.gpuName += QStringLiteral(" (%1-core GPU)").arg(coreCount);
            }
        });
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
    m_snapshot.gpuState = ResourceSnapshot::GpuState::None;

    withGpuStatistics([this](CFDictionaryRef statistics, int) {
        m_snapshot.gpuUtilization = int(dictionaryInt64(statistics, "Device Utilization %"));

        if (m_snapshot.unifiedMemory) {
            // Apple silicon: system memory currently in use by the GPU, against the
            // share of RAM that Metal lets the GPU keep resident
            const qint64 inUse = dictionaryInt64(statistics, "In use system memory");
            if (inUse >= 0 && m_gpuWorkingSetLimit > 0) {
                m_snapshot.gpuState = ResourceSnapshot::GpuState::Ok;
                m_snapshot.vramUsed = inUse;
                m_snapshot.vramTotal = m_gpuWorkingSetLimit;
            }
            return;
        }

        // Discrete AMD GPUs in Intel Macs report dedicated VRAM
        const qint64 used = dictionaryInt64(statistics, "vramUsedBytes");
        const qint64 free = dictionaryInt64(statistics, "vramFreeBytes");
        if (used >= 0 && free >= 0) {
            m_snapshot.gpuState = ResourceSnapshot::GpuState::Ok;
            m_snapshot.gpuName = QStringLiteral("GPU");
            m_snapshot.vramUsed = used;
            m_snapshot.vramTotal = used + free;
        }
    });
}
