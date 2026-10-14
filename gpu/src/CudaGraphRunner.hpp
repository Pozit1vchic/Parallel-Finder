#pragma once
#include "pfgpu/Inference.hpp"
#include <memory>

namespace pfgpu::detail {
// Fixed IO buffers for one graph. Capture and replay share a dedicated worker.
// Initial real-input float32 parity is required; unsupported capture or changed
// tensor shapes use ordinary CUDA execution with the same session and weights.
class CudaGraphRunner {
public:
    CudaGraphRunner(const OrtApi& api, OrtSession* session, int deviceId);
    ~CudaGraphRunner();
    InferenceResult run(const FloatTensor& input);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
