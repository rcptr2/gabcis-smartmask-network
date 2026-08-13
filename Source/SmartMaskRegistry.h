#pragma once

#include <algorithm>
#include <array>
#include <atomic>

namespace smartmask
{

constexpr int kNumBarkBands       = 24;
constexpr int kMaxTrackSlots      = 32;
constexpr int kMaxTrackNameLength = 31; // +1 for the null terminator = 32 bytes

/** One frame of psychoacoustic Bark-band energies for a single track.
    Cache-line aligned so two buffers never false-share.
*/
struct alignas (64) BarkSpectrum
{
    std::array<float, kNumBarkBands> bandEnergies {};
};

/**
    Global, lock-free registry shared by every SmartMask Network instance
    loaded in the host (see Chapter 3 of the spec: "SmartMaskRegistry
    (Global Singleton)").

    Real-time contract (Chapter 4):
      - No heap allocation after a slot has been registered.
      - No std::mutex / std::lock_guard / juce::CriticalSection anywhere here.
      - Cross-instance communication happens only through std::atomic loads
        and stores: scalar parameters, and a double-buffered
        std::atomic<BarkSpectrum*> pointer swap per slot.

    Threading model:
      - registerTrack() / unregisterTrack() are called once each, from the
        owning plugin instance's constructor / destructor (message thread),
        never from processBlock(). They are lock-free (CAS on an atomic
        bool) but are not meant to run every block.
      - setPriority() / setAmount() are called from the message thread
        whenever the user changes a parameter.
      - updateSpectrum() and getGlobalMask() are called every processBlock()
        from the audio thread and are wait-free: a bounded loop over
        kMaxTrackSlots with only atomic loads/stores, no branches that can
        block or allocate.

    A slot has exactly one writer for its own buffers (the instance that
    registered it, via updateSpectrum()) and many concurrent readers (every
    other instance's getGlobalMask()), so the double buffer only ever needs
    a single atomic pointer, not a full SPSC queue.
*/
class SmartMaskRegistry
{
public:
    static SmartMaskRegistry& getInstance();

    SmartMaskRegistry (const SmartMaskRegistry&)            = delete;
    SmartMaskRegistry& operator= (const SmartMaskRegistry&) = delete;

    /** Claims a free slot for a newly-loaded plugin instance. Returns the
        slot index, or -1 if all kMaxTrackSlots slots are already taken.
        Safe to call concurrently from multiple instances' constructors.
    */
    int registerTrack();

    /** Releases a previously registered slot. Call once, from the owning
        instance's destructor. Not real-time safe and must not be called
        from the audio thread. Passing an out-of-range or already-released
        slotIndex is a no-op.
    */
    void unregisterTrack (int slotIndex);

    /** Sets the priority for a registered slot (1 = highest / vocal,
        10 = lowest / pad). Called from the message thread.
    */
    void setPriority (int slotIndex, int priority);

    /** Sets the suppression amount (0.0f-1.0f) for a registered slot.
        Called from the message thread.
    */
    void setAmount (int slotIndex, float amount);

    /** Publishes a freshly analysed Bark spectrum for slotIndex. Writes into
        the slot's currently-inactive buffer, then atomically swaps it in.
        Zero heap allocation, wait-free. Called once per audio block from
        the owning instance's audio thread.
    */
    void updateSpectrum (int slotIndex, const std::array<float, kNumBarkBands>& incomingBands);

    /** Publishes the per-Bark-band gain curve MaskingProcessor::
        computeBandGains() actually applied this block -- i.e. what masking
        is really doing to this track's output, as opposed to updateSpectrum
        's raw pre-mask input energy. Same double-buffered, wait-free
        contract as updateSpectrum(); exists purely so Phase 5's spectrum
        visualiser can show the *masked* (post-gain) energy instead of the
        raw input, which is the only way the plugin's actual effect is
        visible on screen. Called once per audio block from the owning
        instance's audio thread.
    */
    void updateMaskGains (int slotIndex, const std::array<float, kNumBarkBands>& gains);

    /** Publishes the host-reported track name for slotIndex (e.g. FL
        Studio's own mixer track name, via AudioProcessor::
        updateTrackProperties() -- see PluginProcessor.cpp), so
        PriorityListComponent can show that instead of the generic
        "Track #<id>" creation-order fallback (user report, 2026-08-03:
        the fallback numbering didn't match the DAW's own track numbers,
        which was confusing). Message-thread only, same as
        updateTrackProperties() itself -- not part of the real-time
        contract. utf8Name is copied (truncated to kMaxTrackNameLength) into
        this slot's own buffer, so the caller's string does not need to
        outlive the call. Passing nullptr is a no-op.

        A plain const char* (not juce::String) keeps this header JUCE
        -independent, same reasoning as the rest of this class (see Phase 1
        CHANGELOG entry).
    */
    void setTrackName (int slotIndex, const char* utf8Name);

    /** Cross-instance priority-change REQUEST (not a direct write). Each
        plugin instance's "priority" APVTS parameter is owned exclusively
        by that instance -- no other instance holds a pointer to it, and
        writing another instance's parameter state directly from outside
        would bypass normal host automation/undo handling for it. So
        instead, this posts a request into slotIndex's own single-slot
        mailbox; the OWNING instance (SmartMaskAudioProcessor's own
        message-thread poll timer, independent of whether its editor
        window is even open -- see PluginProcessor.cpp) picks it up via
        takeRequestedPriorityChange() and applies it through its own
        parameter, exactly like any other edit. This is what lets
        PriorityListComponent offer full drag-to-reorder of every row from
        any single open instance's editor (user report, 2026-08-03),
        including rows belonging to instances whose own editor isn't even
        open right now.

        Message-thread only. A pending request is a single slot per track,
        not a queue -- if two requests race for the same slot before it's
        picked up, the later call simply overwrites the earlier one, which
        is an acceptable, deliberately simple resolution for a rare,
        user-driven UI action.
    */
    void requestPriorityChange (int slotIndex, int newPriority);

    /** Returns and clears slotIndex's pending priority-change request (see
        requestPriorityChange()), or -1 if none is pending. Meant to be
        polled at a UI-ish rate (e.g. 30Hz) by the OWNING instance only.
    */
    int takeRequestedPriorityChange (int slotIndex);

    /** For every other active slot whose priority is numerically lower
        (= higher priority) than currentPriority, takes the per-Bark-band
        maximum of their published spectra and writes it to outMaskGains.
        This is the raw competing-energy vector, not a gain curve: turning
        it into an attenuation curve using this instance's own amount/
        attack/release settings is the Phase 3 MaskingProcessor's job.
        Wait-free: bounded loop over kMaxTrackSlots, atomic loads only.
    */
    void getGlobalMask (int currentSlotIndex, int currentPriority,
                         std::array<float, kNumBarkBands>& outMaskGains) const;

    /** A read-only snapshot of one active slot, for GUI use (Phase 5's
        spectrum visualiser and priority list -- never the audio thread).
    */
    struct SlotSnapshot
    {
        int slotIndex = -1;
        int trackId   = -1;
        int priority  = 10;
        float amount  = 0.0f;
        std::array<float, kNumBarkBands> bandEnergies {};
        // Per-band gain last applied by MaskingProcessor (1.0 = no
        // attenuation). Defaults to all-1.0 for a freshly registered slot
        // that hasn't published any mask gains yet, so it reads as
        // "unmasked" rather than "silent".
        std::array<float, kNumBarkBands> maskGains {};

        // Host-reported track name (see setTrackName()), empty
        // ('\0'-terminated at index 0) if the host never provided one --
        // callers should fall back to a generic label in that case.
        std::array<char, kMaxTrackNameLength + 1> trackName {};
    };

    /** Fills outSlots with every currently active slot and returns how many
        were written (0..kMaxTrackSlots). Not part of the real-time
        contract -- intended for the GUI's ~30Hz timer callback (message
        thread), not processBlock() -- but happens to use the same
        lock-free atomic loads as the real-time methods regardless.
    */
    int getActiveSlotsSnapshot (std::array<SlotSnapshot, kMaxTrackSlots>& outSlots) const;

private:
    SmartMaskRegistry()  = default;
    ~SmartMaskRegistry() = default;

    struct alignas (64) TrackSlot
    {
        std::atomic<bool>  active { false };
        std::atomic<int>   trackId { -1 };
        std::atomic<int>   priority { 10 };
        std::atomic<float> amount { 0.0f };

        // See requestPriorityChange()/takeRequestedPriorityChange(). -1 =
        // no pending request.
        std::atomic<int> requestedPriority { -1 };

        // Double-buffered spectrum: currentSpectrum always points at one of
        // this slot's own two pre-allocated buffers below. updateSpectrum()
        // swaps the pointer after writing the other buffer in full, so a
        // concurrent reader always sees a complete, never a torn, frame.
        std::atomic<BarkSpectrum*> currentSpectrum { nullptr };
        std::array<BarkSpectrum, 2> spectrumBuffers {};

        // Same double-buffering scheme, for the applied mask-gain curve
        // (see updateMaskGains()). Reuses BarkSpectrum purely as a generic
        // cache-aligned 24-float container, not because these are energies.
        std::atomic<BarkSpectrum*> currentMaskGains { nullptr };
        std::array<BarkSpectrum, 2> maskGainBuffers {};

        // Same double-buffering scheme again, for the host-reported track
        // name (see setTrackName()).
        struct alignas (64) TrackNameBuffer
        {
            std::array<char, kMaxTrackNameLength + 1> chars {};
        };
        std::atomic<TrackNameBuffer*> currentName { nullptr };
        std::array<TrackNameBuffer, 2> nameBuffers {};
    };

    std::array<TrackSlot, kMaxTrackSlots> slots;
    std::atomic<int> nextTrackId { 0 };
};

} // namespace smartmask
