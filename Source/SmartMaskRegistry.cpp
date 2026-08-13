#include "SmartMaskRegistry.h"

namespace smartmask
{

SmartMaskRegistry& SmartMaskRegistry::getInstance()
{
    static SmartMaskRegistry instance;
    return instance;
}

int SmartMaskRegistry::registerTrack()
{
    for (int i = 0; i < kMaxTrackSlots; ++i)
    {
        auto& slot = slots[(size_t) i];

        bool expected = false;
        if (slot.active.compare_exchange_strong (expected, true,
                                                   std::memory_order_acq_rel,
                                                   std::memory_order_relaxed))
        {
            slot.trackId.store (nextTrackId.fetch_add (1, std::memory_order_relaxed),
                                 std::memory_order_relaxed);
            slot.priority.store (10, std::memory_order_relaxed);
            slot.amount.store (0.0f, std::memory_order_relaxed);
            slot.requestedPriority.store (-1, std::memory_order_relaxed);

            slot.spectrumBuffers[0] = BarkSpectrum {};
            slot.spectrumBuffers[1] = BarkSpectrum {};
            slot.currentSpectrum.store (&slot.spectrumBuffers[0], std::memory_order_release);

            // Mask gains default to unity (no attenuation) until the owning
            // instance's first processBlock() publishes a real curve, so a
            // freshly registered slot reads as "unmasked", not "silent".
            slot.maskGainBuffers[0].bandEnergies.fill (1.0f);
            slot.maskGainBuffers[1].bandEnergies.fill (1.0f);
            slot.currentMaskGains.store (&slot.maskGainBuffers[0], std::memory_order_release);

            // Empty until/unless the host calls updateTrackProperties()
            // (see setTrackName()) -- callers fall back to a generic label
            // for an empty name.
            slot.nameBuffers[0].chars[0] = '\0';
            slot.nameBuffers[1].chars[0] = '\0';
            slot.currentName.store (&slot.nameBuffers[0], std::memory_order_release);

            return i;
        }
    }

    return -1;
}

void SmartMaskRegistry::unregisterTrack (int slotIndex)
{
    if (slotIndex < 0 || slotIndex >= kMaxTrackSlots)
        return;

    auto& slot = slots[(size_t) slotIndex];

    if (! slot.active.load (std::memory_order_acquire))
        return;

    slot.currentSpectrum.store (nullptr, std::memory_order_release);
    slot.currentMaskGains.store (nullptr, std::memory_order_release);
    slot.currentName.store (nullptr, std::memory_order_release);
    slot.requestedPriority.store (-1, std::memory_order_relaxed);
    slot.trackId.store (-1, std::memory_order_relaxed);
    slot.active.store (false, std::memory_order_release);
}

void SmartMaskRegistry::setPriority (int slotIndex, int priority)
{
    if (slotIndex < 0 || slotIndex >= kMaxTrackSlots)
        return;

    slots[(size_t) slotIndex].priority.store (priority, std::memory_order_relaxed);
}

void SmartMaskRegistry::setAmount (int slotIndex, float amount)
{
    if (slotIndex < 0 || slotIndex >= kMaxTrackSlots)
        return;

    slots[(size_t) slotIndex].amount.store (amount, std::memory_order_relaxed);
}

void SmartMaskRegistry::updateSpectrum (int slotIndex, const std::array<float, kNumBarkBands>& incomingBands)
{
    if (slotIndex < 0 || slotIndex >= kMaxTrackSlots)
        return;

    auto& slot = slots[(size_t) slotIndex];

    auto* current = slot.currentSpectrum.load (std::memory_order_acquire);
    auto* backBuffer = (current == &slot.spectrumBuffers[0]) ? &slot.spectrumBuffers[1]
                                                              : &slot.spectrumBuffers[0];

    backBuffer->bandEnergies = incomingBands;

    slot.currentSpectrum.store (backBuffer, std::memory_order_release);
}

void SmartMaskRegistry::updateMaskGains (int slotIndex, const std::array<float, kNumBarkBands>& gains)
{
    if (slotIndex < 0 || slotIndex >= kMaxTrackSlots)
        return;

    auto& slot = slots[(size_t) slotIndex];

    auto* current = slot.currentMaskGains.load (std::memory_order_acquire);
    auto* backBuffer = (current == &slot.maskGainBuffers[0]) ? &slot.maskGainBuffers[1]
                                                              : &slot.maskGainBuffers[0];

    backBuffer->bandEnergies = gains;

    slot.currentMaskGains.store (backBuffer, std::memory_order_release);
}

void SmartMaskRegistry::setTrackName (int slotIndex, const char* utf8Name)
{
    if (slotIndex < 0 || slotIndex >= kMaxTrackSlots || utf8Name == nullptr)
        return;

    auto& slot = slots[(size_t) slotIndex];

    auto* current = slot.currentName.load (std::memory_order_acquire);
    auto* backBuffer = (current == &slot.nameBuffers[0]) ? &slot.nameBuffers[1] : &slot.nameBuffers[0];

    size_t n = 0;
    for (; n < (size_t) kMaxTrackNameLength && utf8Name[n] != '\0'; ++n)
        backBuffer->chars[n] = utf8Name[n];
    backBuffer->chars[n] = '\0';

    slot.currentName.store (backBuffer, std::memory_order_release);
}

void SmartMaskRegistry::requestPriorityChange (int slotIndex, int newPriority)
{
    if (slotIndex < 0 || slotIndex >= kMaxTrackSlots)
        return;

    slots[(size_t) slotIndex].requestedPriority.store (newPriority, std::memory_order_relaxed);
}

int SmartMaskRegistry::takeRequestedPriorityChange (int slotIndex)
{
    if (slotIndex < 0 || slotIndex >= kMaxTrackSlots)
        return -1;

    return slots[(size_t) slotIndex].requestedPriority.exchange (-1, std::memory_order_relaxed);
}

void SmartMaskRegistry::getGlobalMask (int currentSlotIndex, int currentPriority,
                                        std::array<float, kNumBarkBands>& outMaskGains) const
{
    outMaskGains.fill (0.0f);

    for (int i = 0; i < kMaxTrackSlots; ++i)
    {
        if (i == currentSlotIndex)
            continue;

        const auto& slot = slots[(size_t) i];

        if (! slot.active.load (std::memory_order_acquire))
            continue;

        if (slot.priority.load (std::memory_order_relaxed) >= currentPriority)
            continue;

        auto* spectrum = slot.currentSpectrum.load (std::memory_order_acquire);
        if (spectrum == nullptr)
            continue;

        for (int band = 0; band < kNumBarkBands; ++band)
            outMaskGains[(size_t) band] = std::max (outMaskGains[(size_t) band],
                                                     spectrum->bandEnergies[(size_t) band]);
    }
}

int SmartMaskRegistry::getActiveSlotsSnapshot (std::array<SlotSnapshot, kMaxTrackSlots>& outSlots) const
{
    int count = 0;

    for (int i = 0; i < kMaxTrackSlots; ++i)
    {
        const auto& slot = slots[(size_t) i];

        if (! slot.active.load (std::memory_order_acquire))
            continue;

        auto* spectrum = slot.currentSpectrum.load (std::memory_order_acquire);
        if (spectrum == nullptr)
            continue;

        auto& out = outSlots[(size_t) count];
        out.slotIndex    = i;
        out.trackId      = slot.trackId.load (std::memory_order_relaxed);
        out.priority     = slot.priority.load (std::memory_order_relaxed);
        out.amount       = slot.amount.load (std::memory_order_relaxed);
        out.bandEnergies = spectrum->bandEnergies;

        auto* maskGains = slot.currentMaskGains.load (std::memory_order_acquire);
        if (maskGains != nullptr)
            out.maskGains = maskGains->bandEnergies;
        else
            out.maskGains.fill (1.0f); // never hit post-registerTrack; unity gain if it ever were

        auto* namePtr = slot.currentName.load (std::memory_order_acquire);
        if (namePtr != nullptr)
            out.trackName = namePtr->chars;
        else
            out.trackName[0] = '\0';

        ++count;
    }

    return count;
}

} // namespace smartmask
