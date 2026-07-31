#pragma once
#include <cmath>

namespace dsp {
struct Biquad {
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    float z1 = 0.0f, z2 = 0.0f;

    inline float processSample(float x) {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void reset() { z1 = z2 = 0.0f; }

    // RBJ Audio EQ Cookbook formulas. Returns coeffs with a0 normalized to 1.
    static Biquad lowShelf(float sr, float freq, float gainDb) {
        Biquad bq; const float A = std::pow(10.0f, gainDb / 40.0f);
        const float w0 = 2.0f * 3.14159265358979f * freq / sr;
        const float cs = std::cos(w0), sn = std::sin(w0);
        const float alpha = sn / 2.0f * std::sqrt((A + 1.0f/A) * (1.0f/0.9f - 1.0f) + 2.0f);
        const float tsa = 2.0f * std::sqrt(A) * alpha;
        const float a0 = (A+1) + (A-1)*cs + tsa;
        bq.b0 = A*((A+1) - (A-1)*cs + tsa) / a0;
        bq.b1 = 2*A*((A-1) - (A+1)*cs)     / a0;
        bq.b2 = A*((A+1) - (A-1)*cs - tsa) / a0;
        bq.a1 = -2*((A-1) + (A+1)*cs)      / a0;
        bq.a2 = ((A+1) + (A-1)*cs - tsa)   / a0;
        return bq;
    }
    static Biquad peaking(float sr, float freq, float q, float gainDb) {
        Biquad bq; const float A = std::pow(10.0f, gainDb / 40.0f);
        const float w0 = 2.0f * 3.14159265358979f * freq / sr;
        const float cs = std::cos(w0), sn = std::sin(w0);
        const float alpha = sn / (2.0f * q);
        const float a0 = 1 + alpha/A;
        bq.b0 = (1 + alpha*A) / a0;
        bq.b1 = (-2*cs)       / a0;
        bq.b2 = (1 - alpha*A) / a0;
        bq.a1 = (-2*cs)       / a0;
        bq.a2 = (1 - alpha/A) / a0;
        return bq;
    }
    static Biquad highShelf(float sr, float freq, float gainDb) {
        Biquad bq; const float A = std::pow(10.0f, gainDb / 40.0f);
        const float w0 = 2.0f * 3.14159265358979f * freq / sr;
        const float cs = std::cos(w0), sn = std::sin(w0);
        const float alpha = sn / 2.0f * std::sqrt((A + 1.0f/A) * (1.0f/0.9f - 1.0f) + 2.0f);
        const float tsa = 2.0f * std::sqrt(A) * alpha;
        const float a0 = (A+1) - (A-1)*cs + tsa;
        bq.b0 = A*((A+1) + (A-1)*cs + tsa) / a0;
        bq.b1 = -2*A*((A-1) + (A+1)*cs)    / a0;
        bq.b2 = A*((A+1) + (A-1)*cs - tsa) / a0;
        bq.a1 = 2*((A-1) - (A+1)*cs)       / a0;
        bq.a2 = ((A+1) - (A-1)*cs - tsa)   / a0;
        return bq;
    }
};
} // namespace dsp
