#include "RealFft.h"
#include <pffft/pffft.h>

namespace astralay::dsp
{

namespace
{
    float* allocate (int count)
    {
        return static_cast<float*> (pffft_aligned_malloc ((size_t) count * sizeof (float)));
    }
}

RealFft::RealFft (int order)
    : size (1 << order)
{
    // PFFFT's real transforms need a multiple of 32 points.
    jassert (size >= 32);

    setup = pffft_new_setup (size, PFFFT_REAL);
    input = allocate (size);
    output = allocate (size);
    work = allocate (size);
}

RealFft::~RealFft()
{
    pffft_aligned_free (work);
    pffft_aligned_free (output);
    pffft_aligned_free (input);
    pffft_destroy_setup (setup);
}

void RealFft::forward (const float* time, float* spectrum) noexcept
{
    std::copy (time, time + size, input);
    pffft_transform_ordered (setup, input, output, work, PFFFT_FORWARD);

    // PFFFT packs the purely real DC and Nyquist terms into the first complex slot.
    const auto nyquist = output[1];
    spectrum[0] = output[0];
    spectrum[1] = 0.0f;
    std::copy (output + 2, output + size, spectrum + 2);
    spectrum[size] = nyquist;
    spectrum[size + 1] = 0.0f;
}

void RealFft::inverse (const float* spectrum, float* time) noexcept
{
    input[0] = spectrum[0];
    input[1] = spectrum[size];
    std::copy (spectrum + 2, spectrum + size, input + 2);

    pffft_transform_ordered (setup, input, output, work, PFFFT_BACKWARD);

    const auto scale = 1.0f / (float) size;

    for (int i = 0; i < size; ++i)
        time[i] = output[i] * scale;
}

} // namespace astralay::dsp
