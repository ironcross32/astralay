#include "FormantShifter.h"

namespace astralay::dsp
{

namespace
{
    constexpr int overlap = 4;              // Hops per frame.
    constexpr float maxEnvelopeGain = 20.0f;  // About +26 dB, so empty bands aren't boosted into noise.
    constexpr float minEnvelopeGain = 0.05f;
    constexpr float tiny = 1.0e-9f;
}

void FormantShifter::prepare (double sampleRate, Method newMethod)
{
    method = newMethod;

    // About 21 ms at any sample rate: 1024 samples at 44.1 or 48 kHz, 2048 at 88.2 or 96 kHz.
    const auto rateMultiple = juce::jmax (1, juce::roundToInt (sampleRate / 48000.0));
    const auto order = juce::jlimit (9, 13, 10 + (int) std::round (std::log2 ((double) rateMultiple)));

    fft = std::make_unique<RealFft> (order);
    frameSize = fft->getSize();
    hopSize = frameSize / overlap;
    bins = frameSize / 2 + 1;

    // Formant resolution: LPC order and cepstral lifter scale with the sample rate.
    lpcOrder = juce::jlimit (12, 48, (int) std::round (sampleRate / 2000.0));
    lifter = juce::jlimit (16, frameSize / 4, (int) std::round (sampleRate * 0.001));

    window.resize ((size_t) frameSize);

    for (int i = 0; i < frameSize; ++i)
        window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) frameSize);

    // Hann analysis and synthesis at 4x overlap sum to 1.5.
    overlapScale = 1.0f / 1.5f;

    spectrum.assign ((size_t) frameSize * 2, 0.0f);
    work.assign ((size_t) frameSize * 2, 0.0f);
    magnitude.assign ((size_t) bins, 0.0f);
    envelope.assign ((size_t) bins, 0.0f);
    lpcCoefficients.assign ((size_t) lpcOrder + 1, 0.0);
    autocorrelation.assign ((size_t) lpcOrder + 1, 0.0);

    output.assign ((size_t) juce::nextPowerOfTwo (frameSize * 2), 0.0f);
    outputMask = (int) output.size() - 1;

    history.prepare (frameSize + overlap * hopSize + 8);

    reset();
}

void FormantShifter::reset()
{
    history.clear();
    std::fill (output.begin(), output.end(), 0.0f);
    readPosition = 0;
    samplesSinceHop = 0;
    primed = false;
}

void FormantShifter::start (float semitones)
{
    ratio = std::pow (2.0f, semitones / 12.0f);
    std::fill (output.begin(), output.end(), 0.0f);
    readPosition = 0;
    samplesSinceHop = 0;
    primed = false;
}

float FormantShifter::next() noexcept
{
    if (! primed)
    {
        // Analyse the frames that would already overlap this moment, so output starts at full level.
        for (int k = overlap - 1; k > 0; --k)
            addFrame (k * hopSize, -k * hopSize);

        addFrame (0, 0);
        primed = true;
        samplesSinceHop = 0;
    }
    else if (++samplesSinceHop >= hopSize)
    {
        addFrame (0, 0);
        samplesSinceHop = 0;
    }

    const auto sample = output[(size_t) readPosition];
    output[(size_t) readPosition] = 0.0f;
    readPosition = (readPosition + 1) & outputMask;
    return sample;
}

void FormantShifter::addFrame (int endsSamplesAgo, int outputOffset) noexcept
{
    // Window the frame that ends endsSamplesAgo samples back.
    for (int i = 0; i < frameSize; ++i)
        spectrum[(size_t) i] = history.back (endsSamplesAgo + frameSize - 1 - i) * window[(size_t) i];

    std::fill (spectrum.begin() + frameSize, spectrum.end(), 0.0f);
    fft->forward (spectrum.data(), spectrum.data());

    for (int k = 0; k < bins; ++k)
    {
        const auto re = spectrum[(size_t) (2 * k)];
        const auto im = spectrum[(size_t) (2 * k + 1)];
        magnitude[(size_t) k] = std::sqrt (re * re + im * im);
    }

    estimateEnvelope();

    // Replace the envelope with a copy stretched by the ratio: the formant at bin k moves to k * ratio.
    const auto last = bins - 1;

    for (int k = 0; k < bins; ++k)
    {
        const auto source = (float) k / ratio;
        const auto index = juce::jmin ((int) source, last);
        const auto fraction = source - (float) index;
        const auto shifted = index >= last ? envelope[(size_t) last]
                                           : envelope[(size_t) index] + (envelope[(size_t) index + 1] - envelope[(size_t) index]) * fraction;

        const auto gain = juce::jlimit (minEnvelopeGain, maxEnvelopeGain, shifted / (envelope[(size_t) k] + tiny));
        spectrum[(size_t) (2 * k)] *= gain;
        spectrum[(size_t) (2 * k + 1)] *= gain;
    }

    fft->inverse (spectrum.data(), spectrum.data());

    const auto scale = overlapScale;

    for (int i = 0; i < frameSize; ++i)
    {
        const auto position = outputOffset + i;

        if (position >= 0)
            output[(size_t) ((readPosition + position) & outputMask)] += spectrum[(size_t) i] * window[(size_t) i] * scale;
    }
}

void FormantShifter::estimateEnvelope() noexcept
{
    if (method == Method::lpc)
        estimateLpcEnvelope();
    else
        estimateCepstralEnvelope();
}

void FormantShifter::estimateLpcEnvelope() noexcept
{
    // Autocorrelation of the windowed frame, as the inverse transform of its power spectrum. That's
    // circular rather than linear, but at these small lags the Hann taper makes the difference
    // negligible, and it's far cheaper than summing directly.
    std::fill (work.begin(), work.end(), 0.0f);

    for (int k = 0; k < bins; ++k)
        work[(size_t) (2 * k)] = magnitude[(size_t) k] * magnitude[(size_t) k];

    fft->inverse (work.data(), work.data());

    for (int lag = 0; lag <= lpcOrder; ++lag)
    {
        // Slight lag windowing for stability.
        const auto lagWindow = std::exp (-0.5 * std::pow (0.01 * lag, 2.0));
        autocorrelation[(size_t) lag] = (double) work[(size_t) lag] * lagWindow;
    }

    autocorrelation[0] = autocorrelation[0] * 1.0001 + 1.0e-12;

    // Levinson-Durbin recursion for the predictor coefficients a[0..order], a[0] = 1.
    std::fill (lpcCoefficients.begin(), lpcCoefficients.end(), 0.0);
    lpcCoefficients[0] = 1.0;
    auto error = autocorrelation[0];

    for (int i = 1; i <= lpcOrder && error > 1.0e-15; ++i)
    {
        auto accumulator = autocorrelation[(size_t) i];

        for (int j = 1; j < i; ++j)
            accumulator += lpcCoefficients[(size_t) j] * autocorrelation[(size_t) (i - j)];

        const auto reflection = -accumulator / error;

        for (int j = 1; j <= i / 2; ++j)
        {
            const auto a = lpcCoefficients[(size_t) j];
            const auto b = lpcCoefficients[(size_t) (i - j)];
            lpcCoefficients[(size_t) j] = a + reflection * b;

            if (j != i - j)
                lpcCoefficients[(size_t) (i - j)] = b + reflection * a;
        }

        lpcCoefficients[(size_t) i] = reflection;
        error *= 1.0 - reflection * reflection;
    }

    // The envelope is 1 / |A(w)|, sampled at the frame's bins.
    std::fill (work.begin(), work.end(), 0.0f);

    for (size_t j = 0; j < lpcCoefficients.size(); ++j)
        work[j] = (float) lpcCoefficients[j];
    fft->forward (work.data(), work.data());

    for (int k = 0; k < bins; ++k)
        envelope[(size_t) k] = 1.0f / (std::hypot (work[(size_t) (2 * k)], work[(size_t) (2 * k + 1)]) + tiny);
}

void FormantShifter::estimateCepstralEnvelope() noexcept
{
    // Real cepstrum: inverse transform of the log magnitude, keep the low quefrencies, transform back.
    std::fill (work.begin(), work.end(), 0.0f);

    for (int k = 0; k < bins; ++k)
        work[(size_t) (2 * k)] = std::log (magnitude[(size_t) k] + tiny);

    fft->inverse (work.data(), work.data());

    for (int n = lifter; n <= frameSize - lifter; ++n)
        work[(size_t) n] = 0.0f;

    std::fill (work.begin() + frameSize, work.end(), 0.0f);
    fft->forward (work.data(), work.data());

    for (int k = 0; k < bins; ++k)
        envelope[(size_t) k] = std::exp (work[(size_t) (2 * k)]);
}

} // namespace astralay::dsp
