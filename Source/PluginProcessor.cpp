#include "PluginProcessor.h"
#include "PluginEditor.h"

using APVTS = juce::AudioProcessorValueTreeState;

namespace
{
    inline void maxStore (std::atomic<float>& a, float v)
    {
        float c = a.load (std::memory_order_relaxed);
        while (v > c && ! a.compare_exchange_weak (c, v)) {}
    }

    // Gain (dB, <= 0) d'un compresseur à genou doux
    inline float gainComputerDb (float levelDb, float thr, float ratio, float knee)
    {
        const float over = levelDb - thr;
        float out;
        if (2.f * over < -knee) out = levelDb;
        else if (2.f * std::abs (over) <= knee)
        {
            const float t = over + knee * 0.5f;
            out = levelDb + (1.f / ratio - 1.f) * t * t / (2.f * knee);
        }
        else out = thr + over / ratio;
        return out - levelDb;
    }

    // Écrêtage doux : linéaire sous t, tanh au-dessus, plafonné à 1.0
    inline float softClip (float x)
    {
        constexpr float t = 0.7f;
        const float a = std::abs (x);
        if (a <= t) return x;
        const float y = t + (1.f - t) * std::tanh ((a - t) / (1.f - t));
        return x < 0.f ? -y : y;
    }

    constexpr double kF1 = 200.0, kF2 = 3000.0;   // fréquences de coupure du comp 3 bandes
}

APVTS::ParameterLayout MasterHeadProcessor::createLayout()
{
    using F = juce::AudioParameterFloat;
    using B = juce::AudioParameterBool;
    using R = juce::NormalisableRange<float>;
    using PID = juce::ParameterID;

    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    auto flt = [&] (const char* id, const char* name, float lo, float hi, float step, float def)
    { p.push_back (std::make_unique<F> (PID { id, 1 }, name, R (lo, hi, step), def)); };
    auto bol = [&] (const char* id, const char* name, bool def)
    { p.push_back (std::make_unique<B> (PID { id, 1 }, name, def)); };

    flt (ids::in,     "Input",        -12.f, 12.f, 0.1f, 0.f);
    bol (ids::specOn, "Spectral On", true);
    flt (ids::spec,   "Spectral Amount", 0.f, 100.f, 1.f, 0.f);
    bol (ids::compOn, "Comp On", true);
    flt (ids::glue,   "Glue",   0.f, 100.f, 1.f, 0.f);
    flt (ids::detail, "Detail", 0.f, 100.f, 1.f, 0.f);
    bol (ids::compOut, "Comp Out", false);
    bol (ids::analogOn, "Analog On", true);
    flt (ids::sat,    "Color",  0.f, 100.f, 1.f, 0.f);
    bol (ids::stereoOn, "Stereo On", true);
    flt (ids::width,  "Width",  0.f, 200.f, 1.f, 100.f);
    bol (ids::bassOn, "Low Mono", true);
    flt (ids::bassHz, "Low Mono Freq", 20.f, 300.f, 1.f, 120.f);
    bol (ids::eqOn,   "EQ On", true);
    flt (ids::low,    "Bass",   -100.f, 100.f, 1.f, 0.f);
    flt (ids::high,   "Air",    -100.f, 100.f, 1.f, 0.f);
    bol (ids::clipOn, "Clip On", true);
    flt (ids::clip,   "Clip Gain", 0.f, 12.f, 0.1f, 0.f);
    bol (ids::limOn,  "Limiter On", true);
    flt (ids::push,   "Limiter Gain", 0.f, 12.f, 0.1f, 0.f);
    bol (ids::tp,     "True Peak", true);
    bol (ids::bypass, "Bypass", false);
    p.push_back (std::make_unique<juce::AudioParameterChoice> (PID { ids::target, 1 }, "Assist Target",
                     juce::StringArray { "-14 STREAM", "-11 LOUD", "-9 CLUB" }, 1));
    p.push_back (std::make_unique<juce::AudioParameterChoice> (PID { ids::monitor, 1 }, "Monitor",
                     juce::StringArray { "None", "Mid", "Side", "Delta" }, 0));

    return { std::make_move_iterator (p.begin()), std::make_move_iterator (p.end()) };
}

MasterHeadProcessor::MasterHeadProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout()),
      os (2, 2, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true),
      osDry (2, 2, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true)
{
    auto P = [this] (const char* id) { return apvts.getRawParameterValue (id); };
    prm = { P (ids::in), P (ids::specOn), P (ids::spec), P (ids::compOn), P (ids::glue), P (ids::detail),
            P (ids::analogOn), P (ids::sat), P (ids::stereoOn), P (ids::width), P (ids::bassOn), P (ids::bassHz),
            P (ids::eqOn), P (ids::low), P (ids::high), P (ids::clipOn), P (ids::clip), P (ids::limOn),
            P (ids::push), P (ids::tp), P (ids::bypass), P (ids::target), P (ids::monitor), P (ids::compOut) };

    for (auto* p : getParameters()) p->addListener (this);
}

MasterHeadProcessor::~MasterHeadProcessor()
{
    cancelPendingUpdate();
    for (auto* p : getParameters()) p->removeListener (this);
}

bool MasterHeadProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    return l.getMainInputChannelSet()  == juce::AudioChannelSet::stereo()
        && l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void MasterHeadProcessor::updateFilters (bool force)
{
    const float lo = prm.low->load(), hi = prm.high->load(), bh = prm.bassHz->load();

    if (force || lo != lastLow)
    {
        auto c = juce::dsp::IIR::Coefficients<float>::makeLowShelf (sr, 100.f, 0.7071f, juce::Decibels::decibelsToGain (lo * 0.06f));
        for (auto& f : lowShelf) f.coefficients = c;
        lastLow = lo;
    }
    if (force || hi != lastHigh)
    {
        const float fc = juce::jmin (10000.f, (float) sr * 0.45f);
        auto c = juce::dsp::IIR::Coefficients<float>::makeHighShelf (sr, fc, 0.7071f, juce::Decibels::decibelsToGain (hi * 0.06f));
        for (auto& f : highShelf) f.coefficients = c;
        lastHigh = hi;
    }
    if (force || bh != lastBassHz)
    {
        bassLP.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowPass (sr, bh, 0.7071f);
        lastBassHz = bh;
    }
}

void MasterHeadProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    maxBlock = juce::jmax (1, samplesPerBlock);
    juce::dsp::ProcessSpec spec { sr, (juce::uint32) maxBlock, 2 };
    const float fs = (float) sr;

    for (auto& f : lowShelf)  f.reset();
    for (auto& f : highShelf) f.reset();
    bassLP.reset();
    updateFilters (true);

    // Spectral
    for (int k = 0; k < kSpecBands; ++k)
    {
        specBP[k].set (dspc::bandpass (sr, kSpecFreq[k], 1.2));  specBP[k].reset();
        for (int c = 0; c < 2; ++c) { specEQ[c][k].set (dspc::peaking (sr, kSpecFreq[k], kSpecQ, 0.0)); specEQ[c][k].reset(); }
        specEnv[k] = 0.f; specGainDb[k] = 0.f; specApplied[k] = 0.f;
    }
    specEnvCoef = 1.f - std::exp (-1.f / (1.5f * fs));

    // Comp
    for (auto& x : xo) x.prepare (sr, kF1, kF2);
    for (int k = 0; k < 3; ++k) bFast[k] = bMed[k] = bSlow[k] = bGr[k] = bUp[k] = 0.f;
    cPe = std::exp (-1.f / (0.005f * fs));
    cPm = std::exp (-1.f / (0.100f * fs));
    cSlowAtk = 1.f - std::exp (-1.f / (0.2f * fs));
    cSlowRel = 1.f - std::exp (-1.f / (1.5f * fs));
    const float atkMs[3] { 30.f, 15.f, 8.f }, relMs[3] { 250.f, 150.f, 100.f };
    for (int k = 0; k < 3; ++k)
    {
        cAtk[k] = std::exp (-1.f / (atkMs[k] * 0.001f * fs));
        cRel[k] = std::exp (-1.f / (relMs[k] * 0.001f * fs));
    }
    cUpAtk = std::exp (-1.f / (0.15f * fs));
    cUpRel = std::exp (-1.f / (0.03f * fs));
    cMk = 1.f - std::exp (-1.f / (0.5f * fs));
    makeupDb = 0.f;

    inGain.reset (sr, 0.02);   inGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (prm.in->load()));
    widthSm.reset (sr, 0.02);  widthSm.setCurrentAndTargetValue (prm.width->load() * 0.01f);

    os.initProcessing ((size_t) maxBlock);
    os.reset();
    limiter.prepare (sr);

    const int lat = (int) std::ceil (os.getLatencyInSamples()) + limiter.getLatency();
    setLatencySamples (lat);

    dry.prepare (spec);
    dry.setMaximumDelayInSamples (lat + 2);
    dry.setDelay ((float) lat);
    dry.reset();
    dryBuf.setSize (2, maxBlock);

    osDry.initProcessing ((size_t) maxBlock);
    osDry.reset();
    dryN.prepare (spec);
    dryN.setMaximumDelayInSamples (limiter.getLatency() + 2);
    dryN.setDelay ((float) limiter.getLatency());
    dryN.reset();
    dry2Buf.setSize (2, maxBlock);

    // Loudness / mesures
    {
        using C = juce::dsp::IIR::Coefficients<float>;
        const float hiLimit = juce::jmin (16000.f, fs * 0.45f);
        an.lowHP.coefficients  = C::makeHighPass (sr, 30.f, 0.7071f);
        an.lowLP.coefficients  = C::makeLowPass  (sr, 150.f, 0.7071f);
        an.midHP.coefficients  = C::makeHighPass (sr, 300.f, 0.7071f);
        an.midLP.coefficients  = C::makeLowPass  (sr, 3000.f, 0.7071f);
        an.highHP.coefficients = C::makeHighPass (sr, 6000.f, 0.7071f);
        an.highLP.coefficients = C::makeLowPass  (sr, hiLimit, 0.7071f);
        for (int c = 0; c < 2; ++c)
        {
            an.kHP[c].coefficients    = C::makeHighPass (sr, 38.13f, 0.5f);
            an.kShelf[c].coefficients = C::makeHighShelf (sr, 1681.97f, 0.7071f, juce::Decibels::decibelsToGain (4.f));
            kHP[c].coefficients    = an.kHP[c].coefficients;
            kShelf[c].coefficients = an.kShelf[c].coefficients;
            kHP[c].reset(); kShelf[c].reset();
        }
    }
    nM = juce::jmax (1, (int) std::lround (0.4 * sr));
    nS = juce::jmax (nM + 1, (int) std::lround (3.0 * sr));
    nHop = juce::jmax (1, (int) std::lround (0.1 * sr));
    kRing.assign ((size_t) nS, 0.f);
    kPos = 0; sumM = sumS = 0.0; samplesRun = 0; hopCount = 0; hopPeak = 0.f; peakRingPos = 0;
    for (auto& v : peakRing) v = 0.f;
    histPow.fill (0.0); histCnt.fill (0);
    cLR = cLL = cRR = 0.f;
    cAlpha = std::exp (-1.f / (0.3f * fs));
}

void MasterHeadProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    if (buffer.getNumChannels() < 2) return;

    const int total = buffer.getNumSamples();
    for (int start = 0; start < total; start += maxBlock)
    {
        const int len = juce::jmin (total - start, maxBlock);
        juce::AudioBuffer<float> sub (buffer.getArrayOfWritePointers(), 2, start, len);
        processChunk (sub);
    }
}

void MasterHeadProcessor::processChunk (juce::AudioBuffer<float>& b)
{
    const int n = b.getNumSamples();
    float* L = b.getWritePointer (0);
    float* R = b.getWritePointer (1);
    const float fs = (float) sr;

    // Chemin sec retardé de la latence totale
    for (int ch = 0; ch < 2; ++ch) dryBuf.copyFrom (ch, 0, b, ch, 0, n);
    {
        juce::dsp::AudioBlock<float> blk (dryBuf);
        auto sub = blk.getSubBlock (0, (size_t) n);
        juce::dsp::ProcessContextReplacing<float> ctx (sub);
        dry.process (ctx);
    }

    if (meters.resetI.exchange (false))
    {
        histPow.fill (0.0); histCnt.fill (0);
        meters.lufsI.store (-120.f);
    }

    // Assist
    {
        const int cmd = assistCmd.exchange (0);
        if (cmd == 1)
        {
            an.eLow = an.eMid = an.eHigh = an.eM = an.eS = an.eK = an.gated = an.total = 0.0;
            an.peak = 0.f;
            for (auto* f : { &an.lowHP, &an.lowLP, &an.midHP, &an.midLP, &an.highHP, &an.highLP,
                             &an.kHP[0], &an.kHP[1], &an.kShelf[0], &an.kShelf[1] }) f->reset();
            assistProgress.store (0.f);
            assistState.store (Analyzing);
        }
        else if (cmd == 2) assistState.store (Idle);

        if (assistState.load() == Analyzing) analyzeChunk (L, R, n);
    }

    float inPkL = 0.f, inPkR = 0.f, outPkL = 0.f, outPkR = 0.f;

    if (prm.bypass->load() > 0.5f)
    {
        for (int ch = 0; ch < 2; ++ch) b.copyFrom (ch, 0, dryBuf, ch, 0, n);
        for (int i = 0; i < n; ++i)
        {
            inPkL = juce::jmax (inPkL, std::abs (L[i])); inPkR = juce::jmax (inPkR, std::abs (R[i]));
        }
        maxStore (meters.inPeakL, inPkL);  maxStore (meters.inPeakR, inPkR);
        maxStore (meters.outPeakL, inPkL); maxStore (meters.outPeakR, inPkR);
        return;
    }

    updateFilters (false);

    // Delta : chemin sec aligné sur le chemin traité (même suréchantillonneur, puis même retard que le limiteur)
    const int monitor = (int) std::lround (prm.monitor->load());
    if (monitor == 3)
    {
        for (int ch = 0; ch < 2; ++ch) dry2Buf.copyFrom (ch, 0, b, ch, 0, n);
        juce::dsp::AudioBlock<float> blk (dry2Buf);
        auto sub = blk.getSubBlock (0, (size_t) n);
        osDry.processSamplesUp (sub);
        osDry.processSamplesDown (sub);
        juce::dsp::ProcessContextReplacing<float> ctx (sub);
        dryN.process (ctx);
    }

    // ---- Paramètres du bloc
    inGain.setTargetValue (juce::Decibels::decibelsToGain (prm.in->load()));
    widthSm.setTargetValue (prm.width->load() * 0.01f);

    const bool  specOn   = prm.specOn->load() > 0.5f;
    const float specAmt  = prm.spec->load() * 0.01f;
    bool anyGain = false;
    for (int k = 0; k < kSpecBands; ++k) if (std::abs (specGainDb[k]) > 0.005f) anyGain = true;
    const bool specAnalyze = specOn && specAmt > 0.001f;
    const bool specApply   = anyGain;

    const float glueN   = prm.glue->load() * 0.01f;
    const float detailN = prm.detail->load() * 0.01f;
    const bool  compOn  = prm.compOn->load() > 0.5f && (glueN > 0.001f || detailN > 0.001f);
    const bool  compOutOn = prm.compOut->load() > 0.5f;
    const float ratio   = 1.5f + 2.5f * glueN;
    const float maxUp   = 6.f * detailN;
    constexpr float knee = 6.f;

    const bool stereoOn = prm.stereoOn->load() > 0.5f;
    const bool bassMono = prm.bassOn->load() > 0.5f;
    const bool eqOn     = prm.eqOn->load() > 0.5f;

    float grMax[3] { 0.f, 0.f, 0.f }, upMax = 0.f;

    // ---- Étage 1 (1x) : Spectral -> Comp -> Stereo -> EQ
    for (int i = 0; i < n; ++i)
    {
        inPkL = juce::jmax (inPkL, std::abs (L[i]));
        inPkR = juce::jmax (inPkR, std::abs (R[i]));

        const float g = inGain.getNextValue();
        float l = L[i] * g, r = R[i] * g;

        // Spectral : mesure en amont (feed-forward), correction identique sur L et R
        if (specAnalyze)
        {
            const float m = 0.5f * (l + r);
            for (int k = 0; k < kSpecBands; ++k)
            {
                const float bp = specBP[k].process (m);
                specEnv[k] += (bp * bp - specEnv[k]) * specEnvCoef;
            }
        }
        if (specApply)
            for (int k = 0; k < kSpecBands; ++k)
            {
                l = specEQ[0][k].process (l);
                r = specEQ[1][k].process (r);
            }

        // Comp 3 bandes + Detail
        if (compOn)
        {
            float bl[3], br[3];
            xo[0].split (l, bl[0], bl[1], bl[2]);
            xo[1].split (r, br[0], br[1], br[2]);

            float meanGr = 0.f;
            for (int k = 0; k < 3; ++k)
            {
                const float pw = 0.5f * (bl[k] * bl[k] + br[k] * br[k]);
                bFast[k] = cPe * bFast[k] + (1.f - cPe) * pw;
                bMed[k]  = cPm * bMed[k]  + (1.f - cPm) * pw;
                if (bSlow[k] <= 0.f) bSlow[k] = pw;
                else                 bSlow[k] += (pw - bSlow[k]) * (pw > bSlow[k] ? cSlowAtk : cSlowRel);

                const float lvl = 10.f * std::log10 (bFast[k] + 1.0e-12f);
                const float ref = 10.f * std::log10 (bSlow[k] + 1.0e-12f);

                float tgt = 0.f;
                if (glueN > 0.001f)   // seuil relatif au niveau moyen de la bande : réglage indépendant du volume
                    tgt = -gainComputerDb (lvl, ref + 3.f - 7.f * glueN, ratio, knee);
                const float c = tgt > bGr[k] ? cAtk[k] : cRel[k];
                bGr[k] = c * bGr[k] + (1.f - c) * tgt;

                float upT = 0.f;
                if (detailN > 0.001f && ref > -70.f)
                {
                    const float lm = 10.f * std::log10 (bMed[k] + 1.0e-12f);
                    const float fl = ref - 6.f;
                    if (lm < fl) upT = juce::jmin (maxUp, (fl - lm) * 0.5f);
                }
                const float cu = upT > bUp[k] ? cUpAtk : cUpRel;
                bUp[k] = cu * bUp[k] + (1.f - cu) * upT;
                meanGr += bGr[k] / 3.f;
            }

            l = r = 0.f;
            float finSum = 0.f;
            for (int k = 0; k < 3; ++k)
            {
                const float fin = 0.65f * bGr[k] + 0.35f * meanGr;   // liaison entre bandes
                const float gain = juce::Decibels::decibelsToGain (-fin + bUp[k]);
                l += bl[k] * gain;  r += br[k] * gain;
                grMax[k] = juce::jmax (grMax[k], fin);
                upMax = juce::jmax (upMax, bUp[k]);
                finSum += fin * (1.f / 3.f);
            }
            // Comp Out : rattrape ~80 % de la réduction moyenne (lissé sur ~0,5 s)
            makeupDb += ((compOutOn ? 0.8f * finSum : 0.f) - makeupDb) * cMk;
            const float mk = juce::Decibels::decibelsToGain (makeupDb);
            l *= mk;  r *= mk;
        }

        // Stereo : largeur M/S, graves mono sans déphasage (S - LP(S))
        if (stereoOn)
        {
            const float m = 0.5f * (l + r), s = 0.5f * (l - r);
            const float sLow = bassLP.processSample (s);
            const float sHigh = s - sLow;
            const float w = widthSm.getNextValue();
            const float sOut = bassMono ? sHigh * w : s * w;
            l = m + sOut;  r = m - sOut;
        }

        // EQ : Bass / Air
        if (eqOn)
        {
            l = highShelf[0].processSample (lowShelf[0].processSample (l));
            r = highShelf[1].processSample (lowShelf[1].processSample (r));
        }

        L[i] = l;  R[i] = r;
    }

    // Spectral : mise à jour lente des gains (fin de bloc)
    if (specAnalyze || anyGain)
    {
        float db[kSpecBands], mean = 0.f, tmean = 0.f;
        for (int k = 0; k < kSpecBands; ++k)
        {
            db[k] = 10.f * std::log10 (specEnv[k] + 1.0e-12f);
            mean += db[k] / (float) kSpecBands;
            tmean += kSpecTarget[k] / (float) kSpecBands;
        }
        const bool loud = mean > -70.f;
        const float kk = 1.f - std::exp (-(float) n / fs);   // ~1 s
        for (int k = 0; k < kSpecBands; ++k)
        {
            float target = specGainDb[k];
            if (! specAnalyze)  target = 0.f;
            else if (loud)      target = juce::jlimit (-4.f, 4.f, specAmt * 0.5f * ((kSpecTarget[k] - tmean) - (db[k] - mean)));
            specGainDb[k] += (target - specGainDb[k]) * kk;

            if (std::abs (specGainDb[k] - specApplied[k]) > 0.02f)
            {
                const auto c = dspc::peaking (sr, kSpecFreq[k], kSpecQ, (double) specGainDb[k]);
                specEQ[0][k].set (c);  specEQ[1][k].set (c);
                specApplied[k] = specGainDb[k];
            }
            meters.specGain[(size_t) k].store (specGainDb[k], std::memory_order_relaxed);
        }
    }

    // ---- Étage 2 (4x) : Analog puis Clip
    float analogPk = 0.f, clipRedMax = 1.f;
    {
        const bool  analogOn = prm.analogOn->load() > 0.5f;
        const float drive = prm.sat->load() * 0.01f;
        const bool  satOn = analogOn && drive > 0.001f;
        const float gSat  = 1.f + 5.f * drive;
        const float satNorm = 0.3f / std::tanh (gSat * 0.3f);
        const float clipDb  = prm.clip->load();
        const bool  clipOn  = prm.clipOn->load() > 0.5f && clipDb > 0.01f;
        const float gClip   = juce::Decibels::decibelsToGain (clipDb);

        juce::dsp::AudioBlock<float> blk (b);
        auto up = os.processSamplesUp (blk);
        for (size_t ch = 0; ch < up.getNumChannels(); ++ch)
        {
            float* d = up.getChannelPointer (ch);
            for (size_t i = 0; i < up.getNumSamples(); ++i)
            {
                float x = d[i];
                analogPk = juce::jmax (analogPk, std::abs (x));
                if (satOn)
                {
                    const float y = std::tanh (gSat * x) * satNorm;
                    x += (y - x) * drive;
                }
                if (clipOn)
                {
                    const float xg = x * gClip;
                    x = softClip (xg);
                    if (std::abs (xg) > 0.7f) clipRedMax = juce::jmax (clipRedMax, std::abs (xg) / (std::abs (x) + 1.0e-9f));
                }
                d[i] = x;
            }
        }
        os.processSamplesDown (blk);
    }

    // ---- Étage 3 (1x) : Limiter, Mix, Monitor, mesures
    const bool  limOn   = prm.limOn->load() > 0.5f;
    const bool  tpOn    = prm.tp->load() > 0.5f;
    const float pushLin = limOn ? juce::Decibels::decibelsToGain (prm.push->load()) : 1.f;
    const float ceilLin = limOn ? juce::Decibels::decibelsToGain (tpOn ? -1.0f : -0.3f) : 1000.f;
    const float* dL = dry2Buf.getReadPointer (0);
    const float* dR = dry2Buf.getReadPointer (1);
    float limMin = 1.f;

    for (int i = 0; i < n; ++i)
    {
        float l = L[i] * pushLin, r = R[i] * pushLin;
        limMin = juce::jmin (limMin, limiter.process (l, r, ceilLin, tpOn && limOn));

        outPkL = juce::jmax (outPkL, std::abs (l));
        outPkR = juce::jmax (outPkR, std::abs (r));

        // Loudness (K approché) : fenêtres 400 ms et 3 s
        const float kl = kShelf[0].processSample (kHP[0].processSample (l));
        const float kr = kShelf[1].processSample (kHP[1].processSample (r));
        const float p = kl * kl + kr * kr;
        sumS += (double) p - (double) kRing[(size_t) kPos];
        int idxM = kPos - nM; if (idxM < 0) idxM += nS;
        sumM += (double) p - (double) kRing[(size_t) idxM];
        kRing[(size_t) kPos] = p;
        if (++kPos >= nS) kPos = 0;
        ++samplesRun;

        hopPeak = juce::jmax (hopPeak, std::abs (l), std::abs (r));
        if (++hopCount >= nHop) { hopCount = 0; if (samplesRun >= nM) addHopBlock(); }

        // Corrélation (~300 ms)
        cLR = cAlpha * cLR + (1.f - cAlpha) * l * r;
        cLL = cAlpha * cLL + (1.f - cAlpha) * l * l;
        cRR = cAlpha * cRR + (1.f - cAlpha) * r * r;

        // Monitor (écoute seulement)
        switch (monitor)
        {
            case 1: { const float m = 0.5f * (l + r); l = r = m; break; }
            case 2: { const float s = 0.5f * (l - r); l = r = s; break; }
            case 3: l -= dL[i]; r -= dR[i]; break;
            default: break;
        }
        L[i] = l;  R[i] = r;
        meters.scope[(size_t) (scopeWriteLocal++ & 4095)].store (0.5f * (l + r), std::memory_order_relaxed);
    }
    meters.scopeW.store (scopeWriteLocal & 4095, std::memory_order_release);

    maxStore (meters.inPeakL, inPkL);   maxStore (meters.inPeakR, inPkR);
    maxStore (meters.outPeakL, outPkL); maxStore (meters.outPeakR, outPkR);
    for (size_t k = 0; k < 3; ++k) maxStore (meters.bandGR[k], grMax[k]);
    maxStore (meters.detailUp, upMax);
    maxStore (meters.clipRed, juce::Decibels::gainToDecibels (clipRedMax, 0.f));
    maxStore (meters.limGR, -juce::Decibels::gainToDecibels (limMin, -60.f));
    maxStore (meters.analogLvl, analogPk);

    const double eps = 1.0e-12;
    meters.lufsM.store ((float) (-0.691 + 10.0 * std::log10 (juce::jmax (eps, sumM / nM))));
    meters.lufsS.store ((float) (-0.691 + 10.0 * std::log10 (juce::jmax (eps, sumS / nS))));
    const float den = std::sqrt (cLL * cRR);
    meters.correlation.store (den > 1.0e-9f ? juce::jlimit (-1.f, 1.f, cLR / den) : 1.f);
}

// Une fois toutes les 100 ms : LUFS intégré (portes -70 LUFS puis -10 LU) et PSR
void MasterHeadProcessor::addHopBlock()
{
    const double eps = 1.0e-12;
    const double blockPow = juce::jmax (0.0, sumM / nM);
    const double loud = -0.691 + 10.0 * std::log10 (blockPow + eps);
    if (loud > -70.0)
    {
        const int bin = juce::jlimit (0, 799, (int) ((loud + 70.0) * 10.0));
        histPow[(size_t) bin] += blockPow;
        histCnt[(size_t) bin]++;
    }

    double sp = 0.0; long long c = 0;
    for (size_t i = 0; i < 800; ++i) { sp += histPow[i]; c += histCnt[i]; }
    if (c > 0)
    {
        const double absMean = sp / (double) c;
        const double relGate = -0.691 + 10.0 * std::log10 (absMean) - 10.0;
        const int startBin = juce::jlimit (0, 799, (int) std::ceil ((relGate + 70.0) * 10.0));
        double sp2 = 0.0; long long c2 = 0;
        for (int i = startBin; i < 800; ++i) { sp2 += histPow[(size_t) i]; c2 += histCnt[(size_t) i]; }
        const double m = c2 > 0 ? sp2 / (double) c2 : absMean;
        meters.lufsI.store ((float) (-0.691 + 10.0 * std::log10 (m)));
    }

    peakRing[peakRingPos] = hopPeak;  hopPeak = 0.f;
    peakRingPos = (peakRingPos + 1) % 30;
    float recent = 0.f;
    for (float v : peakRing) recent = juce::jmax (recent, v);

    const float lufsS = (float) (-0.691 + 10.0 * std::log10 (juce::jmax (eps, sumS / nS)));
    if (samplesRun >= nS && lufsS > -70.f) meters.psr.store (juce::Decibels::gainToDecibels (recent, -100.f) - lufsS);
    else                                   meters.psr.store (-99.f);
}

juce::AudioProcessorEditor* MasterHeadProcessor::createEditor() { return new MasterHeadEditor (*this); }

void MasterHeadProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
    {
        xml->setAttribute ("editorW", editorW);
        xml->setAttribute ("editorH", editorH);
        copyXmlToBinary (*xml, dest);
    }
}

void MasterHeadProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            editorW = xml->getIntAttribute ("editorW", 0);
            editorH = xml->getIntAttribute ("editorH", 0);
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MasterHeadProcessor(); }

// ====================================================================== Assist
void MasterHeadProcessor::analyzeChunk (const float* L, const float* R, int n)
{
    double kSum = 0, eL = 0, eMd = 0, eH = 0, eMs = 0, eSs = 0;
    float pk = 0.f;

    for (int i = 0; i < n; ++i)
    {
        const float l = L[i], r = R[i];
        pk = juce::jmax (pk, std::abs (l), std::abs (r));

        const float kl = an.kShelf[0].processSample (an.kHP[0].processSample (l));
        const float kr = an.kShelf[1].processSample (an.kHP[1].processSample (r));
        kSum += (double) (kl * kl + kr * kr);

        const float m = 0.5f * (l + r), s = 0.5f * (l - r);
        const float lo = an.lowLP.processSample  (an.lowHP.processSample  (m));
        const float mi = an.midLP.processSample  (an.midHP.processSample  (m));
        const float hi = an.highLP.processSample (an.highHP.processSample (m));
        eL += (double) lo * lo;  eMd += (double) mi * mi;  eH += (double) hi * hi;
        eMs += (double) m * m;   eSs += (double) s * s;
    }

    an.total += n;
    an.peak = juce::jmax (an.peak, pk);

    const double chunkLufs = -0.691 + 10.0 * std::log10 (kSum / n + 1.0e-12);
    if (chunkLufs > -60.0)
    {
        an.gated += n;  an.eK += kSum;
        an.eLow += eL;  an.eMid += eMd;  an.eHigh += eH;  an.eM += eMs;  an.eS += eSs;
    }

    constexpr double seconds = 10.0;
    assistProgress.store ((float) juce::jmin (1.0, an.total / (seconds * sr)));
    if (an.total >= seconds * sr) finishAnalysis();
}

void MasterHeadProcessor::finishAnalysis()
{
    if (an.gated < 3.0 * sr) { assistState.store (NoData); return; }

    // Constantes heuristiques (mes hypothèses, pas des normes) : à ajuster à l'oreille.
    constexpr float kLowTargetRel  =  3.f;
    constexpr float kHighTargetRel = -9.f;
    constexpr double eps = 1.0e-12;

    auto perOctDb = [&] (double e, double oct) { return 10.0 * std::log10 (e / an.gated / oct + eps); };
    const double midDb = perOctDb (an.eMid, 3.32);

    auto& r = result;
    r.lufs    = (float) (-0.691 + 10.0 * std::log10 (an.eK / an.gated + eps));
    r.plr     = juce::Decibels::gainToDecibels (an.peak, -100.f) - r.lufs;
    r.lowRel  = (float) (perOctDb (an.eLow, 2.32) - midDb);
    r.highRel = (float) (perOctDb (an.eHigh, 1.415) - midDb);
    r.sideMid = (float) (10.0 * std::log10 (an.eS / (an.eM + eps) + eps));

    auto round1 = [] (float v) { return std::round (v * 10.f) / 10.f; };
    // Bass/Air en % (100 % = 6 dB) ; correction moitié moindre car le Spectral travaille aussi
    r.low  = std::round (juce::jlimit (-3.f, 3.f, 0.5f * (kLowTargetRel  - r.lowRel))  / 0.06f * 0.5f);
    r.high = std::round (juce::jlimit (-3.f, 3.f, 0.5f * (kHighTargetRel - r.highRel)) / 0.06f * 0.5f);
    r.spec   = 50.f;
    r.glue   = std::round (juce::jlimit (10.f, 60.f, (r.plr - 8.f) * 5.f));
    r.detail = std::round (juce::jlimit (0.f, 40.f, (r.plr - 9.f) * 5.f));
    r.sat    = r.plr < 10.f ? 8.f : 15.f;
    r.width  = r.sideMid < -14.f ? 120.f : r.sideMid < -10.f ? 110.f : r.sideMid > -4.f ? 95.f : 100.f;

    const int idx = juce::jlimit (0, 2, (int) std::lround (prm.target->load()));
    r.targetLufs = idx == 0 ? -14.f : idx == 1 ? -11.f : -9.f;

    const float need = r.targetLufs - r.lufs + r.glue * 0.03f;
    if (need <= 0.f) { r.clip = r.push = r.in = 0.f; }
    else
    {
        r.clip = round1 (juce::jlimit (0.f, 3.f, need * 0.25f));
        const float rest = need - r.clip;
        r.push = round1 (juce::jmin (rest, 12.f));
        r.in   = round1 (juce::jlimit (0.f, 12.f, rest - 12.f));
    }

    assistState.store (Done, std::memory_order_release);
    triggerAsyncUpdate();
}

void MasterHeadProcessor::handleAsyncUpdate()
{
    if (assistState.load (std::memory_order_acquire) == Done) applyAssistParams();
}

void MasterHeadProcessor::applyAssistParams()
{
    const auto before = takeSnapshot();
    {
        juce::ScopedValueSetter<bool> guard (applyingSnapshot, true);
        auto set = [this] (const char* id, float v)
        {
            if (auto* p = apvts.getParameter (id))
            {
                p->beginChangeGesture();
                p->setValueNotifyingHost (p->convertTo0to1 (v));
                p->endChangeGesture();
            }
        };
        for (auto* on : { ids::specOn, ids::compOn, ids::analogOn, ids::stereoOn, ids::eqOn, ids::clipOn, ids::limOn, ids::tp, ids::bassOn })
            set (on, 1.f);
        set (ids::in, result.in);       set (ids::spec, result.spec);   set (ids::glue, result.glue);
        set (ids::detail, result.detail); set (ids::sat, result.sat);   set (ids::width, result.width);
        set (ids::bassHz, 120.f);       set (ids::low, result.low);     set (ids::high, result.high);
        set (ids::clip, result.clip);   set (ids::push, result.push);
        set (ids::bypass, 0.f);         set (ids::monitor, 0.f);
    }
    pushUndo (before);

    auto sgn = [] (float v) { return (v >= 0 ? "+" : "") + juce::String (v, 1); };
    assistSummary = juce::String (juce::CharPointer_UTF8 ("Mesur\xc3\xa9 : ")) + juce::String (result.lufs, 1) + " LUFS, PLR " + juce::String (result.plr, 1)
                  + " dB, graves " + sgn (result.lowRel) + ", aigus " + sgn (result.highRel)
                  + ", side/mid " + juce::String (result.sideMid, 1) + " dB  |  Cible " + juce::String (result.targetLufs, 0)
                  + " LUFS : Spectral " + juce::String ((int) result.spec) + "%, Glue " + juce::String ((int) result.glue)
                  + "%, Detail " + juce::String ((int) result.detail) + "%, Bass " + juce::String ((int) result.low)
                  + "%, Air " + juce::String ((int) result.high) + "%, Limiter " + sgn (result.push) + " dB";
}

// ====================================================================== Undo / Redo
MasterHeadProcessor::Snapshot MasterHeadProcessor::takeSnapshot() const
{
    Snapshot s;
    for (auto* p : getParameters()) s.push_back (p->getValue());
    return s;
}

void MasterHeadProcessor::applySnapshot (const Snapshot& s)
{
    juce::ScopedValueSetter<bool> guard (applyingSnapshot, true);
    const auto& params = getParameters();
    for (int i = 0; i < params.size() && i < (int) s.size(); ++i)
        if (params[i]->getValue() != s[(size_t) i])
        {
            params[i]->beginChangeGesture();
            params[i]->setValueNotifyingHost (s[(size_t) i]);
            params[i]->endChangeGesture();
        }
}

void MasterHeadProcessor::pushUndo (const Snapshot& before)
{
    if (before == takeSnapshot()) return;
    undoStack.push_back (before);
    if (undoStack.size() > 200) undoStack.erase (undoStack.begin());
    redoStack.clear();
}

void MasterHeadProcessor::parameterGestureChanged (int, bool starting)
{
    if (applyingSnapshot || ! juce::MessageManager::existsAndIsCurrentThread()) return;

    if (starting)
    {
        if (gestureDepth++ == 0) pendingBefore = takeSnapshot();
    }
    else if (gestureDepth > 0 && --gestureDepth == 0)
    {
        pushUndo (pendingBefore);
    }
}

void MasterHeadProcessor::undo()
{
    if (undoStack.empty()) return;
    redoStack.push_back (takeSnapshot());
    const auto target = undoStack.back();
    undoStack.pop_back();
    applySnapshot (target);
}

void MasterHeadProcessor::redo()
{
    if (redoStack.empty()) return;
    undoStack.push_back (takeSnapshot());
    const auto target = redoStack.back();
    redoStack.pop_back();
    applySnapshot (target);
}


// ====================================================================== A / B
void MasterHeadProcessor::selectSlot (int s)
{
    s = juce::jlimit (0, 1, s);
    if (s == activeSlot) return;
    slots[activeSlot] = takeSnapshot();
    activeSlot = s;
    if (slots[s].empty()) slots[s] = slots[1 - s];
    const auto before = takeSnapshot();
    applySnapshot (slots[s]);
    (void) before;   // le changement A/B n'entre pas dans l'historique undo
}

void MasterHeadProcessor::copyToOtherSlot()
{
    slots[1 - activeSlot] = takeSnapshot();
}
