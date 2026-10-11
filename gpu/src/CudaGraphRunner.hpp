#pragma once
#include "pfgpu/Inference.hpp"
#include <memory>

namespace pfgpu::detail {
// Opt-in audit implementation. Owns fixed IO buffers for exactly one graph.
// All CUDA Graph runs are serialized, including capture, until stream-level
// synchronization is verified for the runtime distributed with the app.
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
