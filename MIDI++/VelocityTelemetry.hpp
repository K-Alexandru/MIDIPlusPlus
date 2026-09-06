#pragma once

// VelocityTelemetry: what was actually played, for the velocity curve graph.
//
// Why this exists: the curve editor is specified to draw a histogram of the
// velocities you play and a dot for the note sounding now. Neither could be
// built, because nothing in the input path kept the velocities it saw, so the
// shipped graph says plainly that it is a pointer preview. This is the missing
// half. The input path records here; a UI owner reads.
//
// Header only and free of project references, so consuming it costs no change
// to either vcxproj.
//
// Live input only. Autoplay velocities are produced by the curve under
// inspection, so feeding them back would draw the graph its own output.

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace velocity_telemetry {

// The mockup's histogram is 32 bars and the engine's curve is 32 samples, so
// one bar is one sample rather than an arbitrary rebinning of the range.
inline constexpr int kBuckets = 32;

struct Snapshot {
    std::array<uint32_t, kBuckets> buckets{};
    uint32_t total = 0;
    // 0 means nothing has been played since the last reset. A note on carrying
    // velocity 0 is a note off, so 0 is never a real played velocity.
    uint8_t last = 0;
    // Moves on every recorded note and on every reset. A reader holding the
    // previous value can skip its redraw.
    uint64_t revision = 0;
};

namespace detail {
inline std::array<std::atomic<uint32_t>, kBuckets> buckets{};
inline std::atomic<uint8_t> last{0};
inline std::atomic<uint64_t> revision{0};
} // namespace detail

inline int bucketFor(uint8_t velocity) noexcept { return velocity * kBuckets / 128; }

// Runs on the MIDI callback thread, which is the thread the whole latency
// budget is measured against, so it allocates nothing, takes no lock and never
// blocks: three relaxed atomics and a release on the revision.
inline void record(uint8_t velocity) noexcept {
    if (velocity == 0 || velocity > 127) return;
    detail::buckets[bucketFor(velocity)].fetch_add(1, std::memory_order_relaxed);
    detail::last.store(velocity, std::memory_order_relaxed);
    detail::revision.fetch_add(1, std::memory_order_release);
}

// Decodes one complete MIDI message and records it when it is a note on that
// sounds. Channel selection stays with the caller, because only the caller
// knows which channel the user chose.
inline void observe(const uint8_t* data, size_t length) noexcept {
    if (!data || length < 3) return;
    if ((data[0] & 0xF0) != 0x90) return;  // note off and control change carry no played velocity
    if (data[1] > 127 || data[2] > 127) return;
    record(data[2]);                       // a velocity of 0 is a note off, and record drops it
}

// Not one instant: the buckets are read one at a time, so a snapshot taken
// while someone is playing can be a note or two out of step with itself. A
// histogram does not need better, and paying for a lock on the callback thread
// to get it would be the wrong trade.
inline Snapshot snapshot() noexcept {
    Snapshot result;
    result.revision = detail::revision.load(std::memory_order_acquire);
    result.last = detail::last.load(std::memory_order_relaxed);
    for (int i = 0; i < kBuckets; ++i) {
        result.buckets[i] = detail::buckets[i].load(std::memory_order_relaxed);
        result.total += result.buckets[i];
    }
    return result;
}

// For a new session, or for a reader that wants the histogram to describe the
// piece in front of it rather than everything since launch.
inline void reset() noexcept {
    for (auto& bucket : detail::buckets) bucket.store(0, std::memory_order_relaxed);
    detail::last.store(0, std::memory_order_relaxed);
    detail::revision.fetch_add(1, std::memory_order_release);
}

} // namespace velocity_telemetry
