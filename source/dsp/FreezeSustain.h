#pragma once

#include "DelayLine.h"
#include "Finite.h"
#include "GlitchChain.h"

namespace astralay::dsp
{

/** A protected recording of one trip round a tap, used only during freeze sustain.

    Capture walks the original loop from oldest to newest, one sample per call, before the delay
    line overwrites it. This bounds audio-thread work even for sixteen ten-second loops. Storage
    is allocated in prepare, and reset only invalidates it. Pitch/formant metadata is stored at
    the same coarse resolution as the chain's loop tracks.

    Recovery crossfades toward the recording as the processed signal falls below 70% of the
    recording's RMS. It never amplifies either signal. The reference cannot erode, and DC is
    excluded from both the level comparison and the restored audio.
*/
class FreezeSustain
{
public:
    struct Sample
    {
        float audio = 0.0f, amount = 0.0f;
        GlitchChain::LoopState state;
    };

    void prepare (double newSampleRate, int maxSamples)
    {
        sampleRate = newSampleRate;
        audio.resize ((size_t) maxSamples);
        states.resize ((size_t) ((maxSamples + stateStep - 1) / stateStep));
        fadeCoefficient = 1.0 - std::exp (-1.0 / (0.05 * sampleRate));
        reset();
    }

    void reset() noexcept
    {
        length = captured = 0;
        position = amount = sum = squares = referenceMean = referencePower = 0.0;
        measuredMean = measuredSquares = 0.0;
    }

    Sample next (bool wanted, float freeze, double delay, const DelayLine& line, const GlitchChain& chain) noexcept
    {
        if (freeze <= 0.0f || (! wanted && amount < 1.0e-6))
        {
            reset();
            return {};
        }

        if (length == 0)
        {
            if (! wanted)
                return {};

            length = juce::jlimit (2, (int) audio.size(), (int) std::round (delay));
            // At least one loop, so pauses within long recordings aren't treated as erosion.
            measureCoefficient = 1.0 - std::exp (-1.0 / juce::jmax (0.05 * sampleRate, (double) length));
        }

        if (captured < length)
        {
            const auto value = line.read ((double) length);
            if (isNonFinite (value))
            {
                reset();
                return {};
            }

            audio[(size_t) captured] = value;
            if (captured % stateStep == 0)
                states[(size_t) (captured / stateStep)] = chain.getLoopState ((float) length);
            sum += value;
            squares += (double) value * value;

            if (++captured == length)
            {
                referenceMean = sum / length;
                referencePower = juce::jmax (0.0, squares / length - referenceMean * referenceMean);
                measuredMean = referenceMean;
                measuredSquares = squares / length;
            }
        }

        Sample result;
        if (captured == length && referencePower > 1.0e-16)
        {
            const auto power = juce::jmax (0.0, measuredSquares - measuredMean * measuredMean);
            const auto target = wanted ? juce::jlimit (0.0, 1.0, 1.0 - std::sqrt (power / referencePower) / 0.7) : 0.0;
            amount += fadeCoefficient * (target - amount);
            result.amount = (float) amount * freeze;

            if (result.amount > 0.0f)
            {
                result.audio = readAudio() - (float) referenceMean;
                result.state = states[(size_t) ((int) position / stateStep)];
            }
        }

        // Follow the tap's changing period without ever resampling back into the protected copy.
        position += (double) length / juce::jmax (2.0, delay);
        position -= std::floor (position / length) * length;
        return result;
    }

    /** Measure before reinjection: a crusher producing zero must keep receiving saved audio. */
    void observe (float processed) noexcept
    {
        if (length == 0 || captured < length || isNonFinite (processed))
            return;
        measuredMean += measureCoefficient * ((double) processed - measuredMean);
        measuredSquares += measureCoefficient * ((double) processed * processed - measuredSquares);
    }

private:
    float readAudio() const noexcept
    {
        const auto whole = (int) position;
        const auto fraction = (float) (position - whole);
        if (fraction == 0.0f)
            return audio[(size_t) whole];

        const auto& kernel = sinc::kernelFor (fraction);
        auto value = 0.0f;
        for (int k = 0; k < sinc::kernelSize; ++k)
        {
            const auto index = (whole - (sinc::kernelSize / 2 - 1) + k) % length;
            value += audio[(size_t) (index < 0 ? index + length : index)] * kernel[(size_t) k];
        }
        return value;
    }

    static constexpr int stateStep = 16;
    std::vector<float> audio;
    std::vector<GlitchChain::LoopState> states;
    int length = 0, captured = 0;
    double sampleRate = 48000.0, position = 0.0, amount = 0.0;
    double sum = 0.0, squares = 0.0, referenceMean = 0.0, referencePower = 0.0;
    double measuredMean = 0.0, measuredSquares = 0.0, measureCoefficient = 0.0, fadeCoefficient = 0.0;
};

} // namespace astralay::dsp
