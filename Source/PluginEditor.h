#pragma once
#include "PluginProcessor.h"
#include "VegaUI.h"

class VegaSwingEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit VegaSwingEditor (VegaSwingProcessor&);
    ~VegaSwingEditor() override;
    void paint (juce::Graphics&) override {}
    void resized() override;
    static constexpr int W = 920, H = 530;
    class Main;
private:
    void timerCallback() override;
    VegaSwingProcessor& proc;
    VegaLookAndFeel lnf;
    std::unique_ptr<Main> main;
    juce::ComponentBoundsConstrainer constrainer;
};
