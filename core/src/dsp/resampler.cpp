// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include "tapein/core/dsp/resampler.h"

#include <samplerate.h>

#include <string>
#include <utility>

namespace tapein::core::dsp {

namespace {

int converterFor(ResamplerQuality q) noexcept
{
    switch (q) {
    case ResamplerQuality::fastest:
        return SRC_SINC_FASTEST;
    case ResamplerQuality::best:
        return SRC_SINC_BEST_QUALITY;
    case ResamplerQuality::medium:
    default:
        return SRC_SINC_MEDIUM_QUALITY;
    }
}

SRC_STATE* state(void* p) noexcept
{
    return static_cast<SRC_STATE*>(p);
}

} // namespace

Result<Resampler> Resampler::create(uint32_t channels, ResamplerQuality quality)
{
    if (channels == 0 || channels > 8) {
        return makeError(ErrorCode::invalidArgument, "resampler channel count out of range");
    }
    int error = 0;
    SRC_STATE* s = src_new(converterFor(quality), static_cast<int>(channels), &error);
    if (s == nullptr) {
        return makeError(ErrorCode::unsupported, std::string("src_new: ") + src_strerror(error));
    }
    return Resampler(s, channels);
}

Resampler::Resampler(Resampler&& other) noexcept
    : m_state(std::exchange(other.m_state, nullptr)), m_channels(other.m_channels)
{
}

Resampler& Resampler::operator=(Resampler&& other) noexcept
{
    if (this != &other) {
        if (m_state != nullptr) {
            src_delete(state(m_state));
        }
        m_state = std::exchange(other.m_state, nullptr);
        m_channels = other.m_channels;
    }
    return *this;
}

Resampler::~Resampler()
{
    if (m_state != nullptr) {
        src_delete(state(m_state));
    }
}

void Resampler::reset() noexcept
{
    if (m_state != nullptr) {
        src_reset(state(m_state));
    }
}

void Resampler::setRatio(double outOverIn) noexcept
{
    if (m_state != nullptr) {
        src_set_ratio(state(m_state), outOverIn);
    }
}

Resampler::Progress Resampler::process(std::span<const float> in, std::span<float> out, double outOverIn) noexcept
{
    Progress p;
    if (m_state == nullptr || out.size() < m_channels) {
        return p;
    }
    SRC_DATA data{};
    data.data_in = in.data();
    data.data_out = out.data();
    data.input_frames = static_cast<long>(in.size() / m_channels);
    data.output_frames = static_cast<long>(out.size() / m_channels);
    data.end_of_input = 0;
    data.src_ratio = outOverIn;
    if (src_process(state(m_state), &data) != 0) {
        return p;
    }
    p.framesConsumed = static_cast<uint32_t>(data.input_frames_used);
    p.framesProduced = static_cast<uint32_t>(data.output_frames_gen);
    return p;
}

} // namespace tapein::core::dsp
