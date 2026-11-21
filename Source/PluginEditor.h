#pragma once
#include <array>
#include "PluginProcessor.h"

namespace col
{
    const juce::Colour bg      { 0xff17161f };
    const juce::Colour panel   { 0xff1c1d24 };
    const juce::Colour cell    { 0xff121318 };
    const juce::Colour track   { 0xff343945 };
    const juce::Colour accent  { 0xff3fd0c0 };
    const juce::Colour text    { 0xffe1e4ec };
    const juce::Colour dim     { 0xff8a92a3 };
    const juce::Colour good    { 0xff4cd07a };
    const juce::Colour warn    { 0xffe6b84a };
    const juce::Colour bad     { 0xffe5584d };
    const juce::Colour spectral{ 0xffb07cf0 };
    const juce::Colour comp    { 0xff5b8cff };
    const juce::Colour analog  { 0xfff08a2e };
    const juce::Colour stereo  { 0xff5fe3c0 };
    const juce::Colour eq      { 0xff7bd24a };
    const juce::Colour clip    { 0xffe9d23a };
    const juce::Colour limiter { 0xffee4b57 };
}

constexpr int kSpecBars = 40;

struct MeterData
{
    float inDb[2] { -100.f, -100.f }, outDb[2] { -100.f, -100.f };
    float maxOutDb = -100.f;
    float bandGR[3] { 0.f, 0.f, 0.f };
    float detailUp = 0.f, clipRed = 0.f, limGR = 0.f, analogDb = -100.f;
    float specGain[kSpecBands] {};
    float corr = 1.f, lufsM = -120.f, lufsS = -120.f, lufsI = -120.f, psr = -99.f;
    std::array<float, kSpecBars> bars {};   // 0..1
};

class MhLookAndFeel : public juce::LookAndFeel_V4
{
public:
    MhLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
};

// Interrupteur ON (LED verte)
class LedToggle : public juce::ToggleButton
{
public:
    LedToggle() { setButtonText ("ON"); }
    void paintButton (juce::Graphics&, bool, bool) override;
};

// Petit bouton à pastille (LM, TP, BYPASS)
class PillToggle : public juce::ToggleButton
{
public:
    PillToggle (const juce::String& t, juce::Colour on) : colour (on) { setButtonText (t); }
    void paintButton (juce::Graphics&, bool, bool) override;
private:
    juce::Colour colour;
};

class AssistButton : public juce::Button
{
public:
    AssistButton() : juce::Button ("assist") {}
    void paintButton (juce::Graphics&, bool, bool) override;
};

class InputMeter : public juce::Component
{
public:
    explicit InputMeter (const MeterData& d) : data (d) {}
    void paint (juce::Graphics&) override;
private:
    const MeterData& data;
};

class CurveView : public juce::Component
{
public:
    CurveView (const MeterData& d, MasterHeadProcessor& p) : data (d), proc (p) {}
    void paint (juce::Graphics&) override;
private:
    const MeterData& data;
    MasterHeadProcessor& proc;
};

// Barres verticales : GR de bandes, Detail, Clip, Limiter
class VBars : public juce::Component
{
public:
    VBars (std::vector<const float*> v, float full, juce::Colour c, bool fromTop, std::vector<juce::String> names = {})
        : values (std::move (v)), fullScale (full), colour (c), top (fromTop), labels (std::move (names)) {}
    void paint (juce::Graphics&) override;
private:
    std::vector<const float*> values;
    float fullScale;
    juce::Colour colour;
    bool top;
    std::vector<juce::String> labels;
};

class AnalogLeds : public juce::Component
{
public:
    AnalogLeds (const MeterData& d, juce::Slider& s) : data (d), drive (s) {}
    void paint (juce::Graphics&) override;
private:
    const MeterData& data;
    juce::Slider& drive;
};

class StereoIcon : public juce::Component
{
public:
    explicit StereoIcon (juce::Slider& s) : width (s) {}
    void paint (juce::Graphics&) override;
private:
    juce::Slider& width;
};

// Bouton "Comp Out" (petit rond avec +), comme sur la référence
class CompOutButton : public juce::ToggleButton
{
public:
    CompOutButton() { setButtonText ("Comp Out"); }
    void paintButton (juce::Graphics&, bool, bool) override;
};

class MonitorBar : public juce::Component
{
public:
    MonitorBar (MeterData& d, MasterHeadProcessor& p) : data (d), proc (p) {}
    static std::array<juce::Rectangle<int>, 7> cells (juce::Rectangle<int> bounds);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
private:
    MeterData& data;
    MasterHeadProcessor& proc;
};

class MasterHeadEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit MasterHeadEditor (MasterHeadProcessor&);
    ~MasterHeadEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    static constexpr int kBaseW = 800, kBaseH = 806;   // taille de conception : tout est mis à l'échelle à partir d'elle

    // Toile de taille fixe, redimensionnée par une transformation (la fenêtre peut être étirée librement)
    struct Canvas : public juce::Component
    {
        explicit Canvas (MasterHeadEditor& e) : ed (e) {}
        void paint (juce::Graphics& g) override { ed.paintCanvas (g); }
        void resized() override { ed.layoutCanvas(); }
        MasterHeadEditor& ed;
    };
    void paintCanvas (juce::Graphics&);
    void layoutCanvas();

    enum class Fmt { Pct, PctSigned, Db, DbSigned, DbSuffix };
    struct Knob
    {
        juce::Slider slider;
        juce::Label name, value;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
        Fmt fmt = Fmt::Pct;
    };
    struct Led
    {
        LedToggle button;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> att;
    };

    void timerCallback() override;
    void updateSpectrum();
    void setupKnob (Knob&, const char* id, const juce::String& name, juce::Colour c, Fmt f, double def);
    void setupLed (Led&, const char* id);
    void placeKnob (Knob&, juce::Rectangle<int> cell, int dia = 70);
    static void drawPanel (juce::Graphics&, juce::Rectangle<int>, const juce::String&, juce::Colour);

    MasterHeadProcessor& proc;
    MhLookAndFeel lnf;
    MeterData data;
    Canvas canvas { *this };

    Knob kIn, kSpec, kGlue, kDetail, kColor, kWidth, kBass, kAir, kClip, kPush;
    Led lSpec, lComp, lAnalog, lStereo, lEq, lClip, lLim;
    CompOutButton compOutBtn;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> aCompOut;
    PillToggle pLM { "LM", col::analog }, pTP { "TP", col::analog }, pBypass { "Bypass", col::bad };
    juce::TextButton btnA { "A" }, btnB { "B" }, btnAB { juce::String (juce::CharPointer_UTF8 ("A \xe2\x86\x92 B")) };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> aLM, aTP, aBypass;

    AssistButton assistBtn;
    juce::TextButton undoBtn { "UNDO" }, redoBtn { "REDO" };
    juce::ComboBox targetBox, monitorBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> aTarget, aMonitor;
    juce::Label summary;

    InputMeter inMeter { data };
    CurveView curve { data, proc };
    VBars compBars { { &data.bandGR[0], &data.bandGR[1], &data.bandGR[2] }, 8.f, col::limiter, true, { "L", "M", "H" } };
    VBars detailBar { { &data.detailUp }, 6.f, col::comp, false };
    VBars clipBar { { &data.clipRed }, 6.f, col::limiter, true };
    VBars limBar { { &data.limGR }, 10.f, col::limiter, true };
    AnalogLeds analogLeds { data, kColor.slider };
    StereoIcon stereoIcon { kWidth.slider };
    MonitorBar monitorBar { data, proc };

    juce::Rectangle<int> rSpec, rComp, rAnalog, rStereo, rEq, rClip, rLim;

    juce::dsp::FFT fft { 11 };
    juce::dsp::WindowingFunction<float> window { 2048, juce::dsp::WindowingFunction<float>::hann };
    std::array<float, 4096> fftBuf {};
};
