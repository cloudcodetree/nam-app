#include "dsp/PitchDetector.h"

#include <cmath>
#include <vector>

namespace dsp {

float detectPitchHz(const float* x, int n, double sr) {
    if (x == nullptr || n <= 0 || sr <= 0) return 0.0f;

    // Guitar/bass range: ~35 Hz (low B on a 5-string bass) to ~500 Hz.
    const int maxLag = (int)(sr / 35.0);
    const int minLag = (int)(sr / 500.0);
    if (n < maxLag + minLag || minLag < 2) return 0.0f;

    // Energy gate: don't chase noise. ~-60 dBFS — low enough to track a
    // decaying note's tail; YIN's periodicity threshold rejects hiss.
    double energy = 0.0;
    for (int i = 0; i < n; ++i) energy += (double)x[i] * x[i];
    const double rms = std::sqrt(energy / n);
    if (rms < 0.001) return 0.0f;

    const int W = n - maxLag;   // comparison window

    // YIN difference function d(tau), then cumulative-mean normalisation.
    static thread_local std::vector<float> nd;
    nd.assign((size_t)maxLag + 1, 1.0f);
    double runningSum = 0.0;
    for (int tau = minLag; tau <= maxLag; ++tau) {
        double s = 0.0;
        for (int i = 0; i < W; ++i) {
            const float diff = x[i] - x[i + tau];
            s += (double)diff * diff;
        }
        runningSum += s;
        nd[(size_t)tau] = runningSum > 0.0 ? (float)(s * (tau - minLag + 1) / runningSum) : 1.0f;
    }

    // First dip under threshold; walk to its local minimum.
    constexpr float kThreshold = 0.15f;
    int best = -1;
    for (int tau = minLag; tau <= maxLag; ++tau) {
        if (nd[(size_t)tau] < kThreshold) {
            while (tau + 1 <= maxLag && nd[(size_t)(tau + 1)] < nd[(size_t)tau]) ++tau;
            best = tau;
            break;
        }
    }
    if (best < 0) {
        // Fallback: global minimum if it is at least somewhat periodic.
        float minVal = 0.35f;
        for (int tau = minLag; tau <= maxLag; ++tau)
            if (nd[(size_t)tau] < minVal) {
                minVal = nd[(size_t)tau];
                best = tau;
            }
        if (best < 0) return 0.0f;
    }

    // Parabolic interpolation around the minimum for sub-sample precision.
    double lag = best;
    if (best > minLag && best < maxLag) {
        const double a = nd[(size_t)(best - 1)];
        const double b = nd[(size_t)best];
        const double c = nd[(size_t)(best + 1)];
        const double denom = a - 2.0 * b + c;
        if (std::fabs(denom) > 1e-12) lag += 0.5 * (a - c) / denom;
    }
    if (lag <= 0.0) return 0.0f;
    return (float)(sr / lag);
}

}   // namespace dsp
