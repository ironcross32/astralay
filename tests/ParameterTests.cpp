#include <set>
#include <juce_audio_processors/juce_audio_processors.h>
#include "params/NoteValues.h"
#include "params/Parameters.h"
#include "params/Units.h"

namespace
{
    using namespace astralay;

    /** The smallest processor that can own the plugin's parameter layout. */
    class LayoutHost final : public juce::AudioProcessor
    {
    public:
        LayoutHost() : state (*this, nullptr, "Astralay", params::createLayout()) {}

        const juce::String getName() const override { return "LayoutHost"; }
        void prepareToPlay (double, int) override {}
        void releaseResources() override {}
        void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
        double getTailLengthSeconds() const override { return 0.0; }
        bool acceptsMidi() const override { return false; }
        bool producesMidi() const override { return false; }
        juce::AudioProcessorEditor* createEditor() override { return nullptr; }
        bool hasEditor() const override { return false; }
        int getNumPrograms() override { return 1; }
        int getCurrentProgram() override { return 0; }
        void setCurrentProgram (int) override {}
        const juce::String getProgramName (int) override { return {}; }
        void changeProgramName (int, const juce::String&) override {}
        void getStateInformation (juce::MemoryBlock&) override {}
        void setStateInformation (const void*, int) override {}

        juce::AudioProcessorValueTreeState state;
    };
}

class ParameterTests final : public juce::UnitTest
{
public:
    ParameterTests() : juce::UnitTest ("Parameters", "Astralay") {}

    void runTest() override
    {
        beginTest ("Note values are ordered and measured correctly");
        {
            const auto& all = NoteValues::all();
            expect (all.size() > 20);

            for (int i = 1; i < all.size(); ++i)
                expect (all[i - 1].lengthInQuarters (4.0) <= all[i].lengthInQuarters (4.0));

            expectEquals (all.getLast().getLabel(), juce::String ("4 bars"));

            const auto quarter = NoteValues::indexOf ("1/4");
            expect (quarter >= 0);
            expectWithinAbsoluteError (all[quarter].lengthInQuarters (4.0), 1.0, 1.0e-9);

            // In 6/8 a bar is three quarter notes long.
            const auto bar = NoteValues::indexOf ("1 bar");
            expectWithinAbsoluteError (all[bar].lengthInQuarters (3.0), 3.0, 1.0e-9);
        }

        beginTest ("Typed note values are parsed");
        {
            const auto label = [] (const char* text)
            {
                const auto index = NoteValues::parse (text);
                return index >= 0 ? NoteValues::all()[index].getLabel() : juce::String ("invalid");
            };

            expectEquals (label ("1/8"), juce::String ("1/8"));
            expectEquals (label ("1/8d"), juce::String ("1/8 dotted"));
            expectEquals (label ("1/16 T"), juce::String ("1/16 triplet"));
            expectEquals (label ("1/64"), juce::String ("1/64"));
            expectEquals (label ("4 bars"), juce::String ("4 bars"));
            expectEquals (label ("1 bar"), juce::String ("1 bar"));
            expectEquals (label ("2 bars dotted"), juce::String ("2 bars dotted"));
            expectEquals (label ("4 bars dotted"), juce::String ("invalid"));
            expectEquals (label ("1/3"), juce::String ("invalid"));
            expectEquals (label ("banana"), juce::String ("invalid"));
        }

        beginTest ("Values are formatted readably");
        {
            expectEquals (Units::format (Unit::milliseconds, 500.0f), juce::String ("500 ms"));
            expectEquals (Units::format (Unit::milliseconds, 1500.0f), juce::String ("1.5 s"));
            expectEquals (Units::format (Unit::decibels, -60.0f), juce::String ("-inf dB"));
            expectEquals (Units::format (Unit::decibels, -3.0f), juce::String ("-3.0 dB"));
            expectEquals (Units::format (Unit::pan, 0.0f), juce::String ("centre"));
            expectEquals (Units::format (Unit::pan, -35.0f), juce::String ("35 left"));
            expectEquals (Units::format (Unit::hertz, 1200.0f), juce::String ("1.2 kHz"));
            expectEquals (Units::format (Unit::semitones, 7.0f), juce::String ("+7 semitones"));
        }

        beginTest ("Typed values are parsed with optional units");
        {
            const auto parsed = [] (Unit unit, const char* text) { return Units::parse (unit, text); };

            expectEquals (*parsed (Unit::milliseconds, "250"), 250.0f);
            expectEquals (*parsed (Unit::milliseconds, "1.5 s"), 1500.0f);
            expectEquals (*parsed (Unit::milliseconds, "40ms"), 40.0f);
            expectEquals (*parsed (Unit::hertz, "2kHz"), 2000.0f);
            expectEquals (*parsed (Unit::decibels, "-inf"), params::volumeFloorDb);
            expectEquals (*parsed (Unit::decibels, "-6 dB"), -6.0f);
            expectEquals (*parsed (Unit::pan, "30L"), -30.0f);
            expectEquals (*parsed (Unit::pan, "30 right"), 30.0f);
            expectEquals (*parsed (Unit::pan, "C"), 0.0f);
            expectEquals (*parsed (Unit::semitones, "-5st"), -5.0f);

            expect (! parsed (Unit::milliseconds, "abc").has_value());
            expect (! parsed (Unit::milliseconds, "5 Hz").has_value());
            expect (! parsed (Unit::percent, "").has_value());
        }

        beginTest ("The layout has every parameter, with unique IDs and tap-numbered names");
        {
            LayoutHost host;
            const auto& parameters = host.getParameters();

            constexpr int perTap = 44;
            constexpr int global = 16;
            expectEquals (parameters.size(), params::numTaps * perTap + global + params::numMacros);

            std::set<juce::String> ids;

            for (auto* p : parameters)
                if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (p))
                    ids.insert (withId->paramID);

            expectEquals ((int) ids.size(), parameters.size());

            auto* feedback = host.state.getParameter (params::tapId (2, params::tap::feedback));
            expect (feedback != nullptr);
            expectEquals (feedback->getName (100), juce::String ("Tap 3 Feedback"));

            auto* stutter = host.state.getParameter (params::tapId (15, params::tap::stutterProb));
            expect (stutter != nullptr);
            expectEquals (stutter->getName (100), juce::String ("Tap 16 Stutter Probability"));

            // Only tap 1 starts enabled.
            expect (host.state.getRawParameterValue (params::tapId (0, params::tap::enabled))->load() > 0.5f);
            expect (host.state.getRawParameterValue (params::tapId (1, params::tap::enabled))->load() < 0.5f);
        }

        beginTest ("Defaults match the spec");
        {
            LayoutHost host;
            const auto value = [&host] (const juce::String& id) { return host.state.getRawParameterValue (id)->load(); };
            const auto choice = [&host] (const juce::String& id)
            {
                return host.state.getParameter (id)->getCurrentValueAsText();
            };

            expectWithinAbsoluteError (value (params::tapId (0, params::tap::time)), 500.0f, 1.0e-3f);
            expectEquals (choice (params::tapId (0, params::tap::timeSync)), juce::String ("1/4"));
            expectWithinAbsoluteError (value (params::tapId (0, params::tap::feedback)), 40.0f, 1.0e-3f);
            expectWithinAbsoluteError (value (params::global::outputGain), -3.0f, 1.0e-3f);
            expectWithinAbsoluteError (value (params::global::smearAmount), 10.0f, 1.0e-3f);
            expectWithinAbsoluteError (value (params::global::smearSize), 200.0f, 1.0e-3f);
            expectEquals (value (params::global::freezeSustain), 0.0f);
            expectEquals (host.state.getParameter (params::global::freezeSustain)->getParameterIndex(), host.getParameters().size() - 1);
            expectEquals (host.state.getParameter (params::macroId (0))->getParameterIndex(), params::numTaps * 44 + 15);
            expectEquals (choice (params::global::bufferSync), juce::String ("1/16"));
            expectEquals (choice (params::global::placement), juce::String ("Feedback path"));

            // The +18 dBFS ceiling sits just under it; 0 dBFS is exact; off doesn't clip.
            const auto plus18 = juce::Decibels::decibelsToGain (18.0f);
            const auto ceiling = params::outputClipCeiling (params::OutputClip::plus18);
            expect (ceiling < plus18 && ceiling > plus18 * 0.999f);
            expectEquals (params::outputClipCeiling (params::OutputClip::zero), 1.0f);
            expectEquals (params::outputClipCeiling (params::OutputClip::off), 0.0f);
        }
    }
};

static ParameterTests parameterTests;
