#include <iostream>
#include <juce_events/juce_events.h>

namespace
{
    /** JUCE's default runner logs to the debugger on Windows; print to the console instead. */
    class ConsoleRunner final : public juce::UnitTestRunner
    {
        void logMessage (const juce::String& message) override
        {
            std::cout << message << std::endl;
        }
    };
}

void runBenchmark();

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juce;

    if (argc > 1 && juce::String (argv[1]) == "--benchmark")
    {
        runBenchmark();
        return 0;
    }

    ConsoleRunner runner;
    runner.setAssertOnFailure (false);
    runner.runTestsInCategory ("Astralay");

    int failures = 0;
    int passes = 0;

    for (int i = 0; i < runner.getNumResults(); ++i)
    {
        if (const auto* result = runner.getResult (i))
        {
            failures += result->failures;
            passes += result->passes;
        }
    }

    std::cout << "\n" << passes << " checks passed, " << failures << " failed." << std::endl;
    return failures > 0 ? 1 : 0;
}
