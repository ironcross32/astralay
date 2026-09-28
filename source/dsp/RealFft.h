#pragma once

#include <juce_core/juce_core.h>

struct PFFFT_Setup;

namespace astralay::dsp
{

/** A real-input FFT backed by PFFFT (much faster than JUCE's fallback FFT on Windows).

    Spectra are interleaved complex bins 0 to size / 2 inclusive, so a spectrum buffer needs
    size + 2 floats. The inverse is scaled, so forward then inverse returns the original signal.
    Input and output may be the same buffer.
*/
class RealFft
{
public:
    explicit RealFft (int order);
    ~RealFft();

    int getSize() const noexcept { return size; }

    void forward (const float* time, float* spectrum) noexcept;
    void inverse (const float* spectrum, float* time) noexcept;

private:
    int size;
    PFFFT_Setup* setup = nullptr;
    float* input = nullptr;
    float* output = nullptr;
    float* work = nullptr;

    JUCE_DECLARE_NON_COPYABLE (RealFft)
};

} // namespace astralay::dsp
