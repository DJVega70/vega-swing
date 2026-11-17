#include "PluginEditor.h"

namespace
{
// Shows one grid pair: where the beats were (grey) and where they land now (cyan), plus the warp curve
class GrooveView : public juce::Component
{
public:
    explicit GrooveView (VegaSwingProcessor& p) : proc (p) {}
    void paint (juce::Graphics& g) override
    {
        vegaui::drawScreen (g, getLocalBounds().toFloat());
        auto r = getLocalBounds().toFloat().reduced (16.0f, 12.0f);
        auto& a = proc.apvts;
        const double swing = a.getRawParameterValue ("swing")->load() * 0.01;
        static const char* gridNames[] { "8TH", "16TH", "32ND" };
        g.setFont (VegaFonts::ui (10.0f, true)); g.setColour (VegaColours::textDim);
        g.drawText (juce::String ("ONE BEAT OF ") + gridNames[juce::jlimit (0, 2, (int) a.getRawParameterValue ("grid")->load())] + " NOTES  -  GREY = STRAIGHT, CYAN = SWUNG",
                    r.removeFromTop (16).toNearestInt(), juce::Justification::left);
        auto lanes = r.removeFromBottom (70.0f);
        // two pairs side by side
        for (int pair = 0; pair < 2; ++pair)
        {
            const float x0 = lanes.getX() + lanes.getWidth() * pair / 2.0f, w = lanes.getWidth() / 2.0f;
            for (int k = 0; k < 2; ++k)
            {
                const float straightX = x0 + w * k / 2.0f, swungX = x0 + w * (k == 0 ? 0.0f : (float) swing);
                g.setColour (juce::Colour (0xff4a4a4a)); g.fillRoundedRectangle (straightX, lanes.getY(), 10, 26, 2);
                g.setColour (VegaColours::accent); g.fillRoundedRectangle (swungX, lanes.getY() + 36, 10, 26, 2);
            }
        }
        // warp curve
        auto c = r.reduced (0, 6);
        juce::Path path;
        for (int i = 0; i <= 200; ++i)
        {
            const double ph = 2.0 * i / 200.0, t = VegaSwingProcessor::warp (ph, swing);
            const juce::Point<float> pt (c.getX() + c.getWidth() * (float) (ph / 2.0), c.getBottom() - c.getHeight() * (float) (t / 2.0));
            if (i == 0) path.startNewSubPath (pt); else path.lineTo (pt);
        }
        g.setColour (juce::Colour (0xff2c2c2c)); g.drawLine (c.getX(), c.getBottom(), c.getRight(), c.getY(), 1.0f);
        g.setColour (VegaColours::accent); g.strokePath (path, juce::PathStrokeType (2.0f));
        const float ph = proc.uiPhase.load();
        g.setColour (VegaColours::gold); g.fillRect (c.getX() + c.getWidth() * ph - 1, c.getY(), 2.0f, c.getHeight());
        g.setColour (VegaColours::textDim); g.setFont (VegaFonts::ui (9.0f));
        g.drawText ("output time", c.toNearestInt().removeFromBottom (12), juce::Justification::right);
        g.drawText ("audio time", c.toNearestInt().removeFromTop (12), juce::Justification::left);
    }
private:
    VegaSwingProcessor& proc;
};
}

class VegaSwingEditor::Main : public juce::Component
{
public:
    explicit Main (VegaSwingProcessor& p) : proc (p), a (p.apvts), view (p),
        swing (a, "swing", "SWING", true), grid (a, "grid", "Grid"), wonky (a, "wonky", "Wonky"), feel (a, "feel", "Push / Lay Back"),
        character (a, "smoothness", "Character"), output (a, "output", "Output")
    {
        for (auto* c : std::initializer_list<juce::Component*> { &view, &swing, &grid, &wonky, &feel, &character, &output }) addAndMakeVisible (c);
        const char* names[] { "STRAIGHT", "MPC 54", "MPC 58", "MPC 62", "TRIPLET", "HARD 71" };
        const float vals[] { 50.0f, 54.0f, 58.0f, 62.0f, 66.67f, 71.0f };
        for (int i = 0; i < 6; ++i)
        {
            auto* b = presets.add (new juce::TextButton (names[i]));
            b->onClick = [this, v = vals[i]] { if (auto* q = a.getParameter ("swing")) { q->beginChangeGesture(); q->setValueNotifyingHost (q->convertTo0to1 (v)); q->endChangeGesture(); } };
            addAndMakeVisible (b);
        }
    }
    void refresh() { view.repaint(); }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (VegaColours::background);
        vegaui::drawTopBar (g, W, "SWING  VP-22");
        vegaui::drawBody (g, { 0, 35, W, H - 65 });
        vegaui::drawSection (g, { 0, 290, 300, 210 }, "AMOUNT", false);
        vegaui::drawSection (g, { 300, 290, 460, 210 }, "GROOVE");
        vegaui::drawSection (g, { 760, 290, W - 760, 210 }, "OUTPUT");
        vegaui::drawBottomBar (g, { 0, H - 30, W, 30 }, "PUT IT ON STRAIGHT DRUM LOOPS, HATS OR CHOPS  -  IT FOLLOWS YOUR PROJECT TEMPO (PRESS PLAY)");
    }
    void resized() override
    {
        view.setBounds (12, 44, W - 24, 196);
        int x = 12; for (auto* b : presets) { b->setBounds (x, 248, (W - 24) / 6 - 6, 32); x += (W - 24) / 6; }
        swing.setBounds (70, 310, 160, 180);
        grid.setBounds (316, 330, 130, 46); character.setBounds (316, 410, 200, 46);
        wonky.setBounds (530, 310, 100, 112); feel.setBounds (640, 310, 110, 112);
        output.setBounds (790, 320, 100, 112);
    }
private:
    VegaSwingProcessor& proc; juce::AudioProcessorValueTreeState& a;
    GrooveView view; vegaui::Knob swing; vegaui::Choice grid; vegaui::Knob wonky, feel; vegaui::Choice character; vegaui::Knob output;
    juce::OwnedArray<juce::TextButton> presets;
};

VegaSwingEditor::VegaSwingEditor (VegaSwingProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    main = std::make_unique<Main> (p);
    addAndMakeVisible (*main);
    constrainer.setFixedAspectRatio ((double) W / H);
    constrainer.setSizeLimits (W / 2, H / 2, W * 2, H * 2);
    setConstrainer (&constrainer);
    setResizable (true, true);
    const float s = juce::jlimit (0.5f, 2.0f, proc.uiScale.load());
    setSize (juce::roundToInt (W * s), juce::roundToInt (H * s));
    startTimerHz (30);
}
VegaSwingEditor::~VegaSwingEditor() { stopTimer(); setConstrainer (nullptr); main.reset(); setLookAndFeel (nullptr); }
void VegaSwingEditor::timerCallback() { main->refresh(); }
void VegaSwingEditor::resized()
{
    main->setBounds (0, 0, W, H);
    const float s = (float) getWidth() / (float) W;
    main->setTransform (juce::AffineTransform::scale (s));
    proc.uiScale.store (s);
}
juce::AudioProcessorEditor* VegaSwingProcessor::createEditor() { return new VegaSwingEditor (*this); }
