#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

// Limiteur lookahead (peak, stereo-linked).
// Garantie : le gain appliqué à l'échantillon retardé est <= au gain requis par ce
// même échantillon (min glissant sur N+1 échantillons, puis moyenne sur N).
// Un clamp final de sécurité empêche tout dépassement du plafond.
class LookaheadLimiter
{
public:
    void prepare (double sampleRate)
    {
        N = std::max (1, (int) std::lround (sampleRate * 0.002)); // 2 ms de lookahead
        delayL.assign ((size_t) N, 0.f);
        delayR.assign ((size_t) N, 0.f);
        req.assign ((size_t) N + 1, 1.f);
        mins.assign ((size_t) N, 1.f);
        dPos = rPos = mPos = 0;
        sum = (double) N;
        state = 1.f;
        for (int i = 0; i < 3; ++i) hL[i] = hR[i] = 0.f;
        relAlpha = 1.f - std::exp (-1.f / (0.1f * (float) sampleRate)); // release ~100 ms
    }

    int getLatency() const { return N; }

    // Modifie l et r en place, renvoie le gain courant (<= 1).
    // truePeak : estime les crêtes inter-échantillons par interpolation cubique (x4) - estimation, pas certification.
    float process (float& l, float& r, float ceiling, bool truePeak = false)
    {
        float peak = std::max (std::abs (l), std::abs (r));
        if (truePeak)
        {
            peak = std::max (peak, interpPeak (hL[2], hL[1], hL[0], l));
            peak = std::max (peak, interpPeak (hR[2], hR[1], hR[0], r));
        }
        hL[2] = hL[1]; hL[1] = hL[0]; hL[0] = l;
        hR[2] = hR[1]; hR[1] = hR[0]; hR[0] = r;
        const float rq = peak > ceiling ? ceiling / peak : 1.f;

        req[(size_t) rPos] = rq;
        if (++rPos > N) rPos = 0;

        float m = 1.f;
        for (float v : req) m = std::min (m, v);

        sum += (double) m - (double) mins[(size_t) mPos];
        mins[(size_t) mPos] = m;
        if (++mPos >= N) mPos = 0;

        const float avg = std::min (1.f, (float) (sum / (double) N));

        if (avg < state) state = avg;
        else             state += (avg - state) * relAlpha;

        const float dl = delayL[(size_t) dPos];
        const float dr = delayR[(size_t) dPos];
        delayL[(size_t) dPos] = l;
        delayR[(size_t) dPos] = r;
        if (++dPos >= N) dPos = 0;

        l = std::clamp (dl * state, -ceiling, ceiling);
        r = std::clamp (dr * state, -ceiling, ceiling);
        return state;
    }

private:
    // Catmull-Rom entre p1 et p2 (p0 avant, p3 après) à t = 0.25, 0.5, 0.75
    static float interpPeak (float p0, float p1, float p2, float p3)
    {
        float m = 0.f;
        for (float t : { 0.25f, 0.5f, 0.75f })
        {
            const float v = 0.5f * ((2.f * p1) + (-p0 + p2) * t + (2.f * p0 - 5.f * p1 + 4.f * p2 - p3) * t * t
                                    + (-p0 + 3.f * p1 - 3.f * p2 + p3) * t * t * t);
            m = std::max (m, std::abs (v));
        }
        return m;
    }

    float hL[3] { 0.f, 0.f, 0.f }, hR[3] { 0.f, 0.f, 0.f };
    int N = 1, dPos = 0, rPos = 0, mPos = 0;
    std::vector<float> delayL, delayR, req, mins;
    double sum = 1.0;
    float state = 1.f, relAlpha = 0.001f;
};
