#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

/** Real-time audio swing: re-times straight audio into a swung / triplet / wonky groove without changing pitch.
    The musical-time warp t(tau) is applied with overlapping Hann grains read at normal speed. */
class VegaSwingProcessor : public juce::AudioProcessor
{
public:
    VegaSwingProcessor();
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void prepareToPlay (double, int) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Vega Productions Swing"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.2; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    juce::AudioProcessorValueTreeState apvts;

    /** Output musical position within a pair of grid steps (0..2) -> input position (0..2).
        Hits stay intact: the audio is only stretched in the second half of each step (the tail before the
        next hit) and is played at normal speed, with a constant offset, around every hit. swing in 0.5..0.8 */
    static double warp (double ph, double swing, double g = 0.0)
    {
        // g = one grain length in grid steps: every hit gets that much un-stretched audio in front of it
        const double split = 2.0 * swing;                  // where the off-beat lands (in grid steps)
        if (split <= 1.0 + 1e-9) return ph;
        g = juce::jlimit (0.0, 0.3, g);
        const double a = 0.5 * (1.0 - g);                  // stretch the down-beat's tail from here...
        const double r2 = 0.5 * (2.0 - split);             // ...and squeeze the off-beat's tail over this much
        const double off = split - 1.0;
        if (ph < a) return ph;
        if (ph < split - g) return a + (ph - a) * ((1.0 - g) - a) / ((split - g) - a);
        if (ph < 2.0 - r2 - g) return ph - off;
        if (ph < 2.0 - g) { const double from = 2.0 - r2 - g - off; return from + (ph - (2.0 - r2 - g)) * ((2.0 - g) - from) / r2; }
        return ph;
    }
    int getLatency() const { return latency; }

    std::atomic<float> uiSwing { 0.5f }, uiPhase { 0 }, inL { 0 }, outL { 0 };
    std::atomic<float> uiScale { 1.0f };
    double bpm = 120;

private:
    float readRing (int ch, double pos) const;
    std::vector<float> ring[2]; long long written = 0; int ringN = 0;
    double sr = 48000; int grainLen = 1024, hop = 512, baseDelay = 0, latency = 0;
    struct Grain { double read = 0; int age = 0; bool active = false; };
    std::array<Grain, 4> grains; int hopCount = 0;
    long long lastPair = -1; double pairSwing = 0.5; juce::Random rng;
    float prevDelay = 0;
    std::atomic<float>* p[8] {};
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VegaSwingProcessor)
};
