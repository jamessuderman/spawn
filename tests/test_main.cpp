// Spawn — test runner entry point.
// JUCE's juce::UnitTest auto-registers each test as a static
// instance. We collect them at startup, run them on the message
// thread (UnitTest assumes a MessageManager is alive), print the
// results, and exit with 0 on success / 1 on failure.
//
// We deliberately use a plain console main here instead of going
// through juce::JUCEApplication because the application framework
// wants to own the lifecycle; we just need to run tests and exit.

#include <JuceHeader.h>

#include <iostream>

int main(int /*argc*/, char* /*argv*/[])
{
    juce::ScopedJuceInitialiser_GUI init;

    juce::UnitTestRunner runner;
    runner.setAssertOnFailure(false);
    runner.runAllTests();

    int failures = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
    {
        const auto* r = runner.getResult(i);
        const juce::String label = r->unitTestName + " / " + r->subcategoryName;
        if (r->failures > 0)
        {
            ++failures;
            std::cout << "FAIL: " << label
                      << " (" << r->failures << " failures, "
                      << r->messages.size() << " messages)\n";
            for (const auto& msg : r->messages)
                std::cout << "  " << msg << "\n";
        }
        else
        {
            std::cout << "PASS: " << label
                      << " (" << r->messages.size() << " messages)\n";
        }
    }

    if (failures > 0)
    {
        std::cout << failures << " test(s) failed.\n";
        return 1;
    }
    std::cout << "All tests passed.\n";
    return 0;
}
