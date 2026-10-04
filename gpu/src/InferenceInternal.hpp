#pragma once

#include <pfgpu/Inference.hpp>
#include <pfgpu/OrtRuntime.hpp>

namespace pfgpu::detail {
// The runtime-facing implementation uses its supplied API for every owned
// resource. Keeping it separate also permits failure-path tests without a GPU.
InferenceResult runFloatWithApi(const OrtApi& api, const SessionHandle& session,
                                const FloatTensor& input);
}
