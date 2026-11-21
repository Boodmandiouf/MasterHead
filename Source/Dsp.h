#pragma once
#include <cmath>
#include <complex>

// ---- Biquads maison (pas d'allocation, état en double) + réponse en fréquence pour l'affichage
struct BiquadCoeffs { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };

namespace dspc
{
    inline BiquadCoeffs norm (double b0, double b1, double b2, double a0, double a1, double a2)
    { return { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 }; }

    inline BiquadCoeffs peaking (double sr, double f, double Q, double gainDb)
    {
        const double A = std::pow (10.0, gainDb / 40.0), w = 2.0 * 3.14159265358979323846 * f / sr;
        const double c = std::cos (w), al = std::sin (w) / (2.0 * Q);
        return norm (1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
    }
    inline BiquadCoeffs bandpass (double sr, double f, double Q)
    {
        const double w = 2.0 * 3.14159265358979323846 * f / sr, c = std::cos (w), al = std::sin (w) / (2.0 * Q);
        return norm (al, 0, -al, 1 + al, -2 * c, 1 - al);
    }
    inline BiquadCoeffs lowpass (double sr, double f, double Q = 0.70710678)
    {
        const double w = 2.0 * 3.14159265358979323846 * f / sr, c = std::cos (w), al = std::sin (w) / (2.0 * Q);
        return norm ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
    }
    inline BiquadCoeffs highpass (double sr, double f, double Q = 0.70710678)
    {
        const double w = 2.0 * 3.14159265358979323846 * f / sr, c = std::cos (w), al = std::sin (w) / (2.0 * Q);
        return norm ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
    }
    inline double magnitudeDb (const BiquadCoeffs& k, double f, double sr)
    {
        const double w = 2.0 * 3.14159265358979323846 * f / sr;
        const std::complex<double> z1 = std::polar (1.0, -w), z2 = std::polar (1.0, -2.0 * w);
        const auto num = k.b0 + k.b1 * z1 + k.b2 * z2;
        const auto den = 1.0 + k.a1 * z1 + k.a2 * z2;
        return 20.0 * std::log10 (std::abs (num / den) + 1.0e-12);
    }
}

class Biquad
{
public:
    void set (const BiquadCoeffs& k) { c = k; }
    void reset() { z1 = z2 = 0.0; }
    float process (float x)
    {
        const double in = (double) x;
        const double y = c.b0 * in + z1;
        z1 = c.b1 * in - c.a1 * y + z2;
        z2 = c.b2 * in - c.a2 * y;
        return (float) y;
    }
private:
    BiquadCoeffs c;
    double z1 = 0.0, z2 = 0.0;
};

// Séparateur 3 bandes Linkwitz-Riley 4e ordre, somme plate (passe-tout de compensation sur les graves)
struct Crossover3
{
    Biquad lp1[2], hp1[2], lp2[2], hp2[2], apL[2], apH[2];

    void prepare (double sr, double f1, double f2)
    {
        for (int i = 0; i < 2; ++i)
        {
            lp1[i].set (dspc::lowpass (sr, f1));  hp1[i].set (dspc::highpass (sr, f1));
            lp2[i].set (dspc::lowpass (sr, f2));  hp2[i].set (dspc::highpass (sr, f2));
            apL[i].set (dspc::lowpass (sr, f2));  apH[i].set (dspc::highpass (sr, f2));
        }
        reset();
    }
    void reset()
    {
        for (int i = 0; i < 2; ++i) { lp1[i].reset(); hp1[i].reset(); lp2[i].reset(); hp2[i].reset(); apL[i].reset(); apH[i].reset(); }
    }
    void split (float x, float& lo, float& mi, float& hi)
    {
        const float l = lp1[1].process (lp1[0].process (x));
        const float r = hp1[1].process (hp1[0].process (x));
        mi = lp2[1].process (lp2[0].process (r));
        hi = hp2[1].process (hp2[0].process (r));
        lo = apL[1].process (apL[0].process (l)) + apH[1].process (apH[0].process (l));
    }
};

inline constexpr int kSpecBands = 6;
inline constexpr float kSpecFreq[kSpecBands]   = { 80.f, 200.f, 500.f, 1400.f, 4000.f, 10000.f };
inline constexpr float kSpecTarget[kSpecBands] = { 4.f, 2.f, 0.5f, -1.f, -3.f, -6.f }; // hypothèse : forme cible (dB, relative)
inline constexpr float kSpecQ = 0.9f;
