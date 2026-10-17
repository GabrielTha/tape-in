// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors

#include "tapein/core/dsp/drift_controller.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace tapein::core::dsp;

TEST_CASE("fill above target lowers the ratio, below raises it")
{
    DriftController c;
    CHECK(c.update(66150 + 4410, 66150, 0.01) < 1.0);
    c.reset();
    CHECK(c.update(66150 - 4410, 66150, 0.01) > 1.0);
    c.reset();
    CHECK(c.update(66150, 66150, 0.01) == 1.0);
}

TEST_CASE("output is clamped to plus or minus maxDeviation")
{
    DriftParams p;
    p.maxDeviation = 0.002;
    DriftController c(p);
    CHECK(c.update(66150 + 441000, 66150, 0.01) >= 1.0 - 0.002 - 1e-12);
    CHECK(c.update(66150 + 441000, 66150, 0.01) <= 1.0 - 0.002 + 1e-12);
    c.reset();
    CHECK(c.update(0, 66150, 0.01) <= 1.0 + 0.002 + 1e-12);
    CHECK(c.update(0, 66150, 0.01) >= 1.0 + 0.002 - 1e-12);
    CHECK(std::abs(c.deviation() - 0.002) < 1e-9);
}

TEST_CASE("a simulated ring with 50 ppm sender drift settles within 100 frames of target in 120 s")
{
    // Plant: the sender writes 44100 * (1 + drift) frames per second, the
    // consumer eats 44100 / multiplier per second of input (multiplier is
    // output-over-input, so a multiplier below 1 consumes input faster).
    DriftController c;
    const double target = 66150;
    double fill = target + 2000; // start 45 ms high
    const double drift = 50e-6;
    const double dt = 0.01;
    double worstLate = 0;
    for (int step = 0; step < 12000; ++step) {
        const double m = c.update(fill, target, dt);
        fill += dt * 44100.0 * (1.0 + drift) - dt * 44100.0 / m;
        if (step > 10000) {
            worstLate = std::max(worstLate, std::abs(fill - target));
        }
    }
    CHECK(worstLate < 100.0);
    CHECK(std::abs(c.deviation() + drift) < 20e-6); // steady state cancels the drift
}

TEST_CASE("reset clears the integrator")
{
    DriftController c;
    for (int i = 0; i < 1000; ++i) {
        c.update(66150 + 4410, 66150, 0.01);
    }
    c.reset();
    CHECK(c.deviation() == 0.0);
    CHECK(c.update(66150, 66150, 0.01) == 1.0);
}
