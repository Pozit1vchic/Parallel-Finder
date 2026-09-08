#include "pfcore/SceneDetector.hpp"

#include <stdexcept>

namespace pfcore {

SceneDetector::SceneDetector(double threshold)
    : threshold_(threshold)
{
    if (!(threshold_ > 0.0)) {
        throw std::invalid_argument("SceneDetector: threshold must be > 0");
    }
}

void SceneDetector::setThreshold(double threshold)
{
    if (!(threshold > 0.0)) {
        throw std::invalid_argument("SceneDetector: threshold must be > 0");
    }
    threshold_ = threshold;
}

std::vector<SceneBoundary> SceneDetector::detect() const
{
    // TODO(2b): frame-pair scoring over decoded frames (HSV histogram diff,
    // adaptive threshold) with a calibrated default on video fixtures.
    return {};
}

} // namespace pfcore
