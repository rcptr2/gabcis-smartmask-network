#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "SmartMaskRegistry.h"

#include <atomic>
#include <cmath>
#include <random>
#include <set>
#include <thread>
#include <vector>

using namespace smartmask;

namespace
{
    // The registry is a process-wide singleton, so each test releases the
    // slots it took before returning -- otherwise test cases would see each
    // other's leftover state.
    struct ScopedSlot
    {
        int index;
        ScopedSlot() : index (SmartMaskRegistry::getInstance().registerTrack()) {}
        ~ScopedSlot()
        {
            if (index >= 0)
                SmartMaskRegistry::getInstance().unregisterTrack (index);
        }
    };
}

TEST_CASE ("registerTrack claims unique slots under concurrent load", "[registry][concurrency]")
{
    auto& registry = SmartMaskRegistry::getInstance();

    constexpr int numThreads = 16;
    std::array<int, numThreads> results {};
    results.fill (-2);

    std::vector<std::thread> threads;
    threads.reserve (numThreads);
    for (int t = 0; t < numThreads; ++t)
        threads.emplace_back ([&, t] { results[(size_t) t] = registry.registerTrack(); });

    for (auto& th : threads)
        th.join();

    std::set<int> unique (results.begin(), results.end());
    CHECK (unique.size() == (size_t) numThreads);
    for (auto idx : results)
        CHECK (idx >= 0);

    for (auto idx : results)
        registry.unregisterTrack (idx);
}

TEST_CASE ("getGlobalMask only sees strictly higher-priority tracks", "[registry][logic]")
{
    auto& registry = SmartMaskRegistry::getInstance();

    ScopedSlot vocal; // will be priority 1 (highest)
    ScopedSlot pad;   // will be priority 5 (lower)
    REQUIRE (vocal.index >= 0);
    REQUIRE (pad.index >= 0);

    registry.setPriority (vocal.index, 1);
    registry.setPriority (pad.index, 5);

    std::array<float, kNumBarkBands> vocalBands {};
    vocalBands[5] = 0.9f;
    registry.updateSpectrum (vocal.index, vocalBands);

    std::array<float, kNumBarkBands> padBands {};
    padBands[10] = 0.4f;
    registry.updateSpectrum (pad.index, padBands);

    // The lower-priority pad should see the vocal's energy competing in band 5.
    std::array<float, kNumBarkBands> maskForPad {};
    registry.getGlobalMask (pad.index, 5, maskForPad);
    CHECK (maskForPad[5] == Catch::Approx (0.9f));
    CHECK (maskForPad[10] == Catch::Approx (0.0f));

    // The highest-priority vocal should see nothing -- nothing outranks it.
    std::array<float, kNumBarkBands> maskForVocal {};
    registry.getGlobalMask (vocal.index, 1, maskForVocal);
    for (auto v : maskForVocal)
        CHECK (v == Catch::Approx (0.0f));
}

TEST_CASE ("getActiveSlotsSnapshot reports exactly the currently active slots", "[registry][gui]")
{
    auto& registry = SmartMaskRegistry::getInstance();

    ScopedSlot vocal;
    ScopedSlot pad;
    REQUIRE (vocal.index >= 0);
    REQUIRE (pad.index >= 0);

    registry.setPriority (vocal.index, 1);
    registry.setPriority (pad.index, 7);
    registry.setAmount (pad.index, 0.5f);

    std::array<float, kNumBarkBands> vocalBands {};
    vocalBands[3] = 0.42f;
    registry.updateSpectrum (vocal.index, vocalBands);

    std::array<float, kNumBarkBands> padBands {};
    padBands[12] = 0.13f;
    registry.updateSpectrum (pad.index, padBands);

    std::array<SmartMaskRegistry::SlotSnapshot, kMaxTrackSlots> snapshot {};
    const int count = registry.getActiveSlotsSnapshot (snapshot);

    REQUIRE (count >= 2);

    auto findBySlotIndex = [&] (int slotIndex, int n) -> const SmartMaskRegistry::SlotSnapshot*
    {
        for (int i = 0; i < n; ++i)
            if (snapshot[(size_t) i].slotIndex == slotIndex)
                return &snapshot[(size_t) i];
        return nullptr;
    };

    const auto* vocalSnapshot = findBySlotIndex (vocal.index, count);
    const auto* padSnapshot   = findBySlotIndex (pad.index, count);

    REQUIRE (vocalSnapshot != nullptr);
    REQUIRE (padSnapshot != nullptr);

    CHECK (vocalSnapshot->priority == 1);
    CHECK (vocalSnapshot->bandEnergies[3] == Catch::Approx (0.42f));

    CHECK (padSnapshot->priority == 7);
    CHECK (padSnapshot->amount == Catch::Approx (0.5f));
    CHECK (padSnapshot->bandEnergies[12] == Catch::Approx (0.13f));

    // A freshly registered slot defaults to unity mask gain (unmasked),
    // not zero, so the GUI doesn't misread "no data yet" as "silenced".
    for (auto g : padSnapshot->maskGains)
        CHECK (g == Catch::Approx (1.0f));

    // updateMaskGains() publishes what MaskingProcessor actually applied --
    // this is what lets the GUI show real masking, not just raw input energy.
    std::array<float, kNumBarkBands> padGains {};
    padGains.fill (1.0f);
    padGains[12] = 0.35f; // e.g. ducked by a higher-priority competitor
    registry.updateMaskGains (pad.index, padGains);

    registry.getActiveSlotsSnapshot (snapshot);
    const auto* padSnapshotAfterGains = findBySlotIndex (pad.index, count);
    REQUIRE (padSnapshotAfterGains != nullptr);
    CHECK (padSnapshotAfterGains->maskGains[12] == Catch::Approx (0.35f));
    CHECK (padSnapshotAfterGains->maskGains[0] == Catch::Approx (1.0f));

    // A slot released before the snapshot must not appear in it.
    const int releasedIndex = pad.index;
    registry.unregisterTrack (releasedIndex);
    const int countAfter = registry.getActiveSlotsSnapshot (snapshot);
    CHECK (findBySlotIndex (releasedIndex, countAfter) == nullptr);
    CHECK (countAfter == count - 1);

    // Prevent the ScopedSlot destructor from double-releasing.
    pad.index = -1;
}

TEST_CASE ("16 threads concurrently write and read the registry without data races", "[registry][concurrency][stress]")
{
    auto& registry = SmartMaskRegistry::getInstance();

    constexpr int numThreads          = 16;
    constexpr int iterationsPerThread = 2000;

    std::vector<int> slotIndices ((size_t) numThreads);
    for (int t = 0; t < numThreads; ++t)
    {
        slotIndices[(size_t) t] = registry.registerTrack();
        REQUIRE (slotIndices[(size_t) t] >= 0);
        registry.setPriority (slotIndices[(size_t) t], 1 + (t % 10));
    }

    std::atomic<bool> sawInvalidValue { false };

    std::vector<std::thread> threads;
    threads.reserve (numThreads);
    for (int t = 0; t < numThreads; ++t)
    {
        threads.emplace_back ([&, t]
        {
            std::mt19937 rng (1000u + (unsigned) t);
            std::uniform_real_distribution<float> dist (0.0f, 1.0f);

            const int mySlot     = slotIndices[(size_t) t];
            const int myPriority = 1 + (t % 10);

            std::array<float, kNumBarkBands> bands {};
            std::array<float, kNumBarkBands> mask {};

            for (int it = 0; it < iterationsPerThread; ++it)
            {
                for (auto& b : bands)
                    b = dist (rng);

                registry.updateSpectrum (mySlot, bands);
                registry.getGlobalMask (mySlot, myPriority, mask);

                for (auto v : mask)
                    if (! std::isfinite (v) || v < 0.0f || v > 1.0f)
                        sawInvalidValue.store (true, std::memory_order_relaxed);
            }
        });
    }

    for (auto& th : threads)
        th.join();

    CHECK_FALSE (sawInvalidValue.load());

    for (auto idx : slotIndices)
        registry.unregisterTrack (idx);
}
