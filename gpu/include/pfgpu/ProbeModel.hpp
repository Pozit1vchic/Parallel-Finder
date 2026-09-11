#pragma once

#include <string_view>

namespace pfgpu {

// Tiny ONNX model used to answer "is this execution provider actually usable?".
// Probing means creating a real session with the EP attached, because
// OrtApi::GetAvailableProviders explicitly does not guarantee that a listed
// provider can be initialized (missing cuDNN, no CUDA driver, ...).
//
// The model is an Identity node on a float32 [1,2] tensor — small enough that
// session creation costs milliseconds, real enough that EP initialization
// (provider DLL load, device enumeration) actually happens.
inline constexpr std::string_view kProbeModelInputName = "X";
inline constexpr std::string_view kProbeModelOutputName = "Y";
inline constexpr int kProbeModelElementCount = 2;

// Deterministic ONNX bytes, generated on first use. Built in code instead of
// committed as a binary fixture: the repository carries no model files at all
// (see models/README.md) and every build tests against byte-identical input.
std::string_view probeModelBytes();

} // namespace pfgpu
