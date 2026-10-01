#include "DiagnosticLog.h"

#if ASTRALAY_DIAGNOSTICS

namespace astralay::state
{

namespace
{
    using dsp::diagnostics::Event;

    /** How a glitch type's picked values are written. */
    struct PickFormat
    {
        const char* type;
        const char* first;
        const char* firstUnit;
        const char* second = nullptr;
        const char* secondUnit = "";
        bool firstIsSamples = false;
    };

    // In GlitchType order, then the varispeed kind of pitch glitch.
    const std::array<PickFormat, dsp::numGlitchTypes + 1> pickFormats
    {{
        { "reverse",  "segment", "ms", nullptr, "", true },
        { "stutter",  "slice", "ms", nullptr, "", true },
        { "grain",    "size", "ms", "density", "/s", true },
        { "pitch",    "speed", "st/pass", "from", "st" },
        { "lpc",      "shift", "st" },
        { "cepstral", "shift", "st" },
        { "ring",     "frequency", "Hz" },
        { "fm",       "ratio", "", "index", "" },
        { "crush",    "bits", "", "reduction", "x" },
        { "varispeed", "speed-change", "st" },
    }};

    juce::String number (double value, int decimals = 2)
    {
        return juce::String (value, decimals);
    }

    juce::String decibels (float gain)
    {
        if (dsp::diagnostics::isNonFinite (gain))
            return "not-finite";

        return gain <= 1.0e-10f ? juce::String ("-inf dB")
                                : juce::String (juce::Decibels::gainToDecibels (gain, -200.0f), 1) + " dB";
    }

    juce::String glitchNames (int mask)
    {
        juce::StringArray names;

        for (int i = 0; i < dsp::numGlitchTypes; ++i)
            if ((mask & (1 << i)) != 0)
                names.add (pickFormats[(size_t) i].type);

        return names.isEmpty() ? juce::String ("none") : names.joinIntoString (",");
    }
}

std::unique_ptr<DiagnosticLog> DiagnosticLog::createIfRequested (juce::AudioProcessor& processor)
{
    if (juce::SystemStats::getEnvironmentVariable (environmentVariable, {}).isEmpty())
        return nullptr;

    const auto folder = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                            .getChildFile ("Astralay").getChildFile ("Logs");
    const auto name = "Astralay " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H.%M.%S") + ".log";

    // Each instance of the plugin gets a log of its own.
    return std::make_unique<DiagnosticLog> (processor, folder.getChildFile (name).getNonexistentSibling());
}

DiagnosticLog::DiagnosticLog (juce::AudioProcessor& processor, const juce::File& fileToWrite)
    : juce::Thread ("Astralay diagnostics"), file (fileToWrite)
{
    for (auto* parameter : processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
            watched.push_back ({ ranged });

    file.getParentDirectory().createDirectory();
    stream = std::make_unique<juce::FileOutputStream> (file);

    if (! stream->openedOk())
    {
        stream.reset();
        return;
    }

    stream->setPosition (0);
    stream->truncate();

    *stream << "Astralay diagnostic log, " << juce::Time::getCurrentTime().toString (true, true) << juce::newLine
            << "Each line: seconds of audio processed, kind, details." << juce::newLine
            << "  param:  a parameter's value. All are listed with their full ranges at the start, then each change." << juce::newLine
            << "  glitch: a glitch firing on a tap, with each value it picked and the range set for it." << juce::newLine
            << "  tap:    every 50 ms for each tap that is sounding. read, glitched and written are the peak levels" << juce::newLine
            << "          read from the delay line, after the glitches, and written back to the line. After the bar," << juce::newLine
            << "          the peak after each glitch that ran, in processing order. pitch-at is the plugin's" << juce::newLine
            << "          estimate of how far the tap's looped audio sits from its original pitch." << juce::newLine
            << "  output: every 50 ms. peak is before the output clip; clipped counts the samples it caught." << juce::newLine
            << juce::newLine;

    writeParameters (true);
    stream->flush();
    startThread();
}

DiagnosticLog::~DiagnosticLog()
{
    stopThread (2000);
}

void DiagnosticLog::run()
{
    while (! threadShouldExit())
    {
        writePending();
        wait (50);
    }

    writePending();
}

void DiagnosticLog::writePending()
{
    sink.drain ([this] (const Event& event) { writeEvent (event); });

    if (const auto dropped = sink.takeDropped(); dropped > 0)
        line ("log", juce::String (dropped) + " events lost: the log could not keep up");

    writeParameters (false);
    stream->flush();
}

void DiagnosticLog::line (const juce::String& kind, const juce::String& text)
{
    const auto seconds = (double) sink.getTime() / sampleRate;
    *stream << juce::String (seconds, 4).paddedLeft (' ', 11) << "  " << kind.paddedRight (' ', 8) << text << juce::newLine;
}

void DiagnosticLog::writeParameters (bool all)
{
    for (auto& w : watched)
    {
        const auto value = w.parameter->getValue();

        if (! all && juce::exactlyEqual (value, w.lastValue))
            continue;

        w.lastValue = value;

        auto text = w.parameter->paramID + " \"" + w.parameter->getName (128) + "\" = " + w.parameter->getText (value, 0);

        if (all)
            text << "  (range " << w.parameter->getText (0.0f, 0) << " to " << w.parameter->getText (1.0f, 0) << ")";

        line ("param", text);
    }
}

void DiagnosticLog::writeEvent (const Event& event)
{
    const auto& v = event.values;
    const auto ms = [this] (float samples) { return samples * 1000.0 / sampleRate; };
    const auto tap = "tap=" + juce::String (event.tap + 1).paddedLeft ('0', 2);

    // Events carry the time they happened, which is earlier than the sink's clock is now.
    const auto write = [this, &event] (const juce::String& kind, const juce::String& text)
    {
        *stream << juce::String ((double) event.time / sampleRate, 4).paddedLeft (' ', 11) << "  "
                << kind.paddedRight (' ', 8) << text << juce::newLine;
    };

    switch (event.kind)
    {
        case Event::Kind::prepared:
            sampleRate = juce::jmax (1.0, (double) v[0]);
            write ("log", "prepared, sample rate " + number (sampleRate, 0));
            break;

        case Event::Kind::glitchStarted:
        {
            const auto& format = pickFormats[(size_t) juce::jlimit (0, dsp::numGlitchTypes, event.type)];
            const auto scale = [&] (float x) { return format.firstIsSamples ? ms (x) : (double) x; };

            // A value with no range to pick from reports the same number for both ends.
            const auto setRange = [] (double low, double high)
            {
                return juce::exactlyEqual (low, high) ? juce::String()
                                                      : " (set " + number (low) + " to " + number (high) + ")";
            };

            auto text = tap + " type=" + format.type + " length=" + number (ms (v[6]), 1) + "ms freeze=" + number (v[7])
                      + " " + format.first + "=" + number (scale (v[0])) + format.firstUnit
                      + setRange (scale (v[1]), scale (v[2]));

            if (format.second != nullptr)
                text << " " << format.second << "=" << number (v[3]) << format.secondUnit << setRange (v[4], v[5]);

            write ("glitch", text);
            break;
        }

        case Event::Kind::tapStatus:
        {
            // A tap with nothing in it says nothing.
            if (v[3] < 1.0e-6f && v[5] < 1.0e-6f && event.type == 0 && v[6] <= 0.0f)
                break;

            auto text = tap + " freeze=" + number (v[0]) + " delay=" + number (ms (v[1])) + "ms feedback=" + number (v[2] * 100.0, 1) + "%"
                      + " read=" + decibels (v[3]) + " glitched=" + decibels (v[4]) + " written=" + decibels (v[5])
                      + " not-finite=" + juce::String ((int) v[6]) + " pitch-at=" + number (v[7]) + "st ran=" + glitchNames (event.type);

            if (event.type != 0)
            {
                text << " |";

                for (int i = 0; i < dsp::numGlitchTypes; ++i)
                    if ((event.type & (1 << i)) != 0)
                        text << " " << pickFormats[(size_t) i].type << "=" << decibels (event.stages[(size_t) i]);
            }

            write ("tap", text);
            break;
        }

        case Event::Kind::outputStatus:
            if (v[0] < 1.0e-6f && v[4] <= 0.0f)
                break;

            write ("output", "peak=" + decibels (v[0]) + " clipped=" + juce::String ((int) v[1])
                                 + " ceiling=" + (v[2] > 0.0f ? decibels (v[2]) : juce::String ("off"))
                                 + " freeze=" + number (v[3]) + " gain=" + decibels (v[5])
                                 + " not-finite=" + juce::String ((int) v[4]));
            break;
    }
}

} // namespace astralay::state

#endif
