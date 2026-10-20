// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include "tapein/core/dsp/mixer.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <memory>
#include <numbers>
#include <vector>

using namespace tapein::core::dsp;
using tapein::core::buffer::HeapRing;

namespace {

constexpr uint32_t kTarget = 2048;

/// A source whose ring holds `seconds` of a stereo tone, ready to play
/// (below target + 1 s, so the resync rule does not discard it).
std::unique_ptr<SourceStream> toneSource(float amplitude, double freq = 1000.0, double seconds = 0.5)
{
    auto ring = HeapRing::create(1u << 17);
    REQUIRE(ring.ok());
    auto s = SourceStream::create(std::move(ring.value()), ResamplerQuality::fastest, 256);
    REQUIRE(s.ok());
    auto stream = std::move(s.value());
    stream->params().targetFillFrames.store(kTarget);
    const auto frames = static_cast<uint32_t>(seconds * 44100.0);
    std::vector<int16_t> pcm(static_cast<size_t>(frames) * 2);
    for (uint32_t i = 0; i < frames; ++i) {
        const auto v = static_cast<int16_t>(std::lround(static_cast<double>(amplitude) * 32767.0
                                                        * std::sin(2.0 * std::numbers::pi * freq * i / 44100.0)));
        pcm[i * 2] = v;
        pcm[i * 2 + 1] = v;
    }
    REQUIRE(stream->ring().write(pcm) == frames);
    return stream;
}

struct Out {
    std::vector<std::vector<float>> ch;
    std::vector<float*> ptrs;
    explicit Out(uint32_t channels, uint32_t frames) : ch(channels, std::vector<float>(frames, 7.0f))
    {
        for (auto& c : ch) {
            ptrs.push_back(c.data());
        }
    }
};

double peakAbs(const std::vector<float>& v, size_t from = 0)
{
    double m = 0;
    for (size_t i = from; i < v.size(); ++i) {
        m = std::max(m, std::abs(static_cast<double>(v[i])));
    }
    return m;
}

void settle(Mixer& m, uint32_t blocks = 8, uint32_t frames = 512, uint32_t channels = 2)
{
    for (uint32_t i = 0; i < blocks; ++i) {
        Out o(channels, frames);
        m.render(o.ptrs.data(), channels, frames, 44100.0);
    }
}

} // namespace

TEST_CASE("two sources sum")
{
    Mixer m(4, 256);
    REQUIRE(m.add(toneSource(0.25f)).has_value());
    REQUIRE(m.add(toneSource(0.25f)).has_value());
    settle(m);
    Out o(2, 512);
    m.render(o.ptrs.data(), 2, 512, 44100.0);
    CHECK(peakAbs(o.ch[0]) > 0.45);
    CHECK(peakAbs(o.ch[0]) < 0.55);
    CHECK(o.ch[0] == o.ch[1]);
    CHECK(m.meters().callbacks.load() == 9);
    CHECK(m.meters().peakL.load() > 0.45f);
}

TEST_CASE("solo gates the others")
{
    Mixer m(4, 256);
    const auto a = m.add(toneSource(0.25f));
    const auto b = m.add(toneSource(0.25f));
    REQUIRE((a && b));
    settle(m);
    m.source(*a)->params().solo.store(true);
    settle(m, 4);
    Out o(2, 512);
    m.render(o.ptrs.data(), 2, 512, 44100.0);
    CHECK(peakAbs(o.ch[0]) > 0.2);
    CHECK(peakAbs(o.ch[0]) < 0.3);
    CHECK(m.source(*b)->meters().peakL.load() == 0.0f);
    m.source(*a)->params().solo.store(false);
    settle(m, 4);
    m.render(o.ptrs.data(), 2, 512, 44100.0);
    CHECK(peakAbs(o.ch[0]) > 0.45);
}

TEST_CASE("mono output receives the average and extra channels stay silent")
{
    Mixer m(4, 256);
    REQUIRE(m.add(toneSource(0.5f)).has_value());
    settle(m, 8, 512, 1);
    Out mono(1, 512);
    m.render(mono.ptrs.data(), 1, 512, 44100.0);
    CHECK(peakAbs(mono.ch[0]) > 0.45);
    CHECK(peakAbs(mono.ch[0]) < 0.55);
    Out wide(8, 512);
    m.render(wide.ptrs.data(), 8, 512, 44100.0);
    CHECK(peakAbs(wide.ch[0]) > 0.45);
    CHECK(peakAbs(wide.ch[1]) > 0.45);
    for (size_t c = 2; c < 8; ++c) {
        CHECK(peakAbs(wide.ch[c]) == 0.0);
    }
    Out none(0, 512);
    m.render(none.ptrs.data(), 0, 512, 44100.0); // must not crash
}

TEST_CASE("blocks larger than maxBlockFrames and a one-frame block are handled")
{
    Mixer m(4, 256);
    REQUIRE(m.add(toneSource(0.5f)).has_value());
    Out big(2, 3000);
    m.render(big.ptrs.data(), 2, 3000, 44100.0);
    CHECK(peakAbs(big.ch[0], 1000) > 0.45);
    for (int i = 0; i < 100; ++i) {
        Out one(2, 1);
        m.render(one.ptrs.data(), 2, 1, 44100.0);
        CHECK(std::abs(one.ch[0][0]) <= 1.0f);
    }
    Out zero(2, 0);
    m.render(zero.ptrs.data(), 2, 0, 44100.0);
}

TEST_CASE("remove then collectGarbage keeps the source until a render has passed; frees at once when audio is inactive")
{
    Mixer m(4, 256);
    m.setAudioActive(true);
    const auto a = m.add(toneSource(0.5f));
    REQUIRE(a.has_value());
    settle(m, 2);
    m.remove(*a);
    CHECK(m.source(*a) == nullptr);
    m.collectGarbage();
    CHECK(m.pendingGarbage() == 1); // the audio thread may still be inside render()
    settle(m, 1);
    m.collectGarbage();
    CHECK(m.pendingGarbage() == 0);

    const auto b = m.add(toneSource(0.5f));
    REQUIRE(b.has_value());
    CHECK(*b == *a); // slot reused
    m.remove(*b);
    m.setAudioActive(false);
    m.collectGarbage();
    CHECK(m.pendingGarbage() == 0);
}

TEST_CASE("a full-scale sum is soft-clipped to plus or minus one")
{
    Mixer m(4, 256);
    REQUIRE(m.add(toneSource(0.9f)).has_value());
    REQUIRE(m.add(toneSource(0.9f)).has_value());
    settle(m);
    Out o(2, 512);
    m.render(o.ptrs.data(), 2, 512, 44100.0);
    CHECK(peakAbs(o.ch[0]) <= 1.0);
    CHECK(peakAbs(o.ch[0]) > 0.9);
}

TEST_CASE("add beyond maxSources returns nullopt")
{
    Mixer m(2, 256);
    CHECK(m.maxSources() == 2);
    REQUIRE(m.add(toneSource(0.1f)).has_value());
    REQUIRE(m.add(toneSource(0.1f)).has_value());
    CHECK_FALSE(m.add(toneSource(0.1f)).has_value());
}

TEST_CASE("master peaks accumulate until consumed")
{
    Mixer m(4, 256);
    const auto slot = m.add(toneSource(0.5f));
    REQUIRE(slot.has_value());
    settle(m);
    Out loud(2, 512);
    m.render(loud.ptrs.data(), 2, 512, 44100.0);

    // A silent render after the source is gone must not erase the held peak.
    m.remove(*slot);
    Out quiet(2, 512);
    m.render(quiet.ptrs.data(), 2, 512, 44100.0);

    const auto [l, r] = m.consumePeaks();
    CHECK(l > 0.4f);
    CHECK(r > 0.4f);

    // Consuming resets; only silence has rendered since.
    m.render(quiet.ptrs.data(), 2, 512, 44100.0);
    const auto [l2, r2] = m.consumePeaks();
    CHECK(l2 < 0.01f);
    CHECK(r2 < 0.01f);
}
