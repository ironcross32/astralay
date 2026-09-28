#include "HelpText.h"
#include "params/Parameters.h"

namespace astralay::ui
{

juce::String helpFor (const juce::String& key)
{
    using namespace params;

    static const std::map<juce::String, juce::String> text
    {
        // Main
        { helpKeys::undo,       "Undoes the last change." },
        { helpKeys::redo,       "Redoes the last change that was undone." },
        { helpKeys::save,       "Saves the current settings as a preset file." },
        { helpKeys::load,       "Loads a factory preset or a preset file." },
        { helpKeys::presetName, "The name of the current preset. It says modified when the settings have changed since it was loaded or saved." },

        // Tap basics
        { helpKeys::tapSelector, "Chooses which of the 16 taps the controls in this group edit. Each entry says whether that tap is on." },
        { tap::enabled,  "Turns this tap on or off. A tap that is off makes no sound." },
        { tap::time,     "How long after the input this tap repeats, from 1 millisecond to 5 seconds." },
        { tap::timeSync, "How long after the input this tap repeats, as a note value from 1/64 triplet to 4 bars, following the host's tempo." },
        { tap::volume,   "How loud this tap's repeats are, from silent to plus 6 dB." },
        { tap::pan,      "Where this tap sits in the stereo field, from 100 left to 100 right." },
        { tap::feedback, "How much of each repeat is fed back to repeat again. At 100% the repeats never fade." },
        { tap::lowCut,   "Removes low frequencies from the repeats, a little more on each pass. Ranges from 20 Hz, which is effectively off, to 2 kHz." },
        { tap::highCut,  "Removes high frequencies from the repeats, a little more on each pass. Ranges from 1 kHz to 20 kHz, which is effectively off." },

        // Glitches
        { tap::reverseProb, "The chance, per chunk, that this tap plays backwards for the length of the glitch." },

        { tap::stutterProb,    "The chance, per chunk, that this tap stutters by repeating a short slice." },
        { tap::stutterMin,     "The shortest slice a stutter can repeat, from 5 to 250 milliseconds." },
        { tap::stutterMax,     "The longest slice a stutter can repeat, from 5 to 250 milliseconds." },
        { tap::stutterSyncMin, "The shortest slice a stutter can repeat, as a note value from 1/64 triplet to 1/4." },
        { tap::stutterSyncMax, "The longest slice a stutter can repeat, as a note value from 1/64 triplet to 1/4." },

        { tap::grainProb,    "The chance, per chunk, that this tap breaks into grains." },
        { tap::grainSizeMin, "The shortest grain, from 5 to 200 milliseconds." },
        { tap::grainSizeMax, "The longest grain, from 5 to 200 milliseconds." },
        { tap::grainDensMin, "The fewest grains per second, from 1 to 100." },
        { tap::grainDensMax, "The most grains per second, from 1 to 100." },

        { tap::pitchProb, "The chance, per chunk, that this tap shifts in pitch." },
        { tap::pitchMin,  "The lowest pitch shift, from minus 24 to plus 24 semitones." },
        { tap::pitchMax,  "The highest pitch shift, from minus 24 to plus 24 semitones." },

        { tap::lpcProb, "The chance, per chunk, that this tap's formants shift using linear prediction, a grittier, more robotic sound." },
        { tap::lpcMin,  "The lowest formant shift, from minus 12 to plus 12 semitones." },
        { tap::lpcMax,  "The highest formant shift, from minus 12 to plus 12 semitones." },

        { tap::cepsProb, "The chance, per chunk, that this tap's formants shift using the cepstrum, a smoother sound." },
        { tap::cepsMin,  "The lowest formant shift, from minus 12 to plus 12 semitones." },
        { tap::cepsMax,  "The highest formant shift, from minus 12 to plus 12 semitones." },

        { tap::ringProb, "The chance, per chunk, that this tap is ring modulated." },
        { tap::ringMin,  "The lowest ring modulation frequency, from 1 Hz to 5 kHz." },
        { tap::ringMax,  "The highest ring modulation frequency, from 1 Hz to 5 kHz." },

        { tap::fmProb,     "The chance, per chunk, that this tap is frequency modulated." },
        { tap::fmRatioMin, "The lowest modulator ratio, from 0.25 to 16." },
        { tap::fmRatioMax, "The highest modulator ratio, from 0.25 to 16." },
        { tap::fmIndexMin, "The lowest modulation index, from 0 to 10. Higher is harsher." },
        { tap::fmIndexMax, "The highest modulation index, from 0 to 10. Higher is harsher." },

        { tap::crushProb,    "The chance, per chunk, that this tap is bit crushed." },
        { tap::crushBitsMin, "The fewest bits, from 1 to 16. Fewer bits is harsher." },
        { tap::crushBitsMax, "The most bits, from 1 to 16." },
        { tap::crushRateMin, "The least sample-rate reduction, from 1x, which is none, to 64x." },
        { tap::crushRateMax, "The most sample-rate reduction, from 1x, which is none, to 64x." },

        // Global: timing
        { global::sync,   "Follows the host's tempo, so times are set as note values instead of milliseconds." },
        { global::glide,  "How long a tap takes to slide to a new time, bending the pitch like tape. From 0 to 2 seconds." },
        { global::freeze, "Holds the current repeats forever. New input is not added, but glitches keep firing." },

        // Global: glitch engine
        { global::threshold,    "Scales every glitch probability on every tap. At 0% nothing glitches." },
        { global::placement,    "Feedback path glitches only what is fed back, so the first repeat is clean. Output and feedback glitches what you hear from the first repeat." },
        { global::bufferSize,   "The length of a chunk. Glitches can start at each chunk and last a whole number of chunks. From 10 milliseconds to 2 seconds." },
        { global::bufferSync,   "The length of a chunk as a note value. Glitches can start at each chunk and last a whole number of chunks." },
        { global::maxGlitches,  "The most glitches that can run at once on each tap, from 1 to 4." },
        { global::lengthMin,    "The fewest chunks a glitch lasts, from 1 to 16." },
        { global::lengthMax,    "The most chunks a glitch lasts, from 1 to 16." },
        { global::reproducible, "Makes glitches repeat identically every time playback starts, using the seed." },
        { global::seed,         "Picks the pattern of glitches when reproducible randomness is on. Has no effect when it is off." },

        // Global: output
        { global::smearAmount, "How much the combined repeats are smeared and diffused." },
        { global::smearSize,   "The size of the smear, from 10 to 500 milliseconds." },
        { global::mix,         "The balance between the dry input and the repeats. 0% is dry only, 100% is repeats only." },
        { global::outputGain,  "The overall output level, from minus 24 to plus 12 dB." },
    };

    const auto it = text.find (key);
    return it != text.end() ? it->second : juce::String();
}

} // namespace astralay::ui
