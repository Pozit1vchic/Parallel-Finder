#pragma once
#include <array>

namespace pfcore {
// Camera-space rotation, row-major. Rejected detections remain in the sequence
// with observed=false so that missing evidence affects temporal coverage.
struct HeadOrientationSample {
    double time = 0;
    std::array<double, 9> rotation{};
    bool observed = false;
};
}
