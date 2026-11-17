#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
enum { kGrid, kSwing, kWonk, kFeel, kGrain, kOut };
const char* ids[] = { "grid", "swing", "wonky", "feel", "smoothness", "output" };
constexpr int numP = 6;
}

juce::AudioProcessorValueTreeState::ParameterLayout VegaSwingProcessor::layout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> v;
    v.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ids[kGrid], 1 }, "Grid", StringArray { "1/8", "1/16", "1/32" }, 1));
    v.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ids[kSwing], 1 }, "Swing", NormalisableRange<float> (50, 75), 58,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float x, int) { return std::abs (x - 66.67f) < 0.3f ? String ("66.7% (triplet)") : String (x, 1) + "%"; })));
    v.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ids[kWonk], 1 }, "Wonky", NormalisableRange<float> (0, 100), 0,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float x, int) { return String (roundToInt (x)) + "%"; })));
    v.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ids[kFeel], 1 }, "Push / Lay Back", NormalisableRange<float> (-25, 25), 0,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float x, int) { return std::abs (x) < 0.1f ? String ("On the grid") : (x < 0 ? "Push " : "Lay back ") + String (std::abs (x), 1) + " ms"; })));
    v.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ids[kGrain], 1 }, "Character", StringArray { "Tight (drums)", "Balanced", "Smooth (pads/vocals)" }, 1));
    v.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ids[kOut], 1 }, "Output", NormalisableRange<float> (-12, 12), 0,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float x, int) { return String (x, 1) + " dB"; })));
    return { v.begin(), v.end() };
}

VegaSwingProcessor::VegaSwingProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true).withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", layout())
{
    for (int i = 0; i < numP; ++i) p[i] = apvts.getRawParameterValue (ids[i]);
}

bool VegaSwingProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto o = l.getMainOutputChannelSet();
    return o == juce::AudioChannelSet::stereo() && (l.getMainInputChannelSet() == o || l.getMainInputChannelSet() == juce::AudioChannelSet::mono());
}

void VegaSwingProcessor::prepareToPlay (double s, int)
{
    sr = s;
    static const int lens[] { 512, 1024, 2048 };
    grainLen = (int) (lens[juce::jlimit (0, 2, (int) p[kGrain]->load())] * s / 48000.0); hop = grainLen / 2;
    baseDelay = (int) (0.025 * s);                      // room for "push"
    latency = grainLen + baseDelay;
    setLatencySamples (latency);
    ringN = (int) (s * 6.0);
    for (auto& r : ring) r.assign ((size_t) ringN, 0.0f);
    written = 0; hopCount = 0; lastPair = -1;
    for (auto& g : grains) g.active = false;
}

float VegaSwingProcessor::readRing (int ch, double pos) const
{
    const auto& b = ring[ch];
    double m = std::fmod (pos, (double) ringN); if (m < 0) m += ringN;
    const int i = (int) m; const float f = (float) (m - i);
    return b[(size_t) i] + (b[(size_t) ((i + 1) % ringN)] - b[(size_t) i]) * f;
}

void VegaSwingProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals nd;
    const int n = buffer.getNumSamples();
    if (n == 0) return;
    if (getTotalNumInputChannels() == 1) buffer.copyFrom (1, 0, buffer, 0, 0, n);
    {   // character changes the grain size (and latency)
        static const int lens[] { 512, 1024, 2048 };
        const int want = (int) (lens[juce::jlimit (0, 2, (int) p[kGrain]->load())] * sr / 48000.0);
        if (want != grainLen) { grainLen = want; hop = grainLen / 2; latency = grainLen + baseDelay; setLatencySamples (latency); for (auto& g : grains) g.active = false; }
    }
    double ppq = 0; bool playing = false;
    if (auto* ph = getPlayHead()) if (auto pos = ph->getPosition())
    {
        if (auto b = pos->getBpm()) bpm = juce::jlimit (20.0, 400.0, *b);
        if (pos->getIsPlaying()) if (auto q = pos->getPpqPosition()) { ppq = *q; playing = true; }
    }
    const double spb = sr * 60.0 / bpm;
    static const double grids[] { 0.5, 0.25, 0.125 };
    const double u = grids[juce::jlimit (0, 2, (int) p[kGrid]->load())];      // grid step in beats
    const double swing = p[kSwing]->load() * 0.01, wonk = p[kWonk]->load() * 0.01;
    const double feel = p[kFeel]->load() * 0.001 * sr;                         // samples, + = later
    const float outG = juce::Decibels::decibelsToGain (p[kOut]->load());
    float* L = buffer.getWritePointer (0); float* R = buffer.getWritePointer (1);
    float pkIn = 0, pkOut = 0;

    for (int i = 0; i < n; ++i)
    {
        ring[0][(size_t) (written % ringN)] = L[i]; ring[1][(size_t) (written % ringN)] = R[i];
        pkIn = std::max (pkIn, std::abs (L[i]));

        // musical position heard at this output sample (the host shifts us back by `latency`)
        double delaySamples = 0;
        if (playing)
        {
            const double mu = ppq + i / spb - latency / spb;
            const double pairs = mu / (2.0 * u);
            const long long pr = (long long) std::floor (pairs);
            if (pr != lastPair)
            {
                lastPair = pr;
                pairSwing = swing;
                if (wonk > 0) pairSwing = juce::jlimit (0.5, 0.8, swing + wonk * (rng.nextDouble() * 0.16 - 0.05));
            }
            const double phase = (pairs - (double) pr) * 2.0;                  // 0..2 grid steps
            const double t = warp (phase, pairSwing, grainLen / (u * spb));
            delaySamples = std::max (0.0, (phase - t) * u * spb);
            uiPhase.store ((float) (phase * 0.5)); uiSwing.store ((float) pairSwing);
        }
        const double totalDelay = latency + juce::jlimit (-(double) baseDelay, (double) baseDelay, feel) + delaySamples;
        const double readHead = (double) written - totalDelay;

        // start a new grain every hop, or right away when the warp jumps (the off-beat landing)
        const bool jump = std::abs ((float) delaySamples - prevDelay) > hop * 0.5f;
        prevDelay = (float) delaySamples;
        if (--hopCount <= 0 || jump)
        {
            hopCount = hop;
            for (auto& g : grains) if (! g.active) { g.active = true; g.age = 0; g.read = readHead; break; }
        }
        float yl = 0, yr = 0, wsum = 0;
        for (auto& g : grains)
        {
            if (! g.active) continue;
            const float w = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * g.age / grainLen);
            yl += readRing (0, g.read) * w; yr += readRing (1, g.read) * w; wsum += w;
            g.read += 1.0;
            if (++g.age >= grainLen) g.active = false;
        }
        if (wsum > 1.0e-3f) { yl /= std::max (1.0f, wsum); yr /= std::max (1.0f, wsum); }
        // normalise only when overlapping windows sum above 1 (keeps steady state at unity)
        L[i] = yl * outG; R[i] = yr * outG;
        pkOut = std::max (pkOut, std::abs (L[i]));
        ++written;
    }
    inL.store (std::max (inL.load(), pkIn)); outL.store (std::max (outL.load(), pkOut));
}

void VegaSwingProcessor::getStateInformation (juce::MemoryBlock& d)
{
    auto s = apvts.copyState(); s.setProperty ("uiScale", uiScale.load(), nullptr);
    if (auto x = s.createXml()) copyXmlToBinary (*x, d);
}
void VegaSwingProcessor::setStateInformation (const void* data, int size)
{
    if (auto x = getXmlFromBinary (data, size))
    {
        auto s = juce::ValueTree::fromXml (*x);
        if (! s.hasType (apvts.state.getType())) return;
        uiScale.store ((float) s.getProperty ("uiScale", 1.0f));
        apvts.replaceState (s);
    }
}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new VegaSwingProcessor(); }
