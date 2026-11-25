// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
#pragma once
#include <complex>
#include <utility>
#include <vector>

// The FFT a sample's analysis uses on the service thread (engine/onsets.cpp, engine/root_find.cpp); never the audio
// thread's (it is handed vectors).
namespace rhythmer {

// In-place radix-2 FFT of a.size() (a power of two) points; twiddle[k] = e^(-2πik/n), k < n/2.
inline void fft(std::vector<std::complex<float>>& a, const std::vector<std::complex<float>>& twiddle) {
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const size_t half = len / 2, step = n / len;
        for (size_t i = 0; i < n; i += len)
            for (size_t k = 0; k < half; ++k) {
                std::complex<float> u = a[i + k], v = a[i + k + half] * twiddle[k * step];
                a[i + k] = u + v;
                a[i + k + half] = u - v;
            }
    }
}

} // namespace rhythmer
