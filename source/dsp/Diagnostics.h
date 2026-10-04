#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <cstring>
#include "Finite.h"
#include "Glitch.h"

// Set by CMake: 1 in Debug builds and when the ASTRALAY_DIAGNOSTICS option is on. Without it the
// probes below are empty and cost nothing.
#ifndef ASTRALAY_DIAGNOSTICS
 #define ASTRALAY_DIAGNOSTICS 0
#endif

namespace astralay::dsp::diagnostics
{

/** A value a glitch picked when it fired, and the range it was picked from. */
struct Pick
{
    float value = 0.0f, min = 0.0f, max = 0.0f;
};

using Picks = std::array<Pick, 2>;

inline Pick picked (float value, const RandomRange& range) noexcept
{
    return { value, juce::jmin (range.min, range.max), juce::jmax (range.min, range.max) };
}

#if ASTRALAY_DIAGNOSTICS

/** Something the audio thread reports. What values holds depends on the kind:
    - prepared: sample rate.
    - glitchStarted: first pick (value, min, max), second pick (value, min, max), length in
      samples, freeze. type is the GlitchType, or numGlitchTypes for a varispeed pitch glitch.
    - tapStatus, once per interval: freeze, delay in samples, feedback, then the peaks of the signal
      read from the delay line, after the glitches, and written back to the line, then the number
      of samples that were not finite, then the estimate of the loop's pitch in semitones. type has
      a bit per glitch type that ran, and stages holds the peak after each of those.
    - outputStatus, once per interval: peak before the output clip, samples clipped, the clip
      ceiling, freeze, samples that were not finite, output gain.
*/
struct Event
{
    enum class Kind { prepared, glitchStarted, tapStatus, outputStatus };

    Kind kind = Kind::prepared;
    int tap = -1;
    int type = 0;
    juce::int64 time = 0;   // In samples processed.
    std::array<float, 8> values {};
    std::array<float, numGlitchTypes> stages {};
};

/** Carries events from the audio thread to the thread that writes the log, without locking. */
class Sink
{
public:
    void push (const Event& event) noexcept
    {
        int start1, size1, start2, size2;
        fifo.prepareToWrite (1, start1, size1, start2, size2);

        if (size1 + size2 == 0)
        {
            dropped.fetch_add (1, std::memory_order_relaxed);
            return;
        }

        events[(size_t) (size1 > 0 ? start1 : start2)] = event;
        events[(size_t) (size1 > 0 ? start1 : start2)].time = getTime();
        fifo.finishedWrite (1);
    }

    /** Calls handle with each waiting event, oldest first. For the writing thread. */
    template <typename Handler>
    void drain (Handler&& handle)
    {
        int start1, size1, start2, size2;
        fifo.prepareToRead (fifo.getNumReady(), start1, size1, start2, size2);

        for (int i = 0; i < size1; ++i)
            handle (events[(size_t) (start1 + i)]);

        for (int i = 0; i < size2; ++i)
            handle (events[(size_t) (start2 + i)]);

        fifo.finishedRead (size1 + size2);
    }

    /** Events lost because the writing thread fell behind, since the last call. */
    int takeDropped() noexcept { return dropped.exchange (0, std::memory_order_relaxed); }

    juce::int64 getTime() const noexcept { return time.load (std::memory_order_relaxed); }
    void tick() noexcept { time.store (getTime() + 1, std::memory_order_relaxed); }

private:
    static constexpr int capacity = 16384;

    juce::AbstractFifo fifo { capacity };
    std::vector<Event> events { (size_t) capacity };
    std::atomic<juce::int64> time { 0 };
    std::atomic<int> dropped { 0 };
};

constexpr double statusIntervalSeconds = 0.05;

/** Watches one tap: reports each glitch as it starts, and the tap's levels once per interval. */
class TapProbe
{
public:
    void attach (Sink* newSink, int tapIndex, double sampleRate) noexcept
    {
        sink = newSink;
        tap = tapIndex;
        interval = juce::jmax (1, (int) (sampleRate * statusIntervalSeconds));
        clear();
    }

    void glitchStarted (GlitchType type, int lengthSamples, const Picks& picks, bool varispeed) noexcept
    {
        if (sink == nullptr)
            return;

        Event e;
        e.kind = Event::Kind::glitchStarted;
        e.tap = tap;
        e.type = varispeed ? numGlitchTypes : (int) type;
        e.values = { picks[0].value, picks[0].min, picks[0].max,
                     picks[1].value, picks[1].min, picks[1].max,
                     (float) lengthSamples, freeze };
        sink->push (e);
    }

    void setPitchOffset (float semitones) noexcept { pitchOffset = semitones; }

    /** The signal after one running glitch. */
    void stage (GlitchType type, float y) noexcept
    {
        if (sink == nullptr)
            return;

        auto& peak = status.stages[(size_t) type];
        peak = juce::jmax (peak, std::abs (y));
        status.type |= 1 << (int) type;
    }

    void sample (float currentFreeze, float delaySamples, float feedback,
                 float delayed, float glitched, float written) noexcept
    {
        if (sink == nullptr)
            return;

        freeze = currentFreeze;

        auto& v = status.values;
        v[3] = juce::jmax (v[3], std::abs (delayed));
        v[4] = juce::jmax (v[4], std::abs (glitched));
        v[5] = juce::jmax (v[5], std::abs (written));
        v[6] += isNonFinite (glitched) || isNonFinite (written) ? 1.0f : 0.0f;

        if (++count < interval)
            return;

        v[0] = freeze;
        v[1] = delaySamples;
        v[2] = feedback;
        v[7] = pitchOffset;
        sink->push (status);
        clear();
    }

private:
    void clear() noexcept
    {
        status = {};
        status.kind = Event::Kind::tapStatus;
        status.tap = tap;
        count = 0;
    }

    Sink* sink = nullptr;
    Event status;
    int tap = -1, interval = 2400, count = 0;
    float freeze = 0.0f, pitchOffset = 0.0f;
};

/** Watches the plugin's output just before the output clip, and keeps the log's clock. */
class OutputProbe
{
public:
    void attach (Sink* newSink, double sampleRate) noexcept
    {
        sink = newSink;
        interval = juce::jmax (1, (int) (sampleRate * statusIntervalSeconds));
        clear();

        if (sink != nullptr)
        {
            Event e;
            e.kind = Event::Kind::prepared;
            e.values[0] = (float) sampleRate;
            sink->push (e);
        }
    }

    void sample (float left, float right, float ceiling, float freeze, float gain) noexcept
    {
        if (sink == nullptr)
            return;

        sink->tick();

        auto& v = status.values;
        const auto peak = juce::jmax (std::abs (left), std::abs (right));
        v[0] = juce::jmax (v[0], peak);
        v[1] += ceiling > 0.0f && peak > ceiling ? 1.0f : 0.0f;
        v[4] += isNonFinite (left) || isNonFinite (right) ? 1.0f : 0.0f;

        if (++count < interval)
            return;

        v[2] = ceiling;
        v[3] = freeze;
        v[5] = gain;
        sink->push (status);
        clear();
    }

private:
    void clear() noexcept
    {
        status = {};
        status.kind = Event::Kind::outputStatus;
        count = 0;
    }

    Sink* sink = nullptr;
    Event status;
    int interval = 2400, count = 0;
};

#else

class Sink;

class TapProbe
{
public:
    void attach (Sink*, int, double) noexcept {}
    void glitchStarted (GlitchType, int, const Picks&, bool) noexcept {}
    void setPitchOffset (float) noexcept {}
    void stage (GlitchType, float) noexcept {}
    void sample (float, float, float, float, float, float) noexcept {}
};

class OutputProbe
{
public:
    void attach (Sink*, double) noexcept {}
    void sample (float, float, float, float, float) noexcept {}
};

#endif

} // namespace astralay::dsp::diagnostics
