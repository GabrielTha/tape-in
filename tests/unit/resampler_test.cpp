// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include <samplerate.h>

#include <catch2/catch_test_macros.hpp>

#include <string>

TEST_CASE("libsamplerate is linked")
{
    REQUIRE(std::string(src_get_version()).find("libsamplerate") != std::string::npos);
}

#include "tapein/core/dsp/resampler.h"

#include <cmath>
#include <numbers>
#include <vector>

using namespace tapein::core::dsp;

namespace {

std::vector<float> stereoSine(double freqHz, double rate, uint32_t frames, float amplitude = 0.5f)
{
    std::vector<float> v(static_cast<size_t>(frames) * 2);
    for (uint32_t i = 0; i < frames; ++i) {
        const float s = amplitude * static_cast<float>(std::sin(2.0 * std::numbers::pi * freqHz * i / rate));
        v[i * 2] = s;
        v[i * 2 + 1] = s;
    }
    return v;
}

struct RunResult {
    std::vector<float> out; // interleaved
    uint32_t consumed = 0;
};

/// Pushes the whole input through in blocks of inBlock frames, collecting output.
RunResult run(Resampler& rs, const std::vector<float>& in, double ratio, uint32_t inBlock = 352, double ratioStep = 0.0)
{
    RunResult r;
    std::vector<float> outBlock(static_cast<size_t>(inBlock) * 8);
    const uint32_t total = static_cast<uint32_t>(in.size() / 2);
    while (r.consumed < total) {
        const uint32_t n = std::min(inBlock, total - r.consumed);
        const auto p = rs.process(std::span<const float>(in).subspan(static_cast<size_t>(r.consumed) * 2, static_cast<size_t>(n) * 2),
                                  outBlock, ratio);
        REQUIRE(p.framesConsumed <= n);
        r.out.insert(r.out.end(), outBlock.begin(), outBlock.begin() + static_cast<long>(p.framesProduced) * 2);
        r.consumed += p.framesConsumed;
        ratio += ratioStep;
        if (p.framesConsumed == 0 && p.framesProduced == 0) {
            FAIL("resampler made no progress");
        }
    }
    return r;
}

double peakAbs(const std::vector<float>& v, size_t from)
{
    double m = 0;
    for (size_t i = from; i < v.size(); ++i) {
        m = std::max(m, std::abs(static_cast<double>(v[i])));
    }
    return m;
}

double maxJump(const std::vector<float>& v, size_t from)
{
    double m = 0;
    for (size_t i = from + 2; i < v.size(); i += 2) {
        m = std::max(m, std::abs(static_cast<double>(v[i] - v[i - 2])));
    }
    return m;
}

} // namespace

TEST_CASE("44.1k to 48k produces about 48/44.1 output frames per input frame over one second")
{
    auto rs = Resampler::create(2, ResamplerQuality::medium);
    REQUIRE(rs.ok());
    const auto in = stereoSine(1000.0, 44100.0, 44100);
    const auto r = run(rs.value(), in, 48000.0 / 44100.0);
    const auto outFrames = static_cast<double>(r.out.size() / 2);
    CHECK(outFrames > 48000.0 - 200.0);
    CHECK(outFrames < 48000.0 + 200.0);
}

TEST_CASE("a 1 kHz sine survives 44.1k to 96k within 0.5 dB and without jumps")
{
    auto rs = Resampler::create(2, ResamplerQuality::medium);
    REQUIRE(rs.ok());
    const auto in = stereoSine(1000.0, 44100.0, 44100);
    const auto r = run(rs.value(), in, 96000.0 / 44100.0);
    const size_t settle = 2000 * 2; // skip the filter's start-up
    const double peak = peakAbs(r.out, settle);
    CHECK(peak > 0.5 * std::pow(10.0, -0.5 / 20.0));
    CHECK(peak < 0.5 * std::pow(10.0, 0.5 / 20.0));
    // A 1 kHz tone at 96 kHz moves at most 2*pi*1000/96000*0.5 ≈ 0.033 per sample.
    CHECK(maxJump(r.out, settle) < 0.05);
}

TEST_CASE("ratio changes of 0.1 percent between blocks produce no discontinuity")
{
    auto rs = Resampler::create(2, ResamplerQuality::medium);
    REQUIRE(rs.ok());
    const auto in = stereoSine(1000.0, 44100.0, 44100 * 2);
    const auto r = run(rs.value(), in, 48000.0 / 44100.0, 352, 0.001 * 48000.0 / 44100.0 / 10.0);
    // At most 2*pi*1000/48000*0.5 ≈ 0.065 per sample for a clean tone.
    CHECK(maxJump(r.out, 2000 * 2) < 0.08);
}

TEST_CASE("setRatio jumps immediately and process with an empty output consumes nothing")
{
    auto rs = Resampler::create(2, ResamplerQuality::fastest);
    REQUIRE(rs.ok());
    const auto in = stereoSine(1000.0, 44100.0, 352);
    std::vector<float> none;
    const auto p = rs.value().process(in, none, 1.0);
    CHECK(p.framesConsumed == 0);
    CHECK(p.framesProduced == 0);
    rs.value().setRatio(2.0);
    std::vector<float> out(352 * 2 * 4);
    const auto q = rs.value().process(in, out, 2.0);
    CHECK(q.framesConsumed > 0);
    CHECK(q.framesProduced > 0);
    CHECK(rs.value().channels() == 2);
    rs.value().reset();
}
