#pragma once
// Generic 1D separable convolution — both Gaussian blur and Sobel edge
// detection are "separable" filters: instead of a full 2D kernel applied to
// every pixel (O(k^2) work per pixel), you apply a small 1D kernel
// horizontally, then a small 1D kernel vertically (O(2k) work per pixel).
// Writing one generic engine and feeding it different small kernels avoids
// duplicating the same convolution logic per filter.

#include <immintrin.h>
#include <vector>
#include <algorithm>
#include <cstdint>
#include <cmath>

// --- Scalar reference implementations (used both as the real scalar path
//     and as ground truth to verify the AVX2 path against) -----------------

inline void convolveHorizontalScalar(const float* row, float* out, int width,
                                      const std::vector<float>& kernel) {
    int radius = static_cast<int>(kernel.size()) / 2;
    std::vector<float> padded(width + 2 * radius);
    for (int i = 0; i < radius; i++) padded[i] = row[0];                  // clamp left edge
    for (int x = 0; x < width; x++) padded[radius + x] = row[x];
    for (int i = 0; i < radius; i++) padded[radius + width + i] = row[width - 1]; // clamp right edge

    for (int x = 0; x < width; x++) {
        float sum = 0.f;
        for (size_t k = 0; k < kernel.size(); k++) sum += padded[x + k] * kernel[k];
        out[x] = sum;
    }
}

inline void convolveVerticalScalar(const std::vector<float>& imgIn, std::vector<float>& imgOut,
                                    int width, int height, const std::vector<float>& kernel) {
    int radius = static_cast<int>(kernel.size()) / 2;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            float sum = 0.f;
            for (size_t k = 0; k < kernel.size(); k++) {
                int sy = std::clamp(y - radius + static_cast<int>(k), 0, height - 1);
                sum += imgIn[sy * width + x] * kernel[k];
            }
            imgOut[y * width + x] = sum;
        }
    }
}

// --- AVX2 implementations: same math, 8 pixels processed per instruction --

inline void convolveHorizontalAVX2(const float* row, float* out, int width,
                                    const std::vector<float>& kernel) {
    int radius = static_cast<int>(kernel.size()) / 2;
    std::vector<float> padded(width + 2 * radius);
    for (int i = 0; i < radius; i++) padded[i] = row[0];
    for (int x = 0; x < width; x++) padded[radius + x] = row[x];
    for (int i = 0; i < radius; i++) padded[radius + width + i] = row[width - 1];

    int x = 0;
    for (; x + 8 <= width; x += 8) {
        __m256 sum = _mm256_setzero_ps();
        for (size_t k = 0; k < kernel.size(); k++) {
            __m256 p = _mm256_loadu_ps(&padded[x + k]);       // 8 contiguous floats, offset by tap k
            __m256 w = _mm256_set1_ps(kernel[k]);              // broadcast this tap's weight to all 8 lanes
            sum = _mm256_add_ps(sum, _mm256_mul_ps(p, w));
        }
        _mm256_storeu_ps(&out[x], sum);
    }
    // Scalar tail: handles the last (width % 8) pixels that don't fill a full vector.
    for (; x < width; x++) {
        float s = 0.f;
        for (size_t k = 0; k < kernel.size(); k++) s += padded[x + k] * kernel[k];
        out[x] = s;
    }
}

inline void convolveVerticalAVX2(const std::vector<float>& imgIn, std::vector<float>& imgOut,
                                  int width, int height, const std::vector<float>& kernel) {
    int radius = static_cast<int>(kernel.size()) / 2;
    for (int y = 0; y < height; y++) {
        int x = 0;
        for (; x + 8 <= width; x += 8) {
            __m256 sum = _mm256_setzero_ps();
            for (size_t k = 0; k < kernel.size(); k++) {
                int sy = std::clamp(y - radius + static_cast<int>(k), 0, height - 1);
                __m256 p = _mm256_loadu_ps(&imgIn[sy * width + x]); // 8 contiguous floats, same row
                __m256 w = _mm256_set1_ps(kernel[k]);
                sum = _mm256_add_ps(sum, _mm256_mul_ps(p, w));
            }
            _mm256_storeu_ps(&imgOut[y * width + x], sum);
        }
        for (; x < width; x++) {
            float s = 0.f;
            for (size_t k = 0; k < kernel.size(); k++) {
                int sy = std::clamp(y - radius + static_cast<int>(k), 0, height - 1);
                s += imgIn[sy * width + x] * kernel[k];
            }
            imgOut[y * width + x] = s;
        }
    }
}

// --- Named filters built on top of the convolution engine ------------------

// 5-tap Gaussian, sigma ~= 1.0 — a standard smoothing kernel.
inline const std::vector<float>& gaussianKernel() {
    static const std::vector<float> k = {1.f/16, 4.f/16, 6.f/16, 4.f/16, 1.f/16};
    return k;
}

inline void gaussianBlur(const std::vector<float>& in, std::vector<float>& out,
                          int width, int height, bool useAVX2) {
    std::vector<float> temp(in.size());
    out.resize(in.size());
    const auto& k = gaussianKernel();
    for (int y = 0; y < height; y++) {
        const float* row = &in[y * width];
        float* tempRow = &temp[y * width];
        if (useAVX2) convolveHorizontalAVX2(row, tempRow, width, k);
        else         convolveHorizontalScalar(row, tempRow, width, k);
    }
    if (useAVX2) convolveVerticalAVX2(temp, out, width, height, k);
    else         convolveVerticalScalar(temp, out, width, height, k);
}

// Sobel edge detection: Gx = horizontal derivative + vertical smoothing,
// Gy = vertical derivative + horizontal smoothing, magnitude = sqrt(Gx^2+Gy^2).
// This is the separable form of the classic 3x3 Sobel kernel.
inline void sobelEdges(const std::vector<float>& in, std::vector<float>& out,
                        int width, int height, bool useAVX2) {
    static const std::vector<float> derivative = {-1.f, 0.f, 1.f};
    static const std::vector<float> smooth     = { 1.f, 2.f, 1.f};

    std::vector<float> gxH(in.size()), gx(in.size());
    std::vector<float> gyH(in.size()), gy(in.size());

    for (int y = 0; y < height; y++) {
        const float* row = &in[y * width];
        if (useAVX2) {
            convolveHorizontalAVX2(row, &gxH[y * width], width, derivative);
            convolveHorizontalAVX2(row, &gyH[y * width], width, smooth);
        } else {
            convolveHorizontalScalar(row, &gxH[y * width], width, derivative);
            convolveHorizontalScalar(row, &gyH[y * width], width, smooth);
        }
    }
    if (useAVX2) {
        convolveVerticalAVX2(gxH, gx, width, height, smooth);
        convolveVerticalAVX2(gyH, gy, width, height, derivative);
    } else {
        convolveVerticalScalar(gxH, gx, width, height, smooth);
        convolveVerticalScalar(gyH, gy, width, height, derivative);
    }

    out.resize(in.size());
    for (size_t i = 0; i < in.size(); i++)
        out[i] = std::sqrt(gx[i] * gx[i] + gy[i] * gy[i]);
}

// Histogram equalization — deliberately scalar-only. This filter's core step
// is building a cumulative histogram, which is a running total: each bucket
// depends on the sum of all buckets before it. That's a sequential
// dependency chain, not independent per-pixel work, so it doesn't parallelize
// with SIMD the way Gaussian blur and Sobel do — a real limit, not a gap we
// skipped.
inline void histogramEqualize(const std::vector<int16_t>& in, std::vector<int16_t>& out,
                               int16_t minVal, int16_t maxVal) {
    const int bins = 256;
    std::vector<int> histogram(bins, 0);
    auto bucket = [&](int16_t v) {
        double norm = double(v - minVal) / double(maxVal - minVal + 1);
        return std::clamp(static_cast<int>(norm * bins), 0, bins - 1);
    };
    for (int16_t v : in) histogram[bucket(v)]++;

    std::vector<double> cdf(bins);
    double cumulative = 0;
    for (int b = 0; b < bins; b++) {
        cumulative += histogram[b];
        cdf[b] = cumulative / in.size();
    }

    out.resize(in.size());
    for (size_t i = 0; i < in.size(); i++) {
        double eq = cdf[bucket(in[i])];
        out[i] = static_cast<int16_t>(minVal + eq * (maxVal - minVal));
    }
}
