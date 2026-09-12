#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "pfgpu/SessionCache.hpp"

namespace pfgpu {

struct FloatTensor {
    std::vector<std::int64_t> shape;
    std::vector<float> values;
};

struct InferenceResult {
    bool ok = false;
    std::string error;
    std::vector<FloatTensor> outputs;
};

struct TensorSpec {
    std::vector<std::int64_t> shape;
};

struct SessionSpec {
    bool ok = false;
    std::string error;
    TensorSpec input;
    std::vector<TensorSpec> outputs;
};

// Reads static/dynamic tensor shapes without running the graph. This is used
// to reject a profile/model mismatch before expensive video processing starts.
SessionSpec describeSession(const SessionHandle& session);

// Runs a float32 model without exposing ORT handles to pfcore. Input name is
// read from the model, so exported pose models are not coupled to a hardcoded
// graph name. The current implementation intentionally requires float output;
// model-specific decoding (YOLO/NMS variants) belongs in PoseEstimator.
InferenceResult runFloat(const SessionHandle& session, const FloatTensor& input);

} // namespace pfgpu
