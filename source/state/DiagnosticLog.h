#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/Diagnostics.h"

#if ASTRALAY_DIAGNOSTICS

namespace astralay::state
{

/** Writes a text log of what the plugin is doing, for tracking down problems by ear: every
    parameter's value and each change to it, each glitch as it fires with the values it picked and
    the ranges they came from, and, 20 times a second, the freeze amount and signal levels of each
    tap and of the output.

    Only in builds with ASTRALAY_DIAGNOSTICS, and only when the ASTRALAY_LOG environment variable
    is set when the plugin loads. The audio thread hands events to a background thread, which does
    the writing.
*/
class DiagnosticLog final : private juce::Thread
{
public:
    static constexpr auto environmentVariable = "ASTRALAY_LOG";

    /** A log in Documents/Astralay/Logs if the environment variable is set, otherwise null. */
    static std::unique_ptr<DiagnosticLog> createIfRequested (juce::AudioProcessor& processor);

    DiagnosticLog (juce::AudioProcessor& processor, const juce::File& file);
    ~DiagnosticLog() override;

    /** Where the audio thread reports to. */
    dsp::diagnostics::Sink& getSink() noexcept { return sink; }

    const juce::File& getFile() const noexcept { return file; }

private:
    struct Watched
    {
        juce::RangedAudioParameter* parameter = nullptr;
        float lastValue = -1.0f;
    };

    void run() override;
    void writePending();
    void writeEvent (const dsp::diagnostics::Event& event);
    void writeParameters (bool all);
    void line (const juce::String& kind, const juce::String& text);

    dsp::diagnostics::Sink sink;
    juce::File file;
    std::unique_ptr<juce::FileOutputStream> stream;
    std::vector<Watched> watched;
    double sampleRate = 48000.0;
};

} // namespace astralay::state

#endif
