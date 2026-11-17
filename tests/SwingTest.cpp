#include "../Source/PluginProcessor.h"
#include <iostream>
static int fails = 0;
static void check (bool ok, const juce::String& m) { std::cout << (ok ? "PASS " : "FAIL ") << m << "\n"; if (! ok) ++fails; }
static void set (VegaSwingProcessor& p, const char* id, float v) { auto* q = p.apvts.getParameter (id); q->setValueNotifyingHost (q->convertTo0to1 (v)); }
const double fs = 48000; const int blk = 480;
struct Head : juce::AudioPlayHead { double ppq = 0; juce::Optional<PositionInfo> getPosition() const override { PositionInfo i; i.setBpm (120); i.setPpqPosition (ppq); i.setIsPlaying (true); return i; } };
// a short click (decaying 2 kHz burst) on every 16th note (6000 samples at 120 BPM)
static float clicks (int i) { const int t = i % 6000; return t < 400 ? 0.8f * std::exp (-t / 80.0f) * (float) std::sin (2 * juce::MathConstants<double>::pi * 2000 * t / fs) : 0.0f; }
static std::vector<float> run (VegaSwingProcessor& p, Head& h, int n, std::function<float (int)> gen)
{
    std::vector<float> out; juce::AudioBuffer<float> b (2, blk); juce::MidiBuffer m;
    for (int pos = 0; pos < n; pos += blk)
    {
        h.ppq = pos / 24000.0;
        for (int i = 0; i < blk; ++i) { const float x = gen (pos + i); b.setSample (0, i, x); b.setSample (1, i, x); }
        p.processBlock (b, m);
        for (int i = 0; i < blk; ++i) out.push_back (b.getSample (0, i));
    }
    return out;
}
static std::vector<int> onsets (const std::vector<float>& o, int lat)
{
    std::vector<int> r; bool armed = true; int quiet = 0;
    for (int i = lat; i < (int) o.size(); ++i)
    {
        const float a = std::abs (o[(size_t) i]);
        if (armed && a > 0.2f) { r.push_back (i - lat); armed = false; quiet = 0; }
        if (a < 0.01f) { if (++quiet > 800) armed = true; } else quiet = 0;
    }
    return r;
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    Head head;
    auto make = [&] { auto* p = new VegaSwingProcessor(); p->setPlayConfigDetails (2, 2, fs, blk); p->prepareToPlay (fs, blk); p->setPlayHead (&head); return std::unique_ptr<VegaSwingProcessor> (p); };
    check (std::abs (VegaSwingProcessor::warp (1.32, 0.66) - 1.0) < 1e-9 && std::abs (VegaSwingProcessor::warp (1.0, 0.5) - 1.0) < 1e-9 && VegaSwingProcessor::warp (2.0, 0.66) > 1.999,
           "warp map: the off-beat lands at 2 x swing");
    {
        auto p = make(); set (*p, "swing", 50);
        auto o = run (*p, head, 96000, clicks); auto on = onsets (o, p->getLatency());
        bool ok = on.size() >= 12; juce::String s;
        for (size_t k = 1; k < std::min<size_t> (on.size(), 14); ++k) { s << on[k] << " "; ok &= std::abs (on[k] - (int) k * 6000) < 60; }
        check (ok, "50% swing = straight timing (latency " + juce::String (p->getLatency()) + "): " + s);
    }
    {
        auto p = make(); set (*p, "swing", 66.67f);
        auto o = run (*p, head, 96000, clicks); auto on = onsets (o, p->getLatency());
        bool ok = on.size() >= 12; juce::String s;
        for (size_t k = 2; k < std::min<size_t> (on.size(), 14); ++k)
        {
            const int expect = (int) (k / 2) * 12000 + ((k & 1) ? 8000 : 0);    // triplet: off-beat at 2/3 of the pair
            s << on[k] << " ";
            ok &= std::abs (on[k] - expect) < 120;
        }
        check (ok, "triplet swing moves every off-beat 16th to 2/3: " + s);
    }
    {   // pitch is preserved
        auto p = make(); set (*p, "swing", 70);
        auto o = run (*p, head, 96000, [] (int i) { return 0.5f * (float) std::sin (2 * juce::MathConstants<double>::pi * 1000 * i / fs); });
        int z = 0; for (int i = 48001; i < 96000; ++i) z += (o[(size_t) i - 1] < 0) != (o[(size_t) i] < 0);
        const double f = z / 2.0;
        double e = 0; for (int i = 48000; i < 96000; ++i) e += o[(size_t) i] * o[(size_t) i];
        check (std::abs (f - 1000) < 15, "pitch stays at 1 kHz while swinging (" + juce::String (f, 1) + " Hz)");
        check (std::abs (10 * std::log10 (e / 48000 / 0.125)) < 1.5, "level stays steady (" + juce::String (10 * std::log10 (e / 48000 / 0.125), 2) + " dB)");
    }
    {   // wonky: off-beats vary from pair to pair
        auto p = make(); set (*p, "swing", 60); set (*p, "wonky", 100);
        auto o = run (*p, head, 96000 * 2, clicks); auto on = onsets (o, p->getLatency());
        std::vector<int> offs; for (size_t k = 3; k < on.size(); k += 2) offs.push_back (on[k] - (int) (k / 2) * 12000);
        int mn = 1 << 30, mx = 0; for (int x : offs) { mn = std::min (mn, x); mx = std::max (mx, x); }
        check (offs.size() > 6 && mx - mn > 300, "wonky varies the off-beat (" + juce::String (mn) + ".." + juce::String (mx) + ")");
    }
    {   // lay back
        auto p = make(); set (*p, "swing", 50); set (*p, "feel", 10);
        auto o = run (*p, head, 48000, clicks); auto on = onsets (o, p->getLatency());
        check (on.size() > 3 && std::abs (on[2] - 12000 - 480) < 40, "lay back 10 ms delays every hit by 480 samples (" + juce::String (on.size() > 2 ? on[2] - 12000 : -1) + ")");
    }
    std::cout << (fails == 0 ? "ALL PASSED" : "FAILURES: " + std::to_string (fails)) << "\n";
    return fails == 0 ? 0 : 1;
}
