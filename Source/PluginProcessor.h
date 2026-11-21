#pragma once
#include <array>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "Dsp.h"
#include "Limiter.h"

namespace ids
{
    constexpr const char* in       = "in";
    constexpr const char* specOn   = "specOn";
    constexpr const char* spec     = "spec";
    constexpr const char* compOn   = "compOn";
    constexpr const char* glue     = "glue";
    constexpr const char* detail   = "detail";
    constexpr const char* compOut  = "compOut";
    constexpr const char* analogOn = "analogOn";
    constexpr const char* sat      = "sat";
    constexpr const char* stereoOn = "stereoOn";
    constexpr const char* width    = "width";
    constexpr const char* bassOn   = "bassOn";
    constexpr const char* bassHz   = "bassHz";
    constexpr const char* eqOn     = "eqOn";
    constexpr const char* low      = "low";
    constexpr const char* high     = "high";
    constexpr const char* clipOn   = "clipOn";
    constexpr const char* clip     = "clip";
    constexpr const char* limOn    = "limOn";
    constexpr const char* push     = "push";
    constexpr const char* tp       = "tp";
    constexpr const char* bypass   = "bypass";
    constexpr const char* target   = "target";
    constexpr const char* monitor  = "monitor";
}

struct Meters
{
    std::atomic<float> inPeakL { 0.f }, inPeakR { 0.f }, outPeakL { 0.f }, outPeakR { 0.f };
    std::array<std::atomic<float>, 3> bandGR {};          // dB positifs (L/M/H)
    std::atomic<float> detailUp { 0.f }, clipRed { 0.f }, limGR { 0.f }, analogLvl { 0.f };
    std::array<std::atomic<float>, kSpecBands> specGain {}; // dB appliqués par le Spectral
    std::atomic<float> correlation { 1.f };
    std::atomic<float> lufsM { -120.f }, lufsS { -120.f }, lufsI { -120.f }, psr { -99.f };
    std::atomic<bool>  resetI { false };
    std::array<std::atomic<float>, 4096> scope {};         // sortie mono, pour l'analyseur de spectre
    std::atomic<int> scopeW { 0 };
};

class MasterHeadProcessor : public juce::AudioProcessor,
                            private juce::AsyncUpdater,
                            private juce::AudioProcessorParameter::Listener
{
public:
    MasterHeadProcessor();
    ~MasterHeadProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "MasterHead"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    juce::AudioProcessorValueTreeState apvts;
    Meters meters;
    double getSr() const { return sr; }
    int editorW = 0, editorH = 0;   // taille de fenêtre mémorisée (thread message uniquement)
    void resetIntegrated() { meters.resetI.store (true); }

    // ---- A/B
    int  getActiveSlot() const { return activeSlot; }
    void selectSlot (int s);
    void copyToOtherSlot();

    // ---- Assist
    enum AssistState { Idle = 0, Analyzing = 1, Done = 2, NoData = 3 };
    void requestAssist()  { assistCmd.store (1); }
    void cancelAssist()   { assistCmd.store (2); }
    int   getAssistState() const    { return assistState.load(); }
    float getAssistProgress() const { return assistProgress.load(); }
    juce::String getAssistSummary() const { return assistSummary; }

    // ---- Undo / Redo
    bool canUndo() const { return ! undoStack.empty(); }
    bool canRedo() const { return ! redoStack.empty(); }
    void undo();
    void redo();

private:
    void handleAsyncUpdate() override;
    void parameterValueChanged (int, float) override {}
    void parameterGestureChanged (int, bool) override;
    using Snapshot = std::vector<float>;
    Snapshot takeSnapshot() const;
    void applySnapshot (const Snapshot&);
    void pushUndo (const Snapshot& before);
    void applyAssistParams();
    void analyzeChunk (const float* L, const float* R, int n);
    void finishAnalysis();
    void addHopBlock();

    void processChunk (juce::AudioBuffer<float>&);
    void updateFilters (bool force);

    struct Prm
    {
        std::atomic<float> *in, *specOn, *spec, *compOn, *glue, *detail, *analogOn, *sat, *stereoOn, *width,
                           *bassOn, *bassHz, *eqOn, *low, *high, *clipOn, *clip, *limOn, *push, *tp,
                           *bypass, *target, *monitor, *compOut;
    } prm {};

    double sr = 44100.0;
    int maxBlock = 512;

    // Spectral (EQ dynamique 6 bandes, linké stéréo)
    Biquad specBP[kSpecBands], specEQ[2][kSpecBands];
    float specEnv[kSpecBands] {}, specGainDb[kSpecBands] {}, specApplied[kSpecBands] {};
    float specEnvCoef = 0.f;

    // Comp 3 bandes + Detail
    Crossover3 xo[2];
    float bFast[3] {}, bMed[3] {}, bSlow[3] {}, bGr[3] {}, bUp[3] {};
    float cPe = 0.f, cPm = 0.f, cSlowAtk = 0.f, cSlowRel = 0.f, cAtk[3] {}, cRel[3] {}, cUpAtk = 0.f, cUpRel = 0.f;
    float makeupDb = 0.f, cMk = 0.f;   // Comp Out : compensation automatique du gain de réduction

    // EQ (Bass / Air)
    juce::dsp::IIR::Filter<float> lowShelf[2], highShelf[2];
    float lastLow = 1000.f, lastHigh = 1000.f;

    // Stereo
    juce::dsp::IIR::Filter<float> bassLP;
    float lastBassHz = -1.f;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> inGain, widthSm;

    // Analog + Clip en 4x
    juce::dsp::Oversampling<float> os;

    // Limiteur
    LookaheadLimiter limiter;

    // A/B
    Snapshot slots[2];
    int activeSlot = 0;

    // Delta : le signal sec traverse un suréchantillonneur identique, puis le même retard que le limiteur
    juce::dsp::Oversampling<float> osDry;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None> dryN;
    juce::AudioBuffer<float> dry2Buf;

    // Chemin sec retardé (bypass)
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None> dry;
    juce::AudioBuffer<float> dryBuf;

    // Mesures loudness
    juce::dsp::IIR::Filter<float> kHP[2], kShelf[2];
    std::vector<float> kRing;
    int kPos = 0, nM = 1, nS = 1, nHop = 1, hopCount = 0, peakRingPos = 0;
    long long samplesRun = 0;
    double sumM = 0.0, sumS = 0.0;
    float cLR = 0.f, cLL = 0.f, cRR = 0.f, cAlpha = 0.f, hopPeak = 0.f;
    float peakRing[30] {};
    std::array<double, 800> histPow {};
    std::array<int, 800> histCnt {};
    int scopeWriteLocal = 0;

    // Undo/redo
    std::vector<Snapshot> undoStack, redoStack;
    Snapshot pendingBefore;
    int gestureDepth = 0;
    bool applyingSnapshot = false;

    // Assist
    struct AssistResult
    {
        float in = 0, spec = 0, low = 0, high = 0, glue = 0, detail = 0, sat = 0, width = 100, clip = 0, push = 0;
        float lufs = 0, plr = 0, sideMid = 0, lowRel = 0, highRel = 0, targetLufs = -11;
    } result;
    struct Analyzer
    {
        juce::dsp::IIR::Filter<float> lowHP, lowLP, midHP, midLP, highHP, highLP, kHP[2], kShelf[2];
        double eLow = 0, eMid = 0, eHigh = 0, eM = 0, eS = 0, eK = 0, gated = 0, total = 0;
        float peak = 0.f;
    } an;
    std::atomic<int> assistCmd { 0 }, assistState { 0 };
    std::atomic<float> assistProgress { 0.f };
    juce::String assistSummary;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MasterHeadProcessor)
};
