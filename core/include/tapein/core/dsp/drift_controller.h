// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#pragma once

namespace tapein::core::dsp {

struct DriftParams {
    double sampleRate = 44100.0; ///< rate of the ring the fill level is measured in
    double kp = 0.2; ///< ratio deviation per second of fill error (time constant 2/kp = 10 s)
    double ki = 0.01; ///< per second² of accumulated error (critically damped with kp²/4)
    double maxDeviation = 0.002; ///< ±0.2 %, inaudible as pitch
};

/// Slow PI loop on the ring fill level that steers the resampling ratio so
/// the buffer stays at its target: fill above target → consume input faster
/// → ratio multiplier below 1; fill below → above 1. Audio-thread safe
/// (plain arithmetic).
class DriftController {
public:
    explicit DriftController(DriftParams params = {}) noexcept : m_params(params) {}

    /// Returns the multiplier to apply to the nominal ratio, within
    /// 1 ± maxDeviation.
    double update(double fillFrames, double targetFrames, double dtSeconds) noexcept;
    void reset() noexcept;
    /// Last multiplier minus one.
    [[nodiscard]] double deviation() const noexcept { return m_lastMultiplier - 1.0; }

private:
    DriftParams m_params;
    double m_integral = 0.0; ///< seconds × seconds of error
    double m_lastMultiplier = 1.0;
};

} // namespace tapein::core::dsp
