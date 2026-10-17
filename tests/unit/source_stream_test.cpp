// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include "tapein/core/dsp/source_stream.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <memory>
#include <numbers>
#include <vector>

using namespace tapein::core::dsp;
using tapein::core::buffer::HeapRing;

namespace {

constexpr uint32_t kTarget = 8820; // 200 ms, keeps the tests fast
constexpr float kAmplitude = 0.5f;

struct Harness {
    std::unique_ptr<SourceStream> stream;
    uint32_t phase = 0; // tone phase in ring frames
    std::vector<float> left, right; // everything rendered so far, per channel

    explicit Harness(uint32_t ringFrames = 1u << 17, ResamplerQuality q = ResamplerQuality::fastest,
                     uint32_t maxBlock = 1024)
    {
        auto ring = HeapRing::create(ringFrames);
        REQUIRE(ring.ok());
        auto s = SourceStream::create(std::move(ring.value()), q, maxBlock);
        REQUIRE(s.ok());
        stream = std::move(s.value());
        stream->params().targetFillFrames.store(kTarget);
    }

    /// Writes `frames` of a continuous 1 kHz stereo tone into the ring.
    uint32_t feed(uint32_t frames)
    {
        std::vector<int16_t> pcm(static_cast<size_t>(frames) * 2);
        for (uint32_t i = 0; i < frames; ++i) {
            const auto s = static_cast<int16_t>(
                std::lround(static_cast<double>(kAmplitude) * 32767.0 * std::sin(2.0 * std::numbers::pi * 1000.0 * (phase + i) / 44100.0)));
            pcm[i * 2] = s;
            pcm[i * 2 + 1] = s;
        }
        const uint32_t n = stream->ring().write(pcm);
        phase += n;
        return n;
    }

    /// Renders one block and appends it to left/right.
    void render(uint32_t frames, double rate = 44100.0, float soloGate = 1.0f)
    {
        std::vector<float> l(frames, 0.0f);
        std::vector<float> r(frames, 0.0f);
        stream->render(l.data(), r.data(), frames, rate, soloGate);
        left.insert(left.end(), l.begin(), l.end());
        right.insert(right.end(), r.begin(), r.end());
    }

    void renderBlocks(uint32_t blocks, uint32_t frames = 512, double rate = 44100.0)
    {
        for (uint32_t i = 0; i < blocks; ++i) {
            render(frames, rate);
        }
    }

    [[nodiscard]] StreamState state() const { return static_cast<StreamState>(stream->meters().state.load()); }
};

double peakAbs(const std::vector<float>& v, size_t from = 0, size_t to = SIZE_MAX)
{
    double m = 0;
    for (size_t i = from; i < std::min(to, v.size()); ++i) {
        m = std::max(m, std::abs(static_cast<double>(v[i])));
    }
    return m;
}

double maxJump(const std::vector<float>& v, size_t from = 0)
{
    double m = 0;
    for (size_t i = from + 1; i < v.size(); ++i) {
        m = std::max(m, std::abs(static_cast<double>(v[i] - v[i - 1])));
    }
    return m;
}

bool allZero(const std::vector<float>& v, size_t from, size_t to = SIZE_MAX)
{
    for (size_t i = from; i < std::min(to, v.size()); ++i) {
        if (v[i] != 0.0f) {
            return false;
        }
    }
    return true;
}

size_t zeroCrossings(const std::vector<float>& v, size_t from)
{
    size_t n = 0;
    for (size_t i = from + 1; i < v.size(); ++i) {
        if ((v[i - 1] < 0.0f) != (v[i] < 0.0f)) {
            ++n;
        }
    }
    return n;
}

} // namespace

TEST_CASE("renders silence while the ring is below target")
{
    Harness h;
    h.feed(kTarget - 1);
    h.renderBlocks(4);
    CHECK(allZero(h.left, 0));
    CHECK(h.state() == StreamState::buffering);
    CHECK(h.stream->meters().fillFrames.load() == kTarget - 1);
}

TEST_CASE("starts when the ring reaches target and a tone comes through within 1 dB at ratio 1")
{
    Harness h;
    h.feed(kTarget + 4410);
    h.renderBlocks(8);
    CHECK(h.state() == StreamState::playing);
    const double peak = peakAbs(h.left, 1024); // after the fade-in
    CHECK(peak > static_cast<double>(kAmplitude) * std::pow(10.0, -1.0 / 20.0));
    CHECK(peak < static_cast<double>(kAmplitude) * std::pow(10.0, 1.0 / 20.0));
    CHECK(maxJump(h.left) < 0.2);
    CHECK(h.left == h.right);
}

TEST_CASE("44.1k ring into a 48k device yields the right tone frequency")
{
    Harness h;
    h.feed(kTarget + 2048);
    for (int i = 0; i < 48000 / 480; ++i) {
        h.render(480, 48000.0);
        h.feed(441); // what 480 output frames at 48 kHz consume
    }
    // One second of output after the first few blocks: ~2000 crossings per second at 1 kHz.
    const size_t from = 4800;
    const double seconds = static_cast<double>(h.left.size() - from) / 48000.0;
    const double crossingsPerSecond = static_cast<double>(zeroCrossings(h.left, from)) / seconds;
    CHECK(crossingsPerSecond > 2000.0 * 0.98);
    CHECK(crossingsPerSecond < 2000.0 * 1.02);
}

TEST_CASE("underrun fades to exact zeros within 10 ms and resumes after refill without jumps")
{
    Harness h;
    h.feed(kTarget + 2048);
    h.renderBlocks(2);
    REQUIRE(h.state() == StreamState::playing);
    // Drain: the ring has ~10 k frames; render until it has been empty for a while.
    h.renderBlocks(40);
    CHECK(h.state() == StreamState::refilling);
    CHECK(h.stream->meters().underruns.load() == 1);
    // The last 4 blocks (2048 frames) are exact silence.
    CHECK(allZero(h.left, h.left.size() - 2048));
    CHECK(maxJump(h.left) < 0.2);

    const size_t resumeFrom = h.left.size();
    const uint32_t leftover = h.stream->meters().fillFrames.load();
    h.feed(kTarget / 2 - leftover - 100); // still below half the target
    h.renderBlocks(2);
    CHECK(allZero(h.left, resumeFrom));
    h.feed(4096);
    h.renderBlocks(6);
    CHECK(h.state() == StreamState::playing);
    CHECK(peakAbs(h.left, resumeFrom) > 0.3);
    CHECK(maxJump(h.left, resumeFrom) < 0.2);
}

TEST_CASE("mute ramps to silence within one block and unmute brings the tone back")
{
    Harness h;
    h.feed(kTarget + 44100);
    h.renderBlocks(4);
    REQUIRE(peakAbs(h.left, 1024) > 0.3);
    h.stream->params().mute.store(true);
    h.render(512);
    const size_t afterMute = h.left.size();
    h.renderBlocks(2);
    CHECK(allZero(h.left, afterMute));
    CHECK(maxJump(h.left) < 0.2);
    h.stream->params().mute.store(false);
    h.renderBlocks(2);
    CHECK(peakAbs(h.left, afterMute + 1024) > 0.3);
}

TEST_CASE("sender gain applies unless ignoreSenderGain is set; user gain always applies")
{
    Harness h;
    h.feed(kTarget + 44100);
    h.stream->params().senderGain.store(0.1f);
    h.renderBlocks(6);
    const double withSender = peakAbs(h.left, 2048);
    CHECK(withSender > 0.04);
    CHECK(withSender < 0.06);

    h.stream->params().ignoreSenderGain.store(true);
    h.renderBlocks(6);
    const double ignored = peakAbs(h.left, h.left.size() - 1024);
    CHECK(ignored > 0.45);
    CHECK(ignored < 0.55);

    h.stream->params().userGain.store(0.5f);
    h.renderBlocks(6);
    const double user = peakAbs(h.left, h.left.size() - 1024);
    CHECK(user > 0.2);
    CHECK(user < 0.3);
}

TEST_CASE("solo gate of zero silences the source")
{
    Harness h;
    h.feed(kTarget + 44100);
    h.renderBlocks(2);
    for (int i = 0; i < 4; ++i) {
        h.render(512, 44100.0, 0.0f);
    }
    CHECK(allZero(h.left, h.left.size() - 1024));
}

TEST_CASE("ring above target plus one second is skipped back to target")
{
    Harness h;
    h.feed(kTarget + 44100 + 22050);
    h.renderBlocks(12);
    const uint32_t fill = h.stream->meters().fillFrames.load();
    CHECK(fill < kTarget + 2048);
    CHECK(fill > kTarget - 8192);
    CHECK(h.state() == StreamState::playing);
    CHECK(maxJump(h.left) < 0.2);
}

TEST_CASE("a discard request from the producer empties the ring and goes back to buffering")
{
    Harness h;
    h.feed(kTarget + 44100);
    h.renderBlocks(4);
    REQUIRE(h.state() == StreamState::playing);
    h.stream->ring().requestDiscardAll();
    h.renderBlocks(4);
    CHECK(h.state() == StreamState::buffering);
    CHECK(h.stream->meters().fillFrames.load() == 0);
    CHECK(maxJump(h.left) < 0.2);
}

TEST_CASE("stopRequested ends in StreamState::stopped and renders nothing afterwards")
{
    Harness h;
    h.feed(kTarget + 44100);
    h.renderBlocks(4);
    h.stream->params().stopRequested.store(true);
    h.renderBlocks(4);
    CHECK(h.state() == StreamState::stopped);
    CHECK(allZero(h.left, h.left.size() - 1024));
    CHECK(maxJump(h.left) < 0.2);
    h.feed(kTarget + 1000);
    h.renderBlocks(2);
    CHECK(allZero(h.left, h.left.size() - 1024));
}

TEST_CASE("device sample-rate switch from 44.1k to 96k mid-stream keeps the output bounded")
{
    Harness h;
    h.feed(kTarget + 2048);
    for (int i = 0; i < 8; ++i) {
        h.render(512, 44100.0);
        h.feed(512);
    }
    for (int i = 0; i < 8; ++i) {
        h.render(512, 96000.0);
        h.feed(236);
    }
    for (int i = 0; i < 8; ++i) {
        h.render(1024, 48000.0);
        h.feed(941);
    }
    CHECK(h.state() == StreamState::playing);
    CHECK(peakAbs(h.left) < 0.6);
    CHECK(peakAbs(h.left, h.left.size() - 1024) > 0.3);
}

TEST_CASE("blocks of one frame and of maxBlock frames both render")
{
    Harness h;
    h.feed(kTarget + 44100);
    for (int i = 0; i < 3000; ++i) {
        h.render(1);
    }
    h.renderBlocks(4, 1024);
    CHECK(h.state() == StreamState::playing);
    CHECK(peakAbs(h.left, 2000) > 0.3);
}

TEST_CASE("meters report peak, rms, fill and the ratio deviation")
{
    Harness h;
    h.feed(kTarget + 44100);
    h.renderBlocks(8);
    const auto& m = h.stream->meters();
    CHECK(m.peakL.load() > 0.3f);
    CHECK(m.peakR.load() > 0.3f);
    CHECK(m.rms.load() > 0.2f);
    CHECK(m.rms.load() < m.peakL.load());
    CHECK(m.fillFrames.load() > 0);
    CHECK(std::abs(m.ratioDeviation.load()) <= 0.002f);
}

TEST_CASE("medium-quality sinc resampling to a 48k device produces real audio, not silence")
{
    // Regression: the produce() loop once capped how much input the resampler
    // was offered per call, which starved the long medium/best sinc filters
    // and emitted near-silence (found by the app smoke test). Feed a steady
    // tone and require a healthy output level at every quality.
    for (auto quality : {ResamplerQuality::fastest, ResamplerQuality::medium, ResamplerQuality::best}) {
        Harness h(1u << 17, quality, SourceStream::kDefaultMaxBlock);
        h.feed(kTarget + 8192);
        for (int i = 0; i < 40; ++i) {
            h.render(512, 48000.0);
            h.feed(471); // ~what 512 output frames at 48 kHz consume from a 44.1 kHz ring
        }
        REQUIRE(h.state() == StreamState::playing);
        CHECK(peakAbs(h.left, h.left.size() - 4096) > 0.3);
    }
}
