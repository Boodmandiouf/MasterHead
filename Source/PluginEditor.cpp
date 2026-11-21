#include "PluginEditor.h"

namespace
{
    float toDb (float lin) { return juce::Decibels::gainToDecibels (lin, -100.f); }
    juce::String sgn (float v, int dec = 1) { return (v >= 0.f ? "+" : "") + juce::String (v, dec); }
}

// ---------------------------------------------------------------- LookAndFeel & petits composants
MhLookAndFeel::MhLookAndFeel()
{
    setColour (juce::Label::textColourId, col::text);
    setColour (juce::TextButton::buttonColourId, col::cell);
    setColour (juce::TextButton::textColourOffId, col::text);
    setColour (juce::TextButton::textColourOnId, col::text);
    setColour (juce::ComboBox::backgroundColourId, col::cell);
    setColour (juce::ComboBox::textColourId, col::text);
    setColour (juce::ComboBox::outlineColourId, col::track);
    setColour (juce::ComboBox::arrowColourId, col::text);
    setColour (juce::PopupMenu::backgroundColourId, col::panel);
    setColour (juce::PopupMenu::textColourId, col::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, col::accent.withAlpha (0.4f));
}

void MhLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                      float startAngle, float endAngle, juce::Slider& s)
{
    auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4.f);
    const float r = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f;
    const auto c = b.getCentre();
    const float angle = startAngle + pos * (endAngle - startAngle);
    const auto colour = s.findColour (juce::Slider::rotarySliderFillColourId);
    const float th = juce::jmax (3.f, r * 0.12f);
    const juce::PathStrokeType stroke (th, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    juce::Path bgArc, valArc;
    bgArc.addCentredArc (c.x, c.y, r, r, 0.f, startAngle, endAngle, true);
    // l'arc part du centre (utile pour Bass/Air bipolaires) ou du minimum
    const bool bipolar = s.getMinimum() < 0.0;
    const float zeroAngle = bipolar ? startAngle + (float) s.valueToProportionOfLength (0.0) * (endAngle - startAngle) : startAngle;
    valArc.addCentredArc (c.x, c.y, r, r, 0.f, juce::jmin (zeroAngle, angle), juce::jmax (zeroAngle, angle), true);
    g.setColour (col::track);  g.strokePath (bgArc, stroke);
    g.setColour (colour);      g.strokePath (valArc, stroke);

    const float rb = r - th - 3.f;
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a3d48), c.x, c.y - rb, juce::Colour (0xff22242b), c.x, c.y + rb, false));
    g.fillEllipse (c.x - rb, c.y - rb, rb * 2.f, rb * 2.f);

    juce::Path ptr;
    ptr.addRoundedRectangle (-1.5f, -rb + 3.f, 3.f, rb * 0.45f, 1.5f);
    g.setColour (col::text);
    g.fillPath (ptr, juce::AffineTransform::rotation (angle).translated (c.x, c.y));
}

void LedToggle::paintButton (juce::Graphics& g, bool, bool)
{
    const auto b = getLocalBounds().toFloat();
    const float d = 12.f;
    const juce::Rectangle<float> led (0.f, b.getCentreY() - d * 0.5f, d, d);
    if (getToggleState())
    {
        g.setColour (col::good.withAlpha (0.25f));  g.fillEllipse (led.expanded (3.f));
        g.setColour (col::good);                    g.fillEllipse (led);
    }
    else { g.setColour (col::track); g.fillEllipse (led); }
    g.setColour (col::dim);
    g.setFont (13.f);
    g.drawText ("ON", (int) d + 8, 0, getWidth() - (int) d - 8, getHeight(), juce::Justification::centredLeft);
}

void PillToggle::paintButton (juce::Graphics& g, bool, bool)
{
    auto b = getLocalBounds().toFloat().reduced (1.f);
    g.setColour (getToggleState() ? colour : col::cell);
    g.fillRoundedRectangle (b, 5.f);
    g.setColour (getToggleState() ? colour.brighter (0.3f) : col::track);
    g.drawRoundedRectangle (b, 5.f, 1.f);
    g.setColour (getToggleState() ? juce::Colour (0xff1a1208) : col::dim);
    g.setFont (juce::Font (juce::FontOptions (13.f, juce::Font::bold)));
    g.drawText (getButtonText(), getLocalBounds(), juce::Justification::centred);
}

void AssistButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    const auto b = getLocalBounds().toFloat().reduced (4.f);
    const float d = juce::jmin (b.getWidth(), b.getHeight());
    const auto r = juce::Rectangle<float> (d, d).withCentre (b.getCentre());
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffb9bcc6), r.getX(), r.getY(), juce::Colour (0xff4a4d58), r.getX(), r.getBottom(), false));
    g.fillEllipse (r);
    auto in = r.reduced (5.f);
    const auto red = down ? juce::Colour (0xffa81620) : over ? juce::Colour (0xfff0323e) : juce::Colour (0xffdc2430);
    g.setGradientFill (juce::ColourGradient (red.brighter (0.25f), in.getX(), in.getY(), red.darker (0.4f), in.getX(), in.getBottom(), false));
    g.fillEllipse (in);
    g.setColour (juce::Colours::white);
    g.setFont (juce::Font (juce::FontOptions (getButtonText().length() > 6 ? 12.f : 16.f, juce::Font::bold)));
    g.drawText (getButtonText(), in.toNearestInt(), juce::Justification::centred);
}

void CompOutButton::paintButton (juce::Graphics& g, bool over, bool)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (col::dim);
    g.setFont (11.f);
    g.drawFittedText ("Comp\nOut", b.removeFromTop (28.f).toNearestInt(), juce::Justification::centred, 2);
    const float d = 26.f;
    const auto r = juce::Rectangle<float> (d, d).withCentre (b.getCentre());
    const bool on = getToggleState();
    g.setColour (on ? col::comp : (over ? juce::Colour (0xff4a4f5c) : col::track));
    g.fillEllipse (r);
    g.setColour (on ? juce::Colours::white : col::dim);
    g.drawLine (r.getCentreX() - 6.f, r.getCentreY(), r.getCentreX() + 6.f, r.getCentreY(), 2.f);
    g.drawLine (r.getCentreX(), r.getCentreY() - 6.f, r.getCentreX(), r.getCentreY() + 6.f, 2.f);
}

void InputMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.f, 4.f);
    g.setColour (col::cell);  g.fillRoundedRectangle (r, 3.f);
    const float db = juce::jmax (data.inDb[0], data.inDb[1]);
    const float f = juce::jlimit (0.f, 1.f, (db + 60.f) / 60.f);
    juce::ColourGradient grad (col::good, r.getX(), 0.f, col::bad, r.getRight(), 0.f, false);
    grad.addColour (0.75, col::warn);
    g.setGradientFill (grad);
    // segments façon vu-mètre
    const int segs = 48;
    const float sw = r.getWidth() / (float) segs;
    for (int i = 0; i < segs; ++i)
        if ((float) (i + 1) / (float) segs <= f)
            g.fillRect (r.getX() + (float) i * sw + 0.5f, r.getY() + 2.f, sw - 1.f, r.getHeight() - 4.f);
    g.setColour (juce::Colours::white);
    g.setFont (juce::Font (juce::FontOptions (12.f, juce::Font::bold)));
    g.drawText (db < -99.f ? juce::String ("-inf dB") : juce::String (db, 1) + " dB", getLocalBounds().withTrimmedLeft (4), juce::Justification::centredLeft);
}

void CurveView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff0e0f14));  g.fillRoundedRectangle (r, 5.f);
    const double sr = proc.getSr() > 0.0 ? proc.getSr() : 48000.0;
    const float range = 6.f;
    auto yOf = [&] (float db) { return r.getCentreY() - (db / range) * (r.getHeight() * 0.45f); };

    g.setColour (col::track.withAlpha (0.5f));
    g.drawLine (r.getX(), yOf (0.f), r.getRight(), yOf (0.f), 1.f);
    for (float f : { 100.f, 1000.f, 10000.f })
    {
        const float x = r.getX() + std::log (f / 20.f) / std::log (1000.f) * r.getWidth();
        g.drawLine (x, r.getY() + 4.f, x, r.getBottom() - 4.f, 0.5f);
    }

    juce::Path p;
    BiquadCoeffs k[kSpecBands];
    for (int i = 0; i < kSpecBands; ++i) k[i] = dspc::peaking (sr, kSpecFreq[i], kSpecQ, (double) data.specGain[i]);
    const int pts = 160;
    for (int i = 0; i <= pts; ++i)
    {
        const double f = 20.0 * std::pow (1000.0, (double) i / pts);
        double db = 0.0;
        for (auto& c : k) db += dspc::magnitudeDb (c, f, sr);
        const float x = r.getX() + (float) i / pts * r.getWidth();
        const float y = juce::jlimit (r.getY() + 3.f, r.getBottom() - 3.f, yOf ((float) db));
        if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
    }
    g.setColour (juce::Colour (0xff35d6ff).withAlpha (0.25f));
    g.strokePath (p, juce::PathStrokeType (5.f));
    g.setColour (juce::Colour (0xff35d6ff));
    g.strokePath (p, juce::PathStrokeType (2.f));
}

void VBars::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    if (! labels.empty()) area.removeFromBottom (14.f);
    const int n = (int) values.size();
    const float gap = 6.f;
    const float bw = (area.getWidth() - gap * (float) (n - 1)) / (float) n;
    for (int i = 0; i < n; ++i)
    {
        const juce::Rectangle<float> r (area.getX() + (float) i * (bw + gap), area.getY(), bw, area.getHeight());
        g.setColour (juce::Colour (0xff0e0f14));  g.fillRoundedRectangle (r, 2.f);
        const float f = juce::jlimit (0.f, 1.f, *values[(size_t) i] / fullScale);
        if (f > 0.01f)
        {
            g.setColour (colour);
            g.fillRoundedRectangle (top ? r.withHeight (r.getHeight() * f) : r.withTop (r.getBottom() - r.getHeight() * f), 2.f);
        }
        if (! labels.empty())
        {
            g.setColour (col::dim);  g.setFont (12.f);
            g.drawText (labels[(size_t) i], juce::Rectangle<float> (r.getX() - 4.f, area.getBottom() + 1.f, bw + 8.f, 13.f).toNearestInt(), juce::Justification::centred);
        }
    }
}

void AnalogLeds::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    const float amount = (float) drive.getValue() * 0.01f;
    const float level = juce::jlimit (0.f, 1.f, (data.analogDb + 24.f) / 24.f);
    const int lit = amount < 0.01f ? 0 : (int) std::lround (level * 4.f * (0.4f + 0.6f * amount));
    const float rh = (area.getHeight() - 3.f * 4.f) / 4.f;
    for (int i = 0; i < 4; ++i)
    {
        const juce::Rectangle<float> r (area.getX(), area.getY() + (float) i * (rh + 4.f), area.getWidth(), rh);
        const bool on = (3 - i) < lit;   // de bas en haut
        g.setColour (juce::Colour (0xff0e0f14));  g.fillRoundedRectangle (r, 3.f);
        if (on) { g.setColour (col::analog.withAlpha (0.8f));  g.fillRoundedRectangle (r.reduced (2.f), 2.f); }
    }
}

void StereoIcon::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff0e0f14));  g.fillRoundedRectangle (r, 5.f);
    const float w = (float) width.getValue() / 200.f;      // 0..1
    const float rad = r.getHeight() * 0.28f, sep = 4.f + w * 22.f;
    const auto c = r.getCentre();
    g.setColour (col::text);
    g.drawEllipse (c.x - sep * 0.5f - rad, c.y - rad, rad * 2.f, rad * 2.f, 2.f);
    g.drawEllipse (c.x + sep * 0.5f - rad, c.y - rad, rad * 2.f, rad * 2.f, 2.f);
    g.setColour (col::dim);  g.setFont (13.f);
    g.drawText ("L", r.withWidth (18.f).toNearestInt(), juce::Justification::centred);
    g.drawText ("R", r.withLeft (r.getRight() - 18.f).toNearestInt(), juce::Justification::centred);
}

// ---------------------------------------------------------------- Barre de monitoring
std::array<juce::Rectangle<int>, 7> MonitorBar::cells (juce::Rectangle<int> b)
{
    const float wts[7] { 110.f, 150.f, 90.f, 90.f, 86.f, 96.f, 110.f };
    float sum = 0.f;  for (float w : wts) sum += w;
    const int gap = 4;
    const float avail = (float) (b.getWidth() - gap * 6);
    std::array<juce::Rectangle<int>, 7> out;
    float x = (float) b.getX();
    for (size_t i = 0; i < 7; ++i)
    {
        const float w = avail * wts[i] / sum;
        out[i] = juce::Rectangle<int> ((int) x, b.getY(), (int) w, b.getHeight());
        x += w + (float) gap;
    }
    return out;
}

void MonitorBar::mouseDown (const juce::MouseEvent& e)
{
    const auto c = cells (getLocalBounds());
    if (c[3].contains (e.getPosition())) proc.resetIntegrated();
    if (c[4].contains (e.getPosition())) data.maxOutDb = -100.f;
}

void MonitorBar::paint (juce::Graphics& g)
{
    const auto c = cells (getLocalBounds());
    for (auto& r : c)
    {
        g.setColour (col::cell);  g.fillRoundedRectangle (r.toFloat(), 6.f);
        g.setColour (col::track.withAlpha (0.6f));  g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 6.f, 1.f);
    }

    // la case Meters est cerclée de vert comme sur la capture de référence
    g.setColour (col::good);  g.drawRoundedRectangle (c[1].toFloat().reduced (0.5f), 6.f, 1.5f);

    auto label = [&] (juce::Rectangle<int> r, const juce::String& t)
    {
        g.setColour (col::dim);  g.setFont (11.f);
        g.drawText (t, r.removeFromTop (18).withTrimmedTop (4), juce::Justification::centred);
    };
    auto fmt = [] (float v, float floor = -99.f) { return v < floor ? juce::String ("--") : juce::String (v, 1); };
    auto verdict = [&] (juce::Rectangle<int> r, const juce::String& t, juce::Colour cl)
    {
        g.setColour (cl);  g.setFont (juce::Font (juce::FontOptions (11.f, juce::Font::bold)));
        g.drawText (t, r.removeFromBottom (18), juce::Justification::centred);
    };
    auto value = [&] (juce::Rectangle<int> r, const juce::String& t)
    {
        g.setColour (col::text);  g.setFont (juce::Font (juce::FontOptions (20.f, juce::Font::bold)));
        g.drawText (t, r, juce::Justification::centred);
    };

    // 0 : Monitor (combo posé par l'éditeur)
    label (c[0], "Monitor");

    // 1 : Meters (analyseur de spectre)
    {
        auto r = c[1];
        g.setColour (col::good);  g.setFont (juce::Font (juce::FontOptions (13.f, juce::Font::bold)));
        g.drawText ("Meters", r.removeFromTop (20).withTrimmedLeft (8), juce::Justification::centredLeft);
        auto area = r.reduced (6, 4).toFloat();
        const float bw = area.getWidth() / (float) kSpecBars;
        g.setColour (col::good.withAlpha (0.9f));
        for (int i = 0; i < kSpecBars; ++i)
        {
            const float h = juce::jlimit (0.f, 1.f, data.bars[(size_t) i]) * area.getHeight();
            if (h > 0.5f) g.fillRect (area.getX() + (float) i * bw + 0.5f, area.getBottom() - h, bw - 1.f, h);
        }
    }

    // 2 : LUFS S
    {
        label (c[2], "LUFS S");
        const float v = data.lufsS;
        value (c[2].withTrimmedTop (14).withTrimmedBottom (12), fmt (v, -70.f));
        if (v > -70.f) { if (v > -8.f) verdict (c[2], "LOUD", col::warn); else if (v < -20.f) verdict (c[2], "QUIET", col::dim); else verdict (c[2], "GOOD", col::good); }
    }

    // 3 : LUFS I (+ pictogramme de remise à zéro)
    {
        label (c[3], "LUFS I");
        const auto ic = juce::Rectangle<float> ((float) c[3].getRight() - 18.f, (float) c[3].getY() + 6.f, 10.f, 10.f);
        g.setColour (col::accent);
        juce::Path arc;  arc.addCentredArc (ic.getCentreX(), ic.getCentreY(), 4.5f, 4.5f, 0.f, 0.6f, 5.7f, true);
        g.strokePath (arc, juce::PathStrokeType (1.6f));
        const float v = data.lufsI;
        value (c[3].withTrimmedTop (14).withTrimmedBottom (12), fmt (v, -70.f));
        if (v > -70.f) { if (v > -8.f) verdict (c[3], "LOUD", col::warn); else if (v < -20.f) verdict (c[3], "QUIET", col::dim); else verdict (c[3], "GOOD", col::good); }
    }

    // 4 : PEAK
    {
        label (c[4], "PEAK");
        g.setColour (data.maxOutDb > -0.05f ? col::bad : col::text);
        g.setFont (juce::Font (juce::FontOptions (20.f, juce::Font::bold)));
        g.drawText (fmt (data.maxOutDb), c[4].withTrimmedTop (14), juce::Justification::centred);
    }

    // 5 : DYNAMICS (PSR : crête récente - LUFS court terme)
    {
        label (c[5], "DYNAMICS");
        const float v = data.psr;
        value (c[5].withTrimmedTop (14).withTrimmedBottom (12), v < -50.f ? juce::String ("--") : juce::String (v, 1));
        if (v > -50.f)
        {
            if (v < 5.f)       verdict (c[5], "CRUSHED", col::bad);
            else if (v < 7.f)  verdict (c[5], "DENSE",   col::warn);
            else if (v < 10.f) verdict (c[5], "PUNCHY",  juce::Colour (0xff5b8cff));
            else               verdict (c[5], "DYNAMIC", col::good);
        }
    }

    // 6 : STEREO
    {
        label (c[6], "STEREO");
        auto r = c[6].reduced (10, 0);
        auto bar = r.withTop (c[6].getY() + 26).withHeight (10).toFloat();
        g.setColour (juce::Colour (0xff0e0f14));  g.fillRoundedRectangle (bar, 3.f);
        const float cx = bar.getCentreX();
        const float x = bar.getX() + (data.corr * 0.5f + 0.5f) * bar.getWidth();
        g.setColour (data.corr < 0.f ? col::bad : juce::Colour (0xff35b6ff));
        g.fillRect (juce::Rectangle<float> (juce::jmin (x, cx), bar.getY() + 1.f, juce::jmax (3.f, std::abs (x - cx)), bar.getHeight() - 2.f));
        const bool silent = data.lufsS < -70.f;
        if (! silent) verdict (c[6], data.corr < -0.1f ? "PHASE" : "OK", data.corr < -0.1f ? col::bad : juce::Colour (0xff35b6ff));
    }
}

// ---------------------------------------------------------------- Editor
void MasterHeadEditor::setupKnob (Knob& k, const char* id, const juce::String& name, juce::Colour c, Fmt f, double def)
{
    k.fmt = f;
    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    k.slider.setColour (juce::Slider::rotarySliderFillColourId, c);
    k.slider.setLookAndFeel (&lnf);
    k.slider.setDoubleClickReturnValue (true, def);

    k.name.setText (name, juce::dontSendNotification);
    k.name.setJustificationType (juce::Justification::centred);
    k.name.setFont (juce::Font (juce::FontOptions (16.f)));
    k.name.setColour (juce::Label::textColourId, col::text);

    k.value.setJustificationType (juce::Justification::centred);
    k.value.setFont (juce::Font (juce::FontOptions (13.f)));
    k.value.setColour (juce::Label::textColourId, col::text);
    k.value.setColour (juce::Label::backgroundColourId, col::cell);
    k.value.setInterceptsMouseClicks (false, false);
    k.name.setInterceptsMouseClicks (false, false);

    Knob* kp = &k;
    k.slider.onValueChange = [kp]
    {
        const double v = kp->slider.getValue();
        juce::String t;
        switch (kp->fmt)
        {
            case Fmt::Pct:       t = juce::String ((int) std::lround (v)) + "%"; break;
            case Fmt::PctSigned: t = (v >= 0 ? "+" : "") + juce::String ((int) std::lround (v)) + "%"; break;
            case Fmt::Db:        t = juce::String (v, 1); break;
            case Fmt::DbSigned:  t = sgn ((float) v); break;
            case Fmt::DbSuffix:  t = sgn ((float) v) + " dB"; break;
        }
        kp->value.setText (t, juce::dontSendNotification);
    };

    canvas.addAndMakeVisible (k.slider);
    canvas.addAndMakeVisible (k.name);
    canvas.addAndMakeVisible (k.value);
    k.att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, k.slider);
    k.slider.onValueChange();
}

void MasterHeadEditor::setupLed (Led& l, const char* id)
{
    canvas.addAndMakeVisible (l.button);
    l.att = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, id, l.button);
}

void MasterHeadEditor::placeKnob (Knob& k, juce::Rectangle<int> cell, int dia)
{
    dia = juce::jmin (dia, cell.getHeight() - 18);
    const juce::Rectangle<int> s (cell.getX() + 10, cell.getY(), dia, dia);
    k.slider.setBounds (s);
    k.name.setBounds (s.getX() - 14, s.getBottom() + 1, dia + 28, 18);
    k.value.setBounds (s.getRight() + 4, cell.getY() + 4, 46, 18);
}

MasterHeadEditor::MasterHeadEditor (MasterHeadProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setupKnob (kIn,     ids::in,     "Input",  col::accent,   Fmt::DbSuffix,  0.0);
    setupKnob (kSpec,   ids::spec,   "Amount", col::spectral, Fmt::Pct,       0.0);
    setupKnob (kGlue,   ids::glue,   "Glue",   col::comp,     Fmt::Pct,       0.0);
    setupKnob (kDetail, ids::detail, "Detail", col::comp,     Fmt::Pct,       0.0);
    setupKnob (kColor,  ids::sat,    "Color",  col::analog,   Fmt::Pct,       0.0);
    setupKnob (kWidth,  ids::width,  "Width",  col::stereo,   Fmt::Pct,     100.0);
    setupKnob (kBass,   ids::low,    "Bass",   col::eq,       Fmt::PctSigned, 0.0);
    setupKnob (kAir,    ids::high,   "Air",    col::eq,       Fmt::PctSigned, 0.0);
    setupKnob (kClip,   ids::clip,   "Gain",   col::clip,     Fmt::Db,        0.0);
    setupKnob (kPush,   ids::push,   "Gain",   col::limiter,  Fmt::DbSigned,  0.0);

    // Input : libellé et valeur discrets dans la barre du haut
    kIn.name.setFont (juce::Font (juce::FontOptions (20.f)));
    kIn.name.setJustificationType (juce::Justification::centredLeft);
    kIn.value.setFont (juce::Font (juce::FontOptions (11.f)));
    kIn.value.setColour (juce::Label::textColourId, col::dim);
    kIn.value.setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    kIn.value.setJustificationType (juce::Justification::centredLeft);

    setupLed (lSpec, ids::specOn);     setupLed (lComp, ids::compOn);   setupLed (lAnalog, ids::analogOn);
    setupLed (lStereo, ids::stereoOn); setupLed (lEq, ids::eqOn);       setupLed (lClip, ids::clipOn);
    setupLed (lLim, ids::limOn);

    for (auto* b : { &pLM, &pTP, &pBypass }) canvas.addAndMakeVisible (*b);
    aLM     = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, ids::bassOn, pLM);
    aTP     = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, ids::tp, pTP);
    aBypass = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, ids::bypass, pBypass);

    for (auto* b : { &undoBtn, &redoBtn }) { b->setLookAndFeel (&lnf); canvas.addAndMakeVisible (*b); }
    undoBtn.onClick = [this] { proc.undo(); };
    redoBtn.onClick = [this] { proc.redo(); };

    for (auto* b : { &btnA, &btnB, &btnAB }) { b->setLookAndFeel (&lnf); canvas.addAndMakeVisible (*b); }
    btnA.onClick  = [this] { proc.selectSlot (0); };
    btnB.onClick  = [this] { proc.selectSlot (1); };
    btnAB.onClick = [this] { proc.copyToOtherSlot(); };

    canvas.addAndMakeVisible (assistBtn);
    assistBtn.setButtonText ("ASSIST");
    assistBtn.onClick = [this]
    {
        if (proc.getAssistState() == MasterHeadProcessor::Analyzing) proc.cancelAssist();
        else                                                          proc.requestAssist();
    };

    targetBox.setLookAndFeel (&lnf);
    targetBox.addItemList ({ "-14 STREAM", "-11 LOUD", "-9 CLUB" }, 1);
    canvas.addAndMakeVisible (targetBox);
    aTarget = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, ids::target, targetBox);

    monitorBox.setLookAndFeel (&lnf);
    monitorBox.addItemList ({ "None", "Mid", "Side", "Delta" }, 1);
    canvas.addAndMakeVisible (monitorBox);
    aMonitor = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, ids::monitor, monitorBox);

    summary.setFont (juce::Font (juce::FontOptions (11.f)));
    summary.setColour (juce::Label::textColourId, col::dim);
    summary.setJustificationType (juce::Justification::topLeft);
    summary.setMinimumHorizontalScale (1.0f);
    canvas.addAndMakeVisible (summary);

    for (juce::Component* c : { (juce::Component*) &inMeter, (juce::Component*) &curve, (juce::Component*) &compBars,
                                (juce::Component*) &detailBar, (juce::Component*) &clipBar, (juce::Component*) &limBar,
                                (juce::Component*) &analogLeds, (juce::Component*) &stereoIcon, (juce::Component*) &monitorBar })
        canvas.addAndMakeVisible (*c);
    monitorBar.toBack();   // les widgets (Mix, Monitor) se posent par-dessus

    aCompOut = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, ids::compOut, compOutBtn);
    canvas.addAndMakeVisible (compOutBtn);

    addAndMakeVisible (canvas);
    setResizable (true, true);
    setResizeLimits (480, 484, 2400, 2420);
    setSize (proc.editorW >= 480 && proc.editorH >= 484 ? proc.editorW : kBaseW,
             proc.editorW >= 480 && proc.editorH >= 484 ? proc.editorH : kBaseH);
    startTimerHz (30);
}

MasterHeadEditor::~MasterHeadEditor()
{
    for (auto* k : { &kIn, &kSpec, &kGlue, &kDetail, &kColor, &kWidth, &kBass, &kAir, &kClip, &kPush })
        k->slider.setLookAndFeel (nullptr);
    undoBtn.setLookAndFeel (nullptr);  redoBtn.setLookAndFeel (nullptr);
    btnA.setLookAndFeel (nullptr);  btnB.setLookAndFeel (nullptr);  btnAB.setLookAndFeel (nullptr);
    targetBox.setLookAndFeel (nullptr);  monitorBox.setLookAndFeel (nullptr);
}

void MasterHeadEditor::updateSpectrum()
{
    const double sr = proc.getSr();
    if (sr <= 0.0) return;
    auto& m = proc.meters;
    const int w = m.scopeW.load (std::memory_order_acquire);
    for (int i = 0; i < 2048; ++i) fftBuf[(size_t) i] = m.scope[(size_t) ((w - 2048 + i) & 4095)].load (std::memory_order_relaxed);
    for (size_t i = 2048; i < fftBuf.size(); ++i) fftBuf[i] = 0.f;
    window.multiplyWithWindowingTable (fftBuf.data(), 2048);
    fft.performFrequencyOnlyForwardTransform (fftBuf.data());

    for (int b = 0; b < kSpecBars; ++b)
    {
        const double f0 = 30.0 * std::pow (18000.0 / 30.0, (double) b / kSpecBars);
        const double f1 = 30.0 * std::pow (18000.0 / 30.0, (double) (b + 1) / kSpecBars);
        const int b0 = juce::jlimit (1, 1023, (int) (f0 * 2048.0 / sr));
        const int b1 = juce::jlimit (b0, 1023, (int) (f1 * 2048.0 / sr));
        float mag = 0.f;
        for (int k = b0; k <= b1; ++k) mag = juce::jmax (mag, fftBuf[(size_t) k]);
        const float db = 20.f * std::log10 (mag / 512.f + 1.0e-9f);
        const float v = juce::jlimit (0.f, 1.f, (db + 80.f) / 80.f);
        data.bars[(size_t) b] = juce::jmax (v, data.bars[(size_t) b] - 0.03f);
    }
}

void MasterHeadEditor::timerCallback()
{
    auto& m = proc.meters;
    const float vIn[2]  { toDb (m.inPeakL.exchange (0.f)),  toDb (m.inPeakR.exchange (0.f)) };
    const float vOut[2] { toDb (m.outPeakL.exchange (0.f)), toDb (m.outPeakR.exchange (0.f)) };
    for (int i = 0; i < 2; ++i)
    {
        data.inDb[i]  = juce::jmax (vIn[i],  data.inDb[i]  - 1.5f);
        data.outDb[i] = juce::jmax (vOut[i], data.outDb[i] - 1.5f);
        data.maxOutDb = juce::jmax (data.maxOutDb, vOut[i]);
    }
    for (size_t k = 0; k < 3; ++k) data.bandGR[k] = juce::jmax (m.bandGR[k].exchange (0.f), data.bandGR[k] - 0.4f);
    data.detailUp = juce::jmax (m.detailUp.exchange (0.f), data.detailUp - 0.2f);
    data.clipRed  = juce::jmax (m.clipRed.exchange (0.f),  data.clipRed  - 0.3f);
    data.limGR    = juce::jmax (m.limGR.exchange (0.f),    data.limGR    - 0.4f);
    data.analogDb = juce::jmax (toDb (m.analogLvl.exchange (0.f)), data.analogDb - 1.5f);
    for (size_t k = 0; k < kSpecBands; ++k) data.specGain[k] = m.specGain[k].load (std::memory_order_relaxed);
    data.corr  = m.correlation.load();
    data.lufsM = m.lufsM.load();
    data.lufsS = m.lufsS.load();
    data.lufsI = m.lufsI.load();
    data.psr   = m.psr.load();
    updateSpectrum();

    const int st = proc.getAssistState();
    if (st == MasterHeadProcessor::Analyzing)
    {
        assistBtn.setButtonText (juce::String ((int) (proc.getAssistProgress() * 100.f)) + "%");
        summary.setText ("Assist : joue la partie la plus forte du morceau (10 s). Clique pour annuler.", juce::dontSendNotification);
    }
    else
    {
        assistBtn.setButtonText ("ASSIST");
        if (st == MasterHeadProcessor::NoData)
            summary.setText ("Assist : pas assez de signal, joue le morceau pendant l'analyse.", juce::dontSendNotification);
        else if (st == MasterHeadProcessor::Done)
            summary.setText (proc.getAssistSummary(), juce::dontSendNotification);
        else
            summary.setText ("ASSIST : lance la lecture, choisis une cible, clique et laisse jouer 10 s.", juce::dontSendNotification);
    }
    btnA.setColour (juce::TextButton::buttonColourId, proc.getActiveSlot() == 0 ? col::analog : col::cell);
    btnB.setColour (juce::TextButton::buttonColourId, proc.getActiveSlot() == 1 ? col::analog : col::cell);
    undoBtn.setEnabled (proc.canUndo());
    redoBtn.setEnabled (proc.canRedo());

    repaint();
}

void MasterHeadEditor::drawPanel (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& name, juce::Colour c)
{
    g.setColour (col::panel);  g.fillRoundedRectangle (r.toFloat(), 8.f);
    g.setColour (col::track.withAlpha (0.7f));  g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 8.f, 1.f);
    const auto chip = juce::Rectangle<float> ((float) r.getX() + 12.f, (float) r.getY() + 10.f, 112.f, 28.f);
    g.setColour (c.withAlpha (0.22f));  g.fillRoundedRectangle (chip, 5.f);
    g.setColour (c);
    g.setFont (juce::Font (juce::FontOptions (17.f, juce::Font::bold | juce::Font::italic)));
    g.drawText (name, chip.toNearestInt(), juce::Justification::centred);
}

void MasterHeadEditor::paint (juce::Graphics& g)
{
    // Fond + bandes du haut sur toute la largeur (la toile centrale est mise à l'échelle par-dessus)
    g.fillAll (col::bg);
    const float s = juce::jmin ((float) getWidth() / (float) kBaseW, (float) getHeight() / (float) kBaseH);
    const float oy = ((float) getHeight() - (float) kBaseH * s) * 0.5f;
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2b2154), 0.f, oy + 48.f * s, col::bg, 0.f, oy + 156.f * s, false));
    g.fillRect (0.f, oy + 48.f * s, (float) getWidth(), 108.f * s);
    g.setColour (juce::Colour (0xff0f0e14));
    g.fillRect (0.f, 0.f, (float) getWidth(), oy + 48.f * s);
}

void MasterHeadEditor::paintCanvas (juce::Graphics& g)
{
    g.fillAll (col::bg);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2b2154), 0.f, 48.f, col::bg, 0.f, 156.f, false));
    g.fillRect (0, 48, kBaseW, 108);
    g.setColour (juce::Colour (0xff0f0e14));
    g.fillRect (0, 0, kBaseW, 48);

    g.setColour (col::text);
    g.setFont (juce::Font (juce::FontOptions (40.f, juce::Font::bold | juce::Font::italic)));
    g.drawText ("MasterHead", 18, 52, 360, 48, juce::Justification::centredLeft);
    g.setColour (col::accent);
    g.setFont (juce::Font (juce::FontOptions (14.f, juce::Font::bold)));
    g.drawText ("BoodmanFX", 22, 96, 200, 18, juce::Justification::centredLeft);

    drawPanel (g, rSpec,   "SPECTRAL", col::spectral);
    drawPanel (g, rComp,   "COMP",     col::comp);
    drawPanel (g, rAnalog, "ANALOG",   col::analog);
    drawPanel (g, rStereo, "STEREO",   col::stereo);
    drawPanel (g, rEq,     "EQ",       col::eq);
    drawPanel (g, rClip,   "CLIP",     col::clip);
    drawPanel (g, rLim,    "LIMITER",  col::limiter);
}

void MasterHeadEditor::layoutCanvas()
{
    const int W = kBaseW, pad = 12;

    // Barre du haut
    kIn.slider.setBounds (8, 6, 36, 36);
    kIn.name.setBounds (50, 3, 80, 24);
    kIn.value.setBounds (52, 27, 80, 14);
    inMeter.setBounds (136, 8, 270, 32);
    undoBtn.setBounds (416, 10, 52, 28);
    redoBtn.setBounds (472, 10, 52, 28);
    btnA.setBounds (544, 10, 34, 28);
    btnB.setBounds (582, 10, 34, 28);
    btnAB.setBounds (622, 10, 62, 28);
    pBypass.setBounds (W - 100, 11, 88, 26);

    // En-tête
    summary.setBounds (18, 118, W - 160, 34);
    assistBtn.setBounds (W - 110, 54, 98, 98);
    targetBox.setBounds (W - 250, 90, 128, 26);

    // Modules
    const int fullW = W - 2 * pad, halfW = (fullW - 8) / 2;
    rSpec   = { pad, 156, fullW, 118 };
    rComp   = { pad, 282, fullW, 108 };
    rAnalog = { pad, 398, halfW, 100 };
    rStereo = { pad + halfW + 8, 398, halfW, 100 };
    rEq     = { pad, 506, fullW, 96 };
    rClip   = { pad, 610, halfW, 100 };
    rLim    = { pad + halfW + 8, 610, halfW, 100 };

    auto led = [] (Led& l, juce::Rectangle<int> r) { l.button.setBounds (r.getX() + 14, r.getBottom() - 32, 56, 20); };
    led (lSpec, rSpec); led (lComp, rComp); led (lAnalog, rAnalog); led (lStereo, rStereo);
    led (lEq, rEq);     led (lClip, rClip); led (lLim, rLim);

    // Spectral
    placeKnob (kSpec, { rSpec.getX() + 130, rSpec.getY() + 8, 130, rSpec.getHeight() - 14 }, 76);
    curve.setBounds (rSpec.getX() + 290, rSpec.getY() + 14, rSpec.getWidth() - 306, rSpec.getHeight() - 28);

    // Comp
    placeKnob (kGlue, { rComp.getX() + 130, rComp.getY() + 6, 130, rComp.getHeight() - 10 }, 70);
    compBars.setBounds (rComp.getX() + 300, rComp.getY() + 14, 64, rComp.getHeight() - 24);
    compOutBtn.setBounds (rComp.getRight() - 72, rComp.getY() + 20, 60, 72);
    detailBar.setBounds (rComp.getRight() - 256, rComp.getY() + 14, 14, rComp.getHeight() - 40);
    placeKnob (kDetail, { rComp.getRight() - 224, rComp.getY() + 6, 130, rComp.getHeight() - 10 }, 70);

    // Analog / Stereo
    placeKnob (kColor, { rAnalog.getX() + 130, rAnalog.getY() + 6, 130, rAnalog.getHeight() - 10 }, 66);
    analogLeds.setBounds (rAnalog.getRight() - 100, rAnalog.getY() + 24, 80, 54);
    placeKnob (kWidth, { rStereo.getX() + 130, rStereo.getY() + 6, 130, rStereo.getHeight() - 10 }, 66);
    pLM.setBounds (rStereo.getX() + 78, rStereo.getBottom() - 34, 40, 24);
    stereoIcon.setBounds (rStereo.getRight() - 118, rStereo.getY() + 32, 100, 36);

    // EQ
    placeKnob (kBass, { rEq.getX() + 130, rEq.getY() + 4, 130, rEq.getHeight() - 8 }, 62);
    placeKnob (kAir,  { rEq.getX() + (int) (rEq.getWidth() * 0.62f), rEq.getY() + 4, 130, rEq.getHeight() - 8 }, 62);

    // Clip / Limiter
    placeKnob (kClip, { rClip.getX() + 130, rClip.getY() + 6, 130, rClip.getHeight() - 10 }, 66);
    clipBar.setBounds (rClip.getRight() - 40, rClip.getY() + 14, 16, rClip.getHeight() - 28);
    placeKnob (kPush, { rLim.getX() + 130, rLim.getY() + 6, 130, rLim.getHeight() - 10 }, 66);
    pTP.setBounds (rLim.getRight() - 110, rLim.getBottom() - 36, 40, 24);
    limBar.setBounds (rLim.getRight() - 40, rLim.getY() + 14, 16, rLim.getHeight() - 28);

    // Barre de monitoring
    const juce::Rectangle<int> barB (pad, 718, fullW, 78);
    monitorBar.setBounds (barB);
    const auto cells = MonitorBar::cells (monitorBar.getLocalBounds());
    {
        const auto c0 = cells[0].translated (barB.getX(), barB.getY());
        monitorBox.setBounds (c0.getX() + 6, c0.getY() + 30, c0.getWidth() - 12, 28);
    }
}

void MasterHeadEditor::resized()
{
    const float s = juce::jmin ((float) getWidth() / (float) kBaseW, (float) getHeight() / (float) kBaseH);
    canvas.setBounds (0, 0, kBaseW, kBaseH);
    canvas.setTransform (juce::AffineTransform::scale (s)
                             .translated (((float) getWidth()  - (float) kBaseW * s) * 0.5f,
                                          ((float) getHeight() - (float) kBaseH * s) * 0.5f));
    if (getWidth() > 0 && getHeight() > 0) { proc.editorW = getWidth(); proc.editorH = getHeight(); }
}
