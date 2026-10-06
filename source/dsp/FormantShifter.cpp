#include "FormantShifter.h"

namespace astralay::dsp
{

namespace
{
    constexpr int overlap = 4;              // Hops per frame.
    constexpr float maxEnvelopeGain = 20.0f;  // About +26 dB, so empty bands aren't boosted into noise.
    constexpr float minEnvelopeGain = 0.05f;
    constexpr float tiny = 1.0e-9f;
    constexpr int gainSmoothing = 4;        // Bins either side that a bin's gain is averaged over.
}

void FormantShifter::prepare (double sampleRate, Method newMethod, int scheduleSlot)
{
    method = newMethod;

    // About 21 ms at any sample rate: 1024 samples at 44.1 or 48 kHz, 2048 at 88.2 or 96 kHz.
    const auto rateMultiple = juce::jmax (1, juce::roundToInt (sampleRate / 48000.0));
    const auto order = juce::jlimit (9, 13, 10 + (int) std::round (std::log2 ((double) rateMultiple)));

    fft = std::make_unique<RealFft> (order);
    frameSize = fft->getSize();
    hopSize = frameSize / overlap;
    primeSpacing = hopSize / (scheduleSlots * overlap);
    schedulePhase = juce::jlimit (0, scheduleSlots - 1, scheduleSlot) * hopSize / scheduleSlots;
    startupFadeSamples = juce::jmax (1, (int) (sampleRate * 0.005));
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
    gains.assign ((size_t) bins, 0.0f);
    lpcCoefficients.assign ((size_t) lpcOrder + 1, 0.0);
    autocorrelation.assign ((size_t) lpcOrder + 1, 0.0);

    output.assign ((size_t) juce::nextPowerOfTwo (frameSize * 2), 0.0f);
    outputMask = (int) output.size() - 1;

    maxAlignedLoop = 2 * frameSize;
    history.prepare (frameSize + overlap * hopSize + maxAlignedLoop + 8);

    reset();
}

void FormantShifter::reset()
{
    history.clear();
    validOutputSamples = 0;
    readPosition = 0;
    samplesSinceHop = 0;
    primed = false;
    primeFramesLeft = overlap;
    frameClock = hopSize - 1;
    frameCount = 0;
    startupSamples = 0;
    startupGain = 0.0f;
}

void FormantShifter::start (float semitones, float loopSamples)
{
    shift = semitones;

    // Output lags input by a frame less a sample. In a short loop, which repeats itself, frames are
    // taken from further back by whatever makes that lag a whole number of trips round the loop, so
    // the shifted audio lines up with the audio it fades in over and out to.
    lookBack = 0;

    if (loopSamples >= 1.0f && loopSamples <= (float) maxAlignedLoop)
    {
        const auto loop = juce::jmax (1, juce::roundToInt (loopSamples));
        const auto lag = frameSize - 1;
        lookBack = (lag + loop - 1) / loop * loop - lag;
    }

    validOutputSamples = 0;
    readPosition = 0;
    samplesSinceHop = 0;
    primed = false;
    primeFramesLeft = overlap;
    startupSamples = 0;
    startupGain = 0.0f;
}

float FormantShifter::next() noexcept
{
    if (! primed)
    {
        // Each shifter owns four small slots within the shared hop. Build the old overlapping
        // frames before their common playback origin; the newest frame ends at that origin.
        // Input history moves while pending output stays still, so account for the time remaining.
        const auto phase = schedulePhase + (overlap - primeFramesLeft) * primeSpacing;
        if (frameClock == phase)
        {
            const auto k = --primeFramesLeft;
            addFrame (k * (hopSize - primeSpacing), -k * hopSize);
            primed = primeFramesLeft == 0;
            samplesSinceHop = 0;
        }
        if (! primed)
            return 0.0f;
    }
    else if (++samplesSinceHop >= hopSize)
    {
        addFrame (0, 0);
        samplesSinceHop = 0;
    }

    const auto sample = validOutputSamples > 0 ? output[(size_t) readPosition] : 0.0f;
    startupGain = (float) startupSamples / (float) startupFadeSamples;
    startupSamples = juce::jmin (startupSamples + 1, startupFadeSamples);
    validOutputSamples = juce::jmax (0, validOutputSamples - 1);
    readPosition = (readPosition + 1) & outputMask;
    return sample;
}

void FormantShifter::addFrame (int endsSamplesAgo, int outputOffset) noexcept
{
    ++frameCount;
    // Window the frame that ends endsSamplesAgo samples back, or its match earlier in a short loop.
    for (int i = 0; i < frameSize; ++i)
        spectrum[(size_t) i] = history.back (lookBack + endsSamplesAgo + frameSize - 1 - i) * window[(size_t) i];

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
    const auto ratio = std::exp2 (shift / 12.0f);

    for (int k = 0; k < bins; ++k)
    {
        const auto source = (float) k / ratio;
        const auto index = juce::jmin ((int) source, last);
        const auto fraction = source - (float) index;
        const auto shifted = index >= last ? envelope[(size_t) last]
                                           : envelope[(size_t) index] + (envelope[(size_t) index + 1] - envelope[(size_t) index]) * fraction;

        // In decibels, near enough, for the averaging below.
        gains[(size_t) k] = std::log (juce::jlimit (minEnvelopeGain, maxEnvelopeGain, shifted / (envelope[(size_t) k] + tiny)));
    }

    auto powerBefore = 0.0f, powerAfter = 0.0f;

    for (int k = 0; k < bins; ++k)
    {
        // Each bin's gain is averaged with its neighbours', nearer ones counting for more. One
        // partial covers about four bins of a frame. An envelope with features narrower than that,
        // as LPC's has on tonal audio, would change the gain inside a partial; the frames then no
        // longer overlap cleanly, and a pass costs up to 2 dB that the sums below can't see.
        auto sum = 0.0f, total = 0.0f;

        for (int offset = -gainSmoothing; offset <= gainSmoothing; ++offset)
        {
            const auto share = (float) (gainSmoothing + 1 - std::abs (offset));
            sum += share * gains[(size_t) juce::jlimit (0, last, k + offset)];
            total += share;
        }

        const auto gain = std::exp (sum / total);

        // The first and last bins appear once in the whole spectrum and every other bin twice, so
        // they carry half the weight. Counted in full, a loop holding a constant offset, as short
        // frozen loops do, gained level each time energy moved out of the first bin.
        const auto weight = k == 0 || k == last ? 0.5f : 1.0f;
        const auto power = weight * magnitude[(size_t) k] * magnitude[(size_t) k];
        powerBefore += power;
        powerAfter += power * gain * gain;

        spectrum[(size_t) (2 * k)] *= gain;
        spectrum[(size_t) (2 * k + 1)] *= gain;
    }

    fft->inverse (spectrum.data(), spectrum.data());

    // The frame keeps the level it came in with. Most sound falls away towards the top, so moving
    // its formants up raises every band a little and moving them down lowers every band. Heard
    // once that hardly shows, but in a feedback loop it happens on every pass, and a frozen tap
    // climbs until it clips or fades to nothing.
    const auto scale = overlapScale * std::sqrt ((powerBefore + tiny) / (powerAfter + tiny));

    for (int i = 0; i < frameSize; ++i)
    {
        const auto position = outputOffset + i;

        if (position >= 0)
        {
            auto& destination = output[(size_t) ((readPosition + position) & outputMask)];
            // A frame extends the contiguous pending output. Its new part replaces stale storage.
            const auto previous = position < validOutputSamples ? destination : 0.0f;
            destination = previous + spectrum[(size_t) i] * window[(size_t) i] * scale;
        }
    }
    validOutputSamples = juce::jmax (validOutputSamples, frameSize + outputOffset);
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
