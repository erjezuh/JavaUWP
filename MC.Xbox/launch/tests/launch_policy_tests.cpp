#include "../jvm_memory.h"
#include "../../common/directory_tree.h"

#include <cassert>
#include <iostream>
#include <set>
#include <vector>

static void TestMemoryBudget() {
    using namespace launchmemory;
    const auto shaders = ChooseBudget(5120, 256, true);
    assert(shaders.CanLaunch());
    assert(shaders.heapLimitMb == 2816);
    assert(shaders.initialHeapMb == 1024);
    assert(shaders.directMb == 512);
    assert(shaders.maxHeapMb + shaders.nativeReserveMb + 256 <= 5120);
    assert(ChooseBudget(5120, 0, true).heapLimitMb == 3072);
    assert(ChooseBudget(5120, 256, false).heapLimitMb == 3840);
    assert(ChooseBudget(16384, 0, true).heapLimitMb == 3072);
    assert(ChooseBudget(16384, 0, false).heapLimitMb == 4096);
    assert(ChooseBudget(0, 0, true).heapLimitMb == 3072); // WinRT unavailable
    assert(ChooseBudget(0, 0, false).heapLimitMb == 4096);
    assert(!ChooseBudget(5120, 6000, true).CanLaunch()); // no unsigned underflow
    assert(!ChooseBudget(2048, 256, true).CanLaunch());
    assert(!ChooseBudget(2048 + 511, 0, true).CanLaunch());
    assert(ChooseBudget(2048 + 512, 0, true).CanLaunch());
    assert(!ChooseBudget(1, 0, false).CanLaunch());

    auto budget = shaders;
    assert(ApplyUserOption(budget, "-Xmx4G")); // stale override from previous build
    assert(budget.maxHeapMb == shaders.heapLimitMb);
    assert(ApplyUserOption(budget, "-XX:MaxHeapSize=8589934592"));
    assert(budget.maxHeapMb == shaders.heapLimitMb);
    assert(ApplyUserOption(budget, "-XX:InitialHeapSize=8G"));
    assert(budget.initialHeapMb == 1024);
    assert(ApplyUserOption(budget, "-XX:MaxRAMPercentage=99"));
    assert(ApplyUserOption(budget, "-XX:InitialRAMPercentage=99"));
    assert(ApplyUserOption(budget, "-XX:MinRAMPercentage=99"));
    assert(ApplyUserOption(budget, "-XX:SoftMaxHeapSize=8G"));
    assert(ApplyUserOption(budget, "-XX:MaxRAM=8G"));
    assert(ApplyUserOption(budget, "-XX:MaxDirectMemorySize=2G"));
    assert(budget.directMb == 512);
    assert(ApplyUserOption(budget, "-XX:MaxDirectMemorySize=0"));
    assert(budget.directMb == 512);
    assert(ApplyUserOption(budget, "-Xmx2g"));
    assert(budget.maxHeapMb == 2048);
    assert(ApplyUserOption(budget, "-Xms256M"));
    assert(budget.initialHeapMb == 256);
    assert(ApplyUserOption(budget, "-XX:MaxDirectMemorySize=128M"));
    assert(budget.directMb == 128);
    assert(ApplyUserOption(budget, "-Xmxgarbage"));
    assert(budget.maxHeapMb == 2048);
    assert(ApplyUserOption(budget, "-Xmx18446744073709551616G"));
    assert(budget.maxHeapMb == 2048);
    assert(!ApplyUserOption(budget, "-XX:MaxGCPauseMillis=50"));
    assert(!ApplyUserOption(budget, "-Djava.io.tmpdir=C:/LocalState/tmp"));
    assert(!ApplyUserOption(budget, "-XX:+UseStringDeduplication"));
    assert(ApplyUserOption(budget, "-Xms1G"));
    assert(ApplyUserOption(budget, "-Xmx512M"));
    Normalize(budget);
    assert(budget.initialHeapMb == budget.maxHeapMb);
    assert(budget.maxHeapMb == 512);
    assert(ApplyUserOption(budget, "-Xmx1M"));
    assert(budget.maxHeapMb == 512); // don't produce an unbootable one-MB heap

    unsigned long long size = 0;
    assert(ParseSizeMb("2G", size) && size == 2048);
    assert(ParseSizeMb("524288K", size) && size == 512);
    assert(ParseSizeMb("1073741824", size) && size == 1024);
    assert(ParseSizeMb("1t", size) && size == 1048576);
    assert(!ParseSizeMb("0", size));
    assert(!ParseSizeMb("", size));
    assert(!ParseSizeMb("G", size));
    assert(!ParseSizeMb("-1G", size));
    assert(!ParseSizeMb("2GB", size));
    assert(!ParseSizeMb("1.5G", size));
    assert(!ParseSizeMb("18446744073709551615G", size));

    // Sweep app limits/usage: the automatic budget never spends the native reserve.
    for (unsigned limit = 256; limit <= 16384; limit += 256) {
        for (unsigned used = 0; used <= limit + 512; used += 128) {
            for (bool shaderLoader : { false, true }) {
                const auto b = ChooseBudget(limit, used, shaderLoader);
                if (b.CanLaunch()) {
                    assert(b.maxHeapMb + b.nativeReserveMb + used <= limit);
                    assert(b.initialHeapMb <= b.maxHeapMb);
                    assert(b.maxHeapMb <= (shaderLoader ? 3072u : 4096u));
                }
            }
        }
    }
}

static void TestDirectoryCreation() {
    using directorytree::Result;
    const std::wstring local = L"C:\\Users\\denied-parent\\LocalState";
    std::set<std::wstring> directories{ local };
    std::vector<std::wstring> attempts;
    auto create = [&](const std::wstring& path) {
        attempts.push_back(path);
        if (directories.count(path)) return Result::Ready;
        // Ancestors of LocalState deliberately cannot be accessed.
        if (path.rfind(local + L"\\", 0) != 0) return Result::Failed;
        if (!directories.count(path.substr(0, path.find_last_of(L'\\')))) return Result::MissingParent;
        directories.insert(path);
        return Result::Ready;
    };
    const auto leaf = local + L"\\profile\\tmp\\jna";
    assert(directorytree::Ensure(leaf, create));
    assert(directories.count(leaf));
    assert(attempts.front() == leaf);
    for (const auto& attempt : attempts) assert(attempt.rfind(local, 0) == 0);
    assert(directorytree::Ensure(leaf + L"\\", create));
    assert(directorytree::Ensure(leaf, create)); // already exists
    assert(!directorytree::Ensure(L"", create));
    assert(!directorytree::Ensure(L"C:\\Windows\\blocked", create));

    // A denied target or a regular file must not be 'fixed' by walking its parents.
    int count = 0;
    assert(!directorytree::Ensure(L"C:\\file\\tmp", [&](const std::wstring&) {
        ++count;
        return Result::Failed;
    }));
    assert(count == 1);
    assert(!directorytree::Ensure(L"C:\\", [](const std::wstring&) { return Result::MissingParent; }));
    assert(!directorytree::Ensure(L"relative", [](const std::wstring&) { return Result::MissingParent; }));

    // Root handling preserves the slash (C:\ rather than the drive-relative C:).
    std::vector<std::wstring> rootAttempts;
    assert(directorytree::Ensure(L"C:\\tmp", [&](const std::wstring& path) {
        rootAttempts.push_back(path);
        return rootAttempts.size() == 1 ? Result::MissingParent : Result::Ready;
    }));
    assert(rootAttempts[1] == L"C:\\");
}

int main() {
    TestMemoryBudget();
    TestDirectoryCreation();
    std::cout << "LAUNCH_POLICY_TESTS_OK\n";
}
