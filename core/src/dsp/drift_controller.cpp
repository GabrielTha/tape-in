// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include "tapein/core/dsp/drift_controller.h"

#include <algorithm>

namespace tapein::core::dsp {

double DriftController::update(double fillFrames, double targetFrames, double dtSeconds) noexcept
{
    const double errorSeconds = (fillFrames - targetFrames) / m_params.sampleRate;
    const double proportional = m_params.kp * errorSeconds;
    double deviation = proportional + m_params.ki * m_integral;
    const double clamped = std::clamp(deviation, -m_params.maxDeviation, m_params.maxDeviation);
    // Anti-windup: integrate only while the output is not saturated in the
    // direction the error is pushing.
    const bool saturated = (deviation > m_params.maxDeviation && errorSeconds > 0.0)
                           || (deviation < -m_params.maxDeviation && errorSeconds < 0.0);
    if (!saturated) {
        m_integral += errorSeconds * dtSeconds;
    }
    deviation = clamped;
    m_lastMultiplier = 1.0 - deviation;
    return m_lastMultiplier;
}

void DriftController::reset() noexcept
{
    m_integral = 0.0;
    m_lastMultiplier = 1.0;
}

} // namespace tapein::core::dsp
