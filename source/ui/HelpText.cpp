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
        { helpKeys::save,       "Saves a preset." },
        { helpKeys::load,       "Loads a preset you made or one of the factory ones." },
        { helpKeys::presetName, "The name of the current preset." },

        // Tap basics
        { helpKeys::tapSelector, "Selects a tap to work with; the remaining controls in this group all apply to the selected tap." },
        { tap::enabled,  "Turns this tap on or off. A tap that is off makes no sound." },
        { tap::time,     "How long after the input this tap repeats." },
        { tap::timeSync, "How long after the input this tap repeats, as a note value, following the host's tempo." },
        { tap::volume,   "How loud this tap's repeats are." },
        { tap::pan,      "Where this tap sits in the stereo field." },
        { tap::feedback, "How much of each repeat is fed back into this tap to repeat again." },
        { tap::lowCut,   "Removes low frequencies from the repeats; cumulative." },
        { tap::highCut,  "Removes high frequencies from the repeats; cumulative." },

        // Glitches
        { tap::reverseProb, "The chance, per chunk, that this tap plays backwards for the length of the glitch." },

        { tap::stutterProb,    "The chance, per chunk, that this tap stutters by repeating a short slice." },
        { tap::stutterMin,     "The shortest slice a stutter can repeat." },
        { tap::stutterMax,     "The longest slice a stutter can repeat." },
        { tap::stutterSyncMin, "The shortest slice a stutter can repeat, as a note value." },
        { tap::stutterSyncMax, "The longest slice a stutter can repeat, as a note value." },

        { tap::grainProb,    "The chance, per chunk, that this tap breaks into grains." },
        { tap::grainSizeMin, "The shortest possible grain." },
        { tap::grainSizeMax, "The longest possible grain." },
        { tap::grainDensMin, "The fewest possible number of grains per second." },
        { tap::grainDensMax, "The most possible number of grains per second." },

        { tap::pitchProb, "The chance, per chunk, that this tap shifts in pitch." },
        { tap::pitchMin,  "The lowest the pitch goes. A sweep turns back when it gets here." },
        { tap::pitchMax,  "The highest the pitch goes. A sweep turns back when it gets here." },
        { tap::pitchSpeedMin, "The slowest a sweep moves, for each pass through the tap. Varispeed doesn't use it." },
        { tap::pitchSpeedMax, "The fastest a sweep moves, for each pass through the tap. Varispeed doesn't use it." },

        { tap::lpcProb, "The chance, per chunk, that this tap's formants shift using linear prediction, a grittier, more robotic sound." },
        { tap::lpcMin,  "The lowest possible formant shift." },
        { tap::lpcMax,  "The highest possible formant shift." },

        { tap::cepsProb, "The chance, per chunk, that this tap's formants shift using the cepstrum, a smoother sound." },
        { tap::cepsMin,  "The lowest possible formant shift." },
        { tap::cepsMax,  "The highest possible formant shift." },

        { tap::ringProb, "The chance, per chunk, that this tap is ring modulated." },
        { tap::ringMin,  "The lowest possible ring modulation frequency." },
        { tap::ringMax,  "The highest possible ring modulation frequency." },

        { tap::fmProb,     "The chance, per chunk, that this tap is frequency modulated." },
        { tap::fmRatioMin, "The lowest possible modulator ratio." },
        { tap::fmRatioMax, "The highest possible modulator ratio." },
        { tap::fmIndexMin, "The lowest possible modulation index. Higher is harsher." },
        { tap::fmIndexMax, "The highest possible modulation index. Higher is harsher." },

        { tap::crushProb,    "The chance, per chunk, that this tap is bit crushed." },
        { tap::crushBitsMin, "The fewest possible bits, fewer bits is harsher." },
        { tap::crushBitsMax, "The most possible bits." },
        { tap::crushRateMin, "The least possible sample-rate reduction, 1x being none." },
        { tap::crushRateMax, "The most possible sample-rate reduction, 1x being none." },

        // Global: timing
        { global::sync,   "Follows the host's tempo, so times are set as note values instead of milliseconds." },
        { global::glide,  "How long a tap takes to slide to a new time, bending the pitch like tape." },
        { global::freeze, "Holds the current repeats forever. New input is not added, but glitches keep firing." },
        { global::freezeSustain, "While frozen, restores some saved loop audio when glitches wear the repeats down. Off by default. Turning it on during a freeze saves what remains." },

        // Global: glitch engine
        { global::placement,    "Feedback path glitches only what is fed back, so the first repeat is clean. Output and feedback glitches what you hear from the first repeat." },
        { global::bufferSize,   "The length of a chunk. Glitches can start at each chunk and last a whole number of chunks." },
        { global::bufferSync,   "The length of a chunk as a note value. Glitches can start at each chunk and last a whole number of chunks." },
        { global::maxGlitches,  "The most glitches that can run at once on each tap." },
        { global::lengthMin,    "The fewest chunks a glitch lasts." },
        { global::lengthMax,    "The most chunks a glitch lasts." },
        { global::reproducible, "Makes glitches repeat identically every time playback starts, using the seed." },
        { global::seed,         "Picks the pattern of glitches when reproducible randomness is on. Has no effect when it is off." },

        // Global: output
        { global::smearAmount, "How much the combined repeats are smeared and diffused." },
        { global::smearSize,   "The size of the smear, higher values exaggerate the effect." },
        { global::mix,         "The balance between the dry input and the repeats. 0% is dry only, 100% is repeats only." },
        { global::outputGain,  "The overall output level." },

        // Macros
        { helpKeys::macroArm,   "While a macro is armed, adjusting another control sets how far the macro moves that control instead of changing it." },
        { helpKeys::macroValue, "Moves every control this macro is set up to move." },
        { helpKeys::modulationAmount, "How far the armed macro moves this control when the macro's value is 1. Setting it to 0 removes it." },

        // Performance
        { helpKeys::performance, "Provides additional functionality for live performance." },
    };

    const auto it = text.find (key);
    return it != text.end() ? it->second : juce::String();
}

} // namespace astralay::ui
