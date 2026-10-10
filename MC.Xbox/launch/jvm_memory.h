#pragma once

#include <algorithm>
#include <cctype>
#include <limits>
#include <string>

// Pure policy, kept independent of WinRT so the budget/override rules can be tested.
namespace launchmemory {

struct Budget {
    unsigned long long heapLimitMb;
    unsigned long long maxHeapMb;
    unsigned long long initialHeapMb;
    unsigned long long directLimitMb = 512;
    unsigned long long directMb = 512;
    unsigned long long nativeReserveMb;

    bool CanLaunch() const { return heapLimitMb >= 512; }
};

inline Budget ChooseBudget(unsigned long long limitMb, unsigned long long usedMb, bool shaderLoaderPresent) {
    const unsigned long long ceiling = shaderLoaderPresent ? 3072 : 4096;
    // Includes direct buffers, JVM code/metaspace/stacks, Mesa and shader render targets.
    // Existing host allocations are accounted for separately, before creating the JVM.
    const unsigned long long reserve = shaderLoaderPresent ? 2048 : 1024;
    unsigned long long heap = ceiling;
    if (limitMb != 0) {
        const auto available = limitMb > usedMb ? limitMb - usedMb : 0;
        heap = available > reserve ? (std::min)(ceiling, available - reserve) : 0;
        heap = (heap / 256) * 256;
    }
    return { heap, heap, (std::min)(1024ull, heap), 512, 512, reserve };
}

inline std::string Lower(std::string value) {
    for (char& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}

inline bool StartsWith(const std::string& value, const char* prefix) {
    return value.rfind(prefix, 0) == 0;
}

inline bool IsMemoryOption(const std::string& option) {
    const auto lower = Lower(option);
    return StartsWith(lower, "-xmx") || StartsWith(lower, "-xms") ||
        StartsWith(lower, "-xx:maxheapsize=") || StartsWith(lower, "-xx:initialheapsize=") ||
        StartsWith(lower, "-xx:minheapsize=") || StartsWith(lower, "-xx:softmaxheapsize=") ||
        StartsWith(lower, "-xx:maxdirectmemorysize=") || StartsWith(lower, "-xx:maxram") ||
        StartsWith(lower, "-xx:minram") || StartsWith(lower, "-xx:initialram") ||
        StartsWith(lower, "-xx:heap");
}

inline bool ParseSizeMb(std::string value, unsigned long long& mb) {
    if (value.empty()) return false;
    unsigned long long multiplier = 1;
    const char suffix = static_cast<char>(std::tolower(static_cast<unsigned char>(value.back())));
    if (suffix == 'k' || suffix == 'm' || suffix == 'g' || suffix == 't') {
        multiplier = suffix == 'k' ? 1024ull : suffix == 'm' ? 1024ull * 1024 :
            suffix == 'g' ? 1024ull * 1024 * 1024 : 1024ull * 1024 * 1024 * 1024;
        value.pop_back();
    }
    if (value.empty()) return false;
    unsigned long long number = 0;
    for (const char c : value) {
        if (c < '0' || c > '9') return false;
        const auto digit = static_cast<unsigned>(c - '0');
        if (number > ((std::numeric_limits<unsigned long long>::max)() - digit) / 10) return false;
        number = number * 10 + digit;
    }
    if (number > (std::numeric_limits<unsigned long long>::max)() / multiplier) return false;
    mb = number * multiplier / (1024ull * 1024);
    // Zero means unlimited for MaxDirectMemorySize, and is never a safe override here.
    return mb > 0;
}

// User options may reduce memory, but cannot consume the space reserved for Mesa.
// Consume aliases/percentage flags too: allowing one through would bypass the cap.
inline bool ApplyUserOption(Budget& budget, const std::string& option) {
    if (!IsMemoryOption(option)) return false;
    const auto lower = Lower(option);
    unsigned long long* setting = nullptr;
    unsigned long long cap = budget.heapLimitMb;
    std::string size;
    if (StartsWith(lower, "-xmx") || StartsWith(lower, "-xms")) {
        setting = StartsWith(lower, "-xmx") ? &budget.maxHeapMb : &budget.initialHeapMb;
        size = option.substr(4);
    } else {
        if (StartsWith(lower, "-xx:maxheapsize=")) setting = &budget.maxHeapMb;
        if (StartsWith(lower, "-xx:initialheapsize=") || StartsWith(lower, "-xx:minheapsize=")) {
            setting = &budget.initialHeapMb;
        }
        if (StartsWith(lower, "-xx:maxdirectmemorysize=")) {
            setting = &budget.directMb;
            cap = budget.directLimitMb;
        }
        if (setting) size = option.substr(option.find('=') + 1);
    }
    if (setting == &budget.initialHeapMb) cap = (std::min)(1024ull, cap);
    unsigned long long requested = 0;
    if (setting && ParseSizeMb(size, requested)) {
        if (setting == &budget.maxHeapMb) requested = (std::max)(512ull, requested);
        *setting = (std::min)(requested, cap);
    }
    return true;
}

inline void Normalize(Budget& budget) {
    budget.maxHeapMb = (std::min)(budget.maxHeapMb, budget.heapLimitMb);
    budget.initialHeapMb = (std::min)(budget.initialHeapMb, budget.maxHeapMb);
    budget.directMb = (std::min)(budget.directMb, budget.directLimitMb);
}

} // namespace launchmemory
