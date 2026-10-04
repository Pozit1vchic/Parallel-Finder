#include "pfgpu/Inference.hpp"

#include <limits>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <unordered_map>
#include <array>
#include <memory>

#include "pfgpu/OrtRuntime.hpp"
#include "InferenceInternal.hpp"

namespace pfgpu {

namespace {

constexpr std::size_t kMaxTensorElements = 64ULL * 1024ULL * 1024ULL;
constexpr std::size_t kMaxOutputs = 64;
constexpr std::size_t kMaxRank = 8;

struct OwnedOutputs {
    const OrtApi& api;
    std::array<OrtValue*, kMaxOutputs> values{};
    ~OwnedOutputs() { for (auto* value : values) if (value) api.ReleaseValue(value); }
    OwnedOutputs(const OwnedOutputs&) = delete;
    OwnedOutputs& operator=(const OwnedOutputs&) = delete;
    explicit OwnedOutputs(const OrtApi& api) : api(api) {}
};

struct RunProfile {
    std::size_t calls = 0;
    double setup = 0, run = 0, output = 0;
};

bool readTensorSpec(const OrtApi& api, const OrtTypeInfo* typeInfo,
                    TensorSpec& destination, std::string& error)
{
    const OrtTensorTypeAndShapeInfo* tensorInfo = nullptr;
    if (!checkStatus(api, api.CastTypeInfoToTensorInfo(typeInfo, &tensorInfo), error)) return false;
    std::size_t rank = 0;
    if (!checkStatus(api, api.GetDimensionsCount(tensorInfo, &rank), error)) return false;
    if (rank == 0 || rank > kMaxRank) {
        error = "tensor rank is outside the supported range";
        return false;
    }
    destination.shape.resize(rank);
    return checkStatus(api, api.GetDimensions(tensorInfo, destination.shape.data(), rank), error);
}

} // namespace

SessionSpec describeSession(const SessionHandle& session)
{
    SessionSpec result;
    const OrtApi* api = ortApi();
    if (!api || !session.session) { result.error = "ONNX Runtime session is not available"; return result; }
    std::unique_ptr<OrtTypeInfo, decltype(api->ReleaseTypeInfo)> inputInfo(nullptr, api->ReleaseTypeInfo);
    if (!checkStatus(*api, api->SessionGetInputTypeInfo(session.session, 0, std::out_ptr(inputInfo)), result.error)) return result;
    const bool inputOk = readTensorSpec(*api, inputInfo.get(), result.input, result.error);
    if (!inputOk) return result;
    std::size_t outputCount = 0;
    if (!checkStatus(*api, api->SessionGetOutputCount(session.session, &outputCount), result.error)) return result;
    if (outputCount == 0 || outputCount > kMaxOutputs) {
        result.error = "model output count is outside the supported range";
        return result;
    }
    result.outputs.resize(outputCount);
    for (std::size_t i = 0; i < outputCount; ++i) {
        std::unique_ptr<OrtTypeInfo, decltype(api->ReleaseTypeInfo)> outputInfo(nullptr, api->ReleaseTypeInfo);
        if (!checkStatus(*api, api->SessionGetOutputTypeInfo(session.session, i, std::out_ptr(outputInfo)), result.error)) return result;
        const bool outputOk = readTensorSpec(*api, outputInfo.get(), result.outputs[i], result.error);
        if (!outputOk) return result;
    }
    result.ok = true;
    return result;
}

InferenceResult runFloat(const SessionHandle& session, const FloatTensor& input)
{
    const OrtApi* api = ortApi();
    if (!api) {
        InferenceResult result;
        result.error = "ONNX Runtime session is not available";
        return result;
    }
    return detail::runFloatWithApi(*api, session, input);
}

InferenceResult detail::runFloatWithApi(const OrtApi& runtimeApi, const SessionHandle& session,
                                       const FloatTensor& input)
{
    const auto* profileFlag = std::getenv("PF_DEBUG_INFERENCE");
    const bool profile = profileFlag && *profileFlag;
    const auto begin = profile ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    const auto elapsed = [&] {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
    };
    InferenceResult result;
    const OrtApi* api = &runtimeApi;
    if (!session.session) { result.error = "ONNX Runtime session is not available"; return result; }
    if (input.shape.empty() || input.values.empty()) { result.error = "inference input tensor is empty"; return result; }
    if (input.shape.size()>kMaxRank) { result.error = "inference input rank is outside the supported range"; return result; }
    std::size_t elements = 1;
    for (const auto dimension : input.shape) {
        if (dimension <= 0 || static_cast<std::uint64_t>(dimension) > std::numeric_limits<std::size_t>::max() / elements) {
            result.error = "inference input shape is invalid"; return result;
        }
        elements *= static_cast<std::size_t>(dimension);
        if (elements > kMaxTensorElements) {
            result.error = "inference input tensor is too large";
            return result;
        }
    }
    if (elements != input.values.size()) { result.error = "input shape does not match value count"; return result; }

    OrtAllocator* allocator = nullptr;
    if (!checkStatus(*api, api->GetAllocatorWithDefaultOptions(&allocator), result.error)) return result;
    const auto freeAllocated = [api, allocator](char* pointer) noexcept {
        if (pointer) {
            if (OrtStatus* status = api->AllocatorFree(allocator, pointer)) {
                api->ReleaseStatus(status);
            }
        }
    };
    std::unique_ptr<char, decltype(freeAllocated)> inputName(nullptr, freeAllocated);
    if (!checkStatus(*api, api->SessionGetInputName(session.session, 0, allocator, std::out_ptr(inputName)), result.error)) return result;
    if (!inputName) { result.error = "ONNX Runtime returned a null input name"; return result; }
    std::unique_ptr<OrtMemoryInfo, decltype(api->ReleaseMemoryInfo)> memory(nullptr, api->ReleaseMemoryInfo);
    if (!checkStatus(*api, api->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, std::out_ptr(memory)), result.error)) return result;
    std::unique_ptr<OrtValue, decltype(api->ReleaseValue)> inputValue(nullptr, api->ReleaseValue);
    if (!checkStatus(*api, api->CreateTensorWithDataAsOrtValue(memory.get(), const_cast<float*>(input.values.data()),
        input.values.size() * sizeof(float), input.shape.data(), input.shape.size(), ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, std::out_ptr(inputValue)), result.error)) return result;
    const char* inputNames[] = {inputName.get()};
    std::size_t outputCount = 0;
    if (!checkStatus(*api, api->SessionGetOutputCount(session.session, &outputCount), result.error)) return result;
    if (outputCount == 0 || outputCount > kMaxOutputs) {
        result.error = "model output count is outside the supported range";
        return result;
    }
    std::array<const char*, kMaxOutputs> outputNames{};
    std::vector<std::unique_ptr<char, decltype(freeAllocated)>> ownedNames;
    ownedNames.reserve(outputCount);
    for (std::size_t i = 0; i < outputCount; ++i) {
        ownedNames.emplace_back(nullptr, freeAllocated);
        if (!checkStatus(*api, api->SessionGetOutputName(session.session, i, allocator, std::out_ptr(ownedNames.back())), result.error)) return result;
        if (!ownedNames.back()) { result.error = "ONNX Runtime returned a null output name"; return result; }
        outputNames[i] = ownedNames.back().get();
    }
    // The DirectML EP forbids concurrent Run calls on one session. Keep the
    // lock through output copies/releases, so another call cannot reuse its
    // buffers before this caller has consumed them. CPU/CUDA remain parallel.
    std::unique_lock<std::mutex> runLock;
    if (session.runMutex) runLock = std::unique_lock(*session.runMutex);
    OwnedOutputs outputs(*api);
    const OrtValue* inputValues[] = {inputValue.get()};
    const double setupMs = profile ? elapsed() : 0;
    OrtStatus* runStatus = api->Run(session.session, nullptr, inputNames, inputValues, 1,
                                   outputNames.data(), outputCount, outputs.values.data());
    const double runMs = profile ? elapsed() - setupMs : 0;
    if (!checkStatus(*api, runStatus, result.error)) return result;
    result.outputNames.reserve(outputCount);
    result.outputs.reserve(outputCount);
    for (std::size_t i = 0; i < outputCount; ++i) result.outputNames.emplace_back(outputNames[i]);
    for (std::size_t i = 0; i < outputCount; ++i) {
        std::unique_ptr<OrtValue, decltype(api->ReleaseValue)> value(outputs.values[i], api->ReleaseValue);
        outputs.values[i] = nullptr;
        if (!value) { result.error = "ONNX Runtime returned a null output"; return result; }
        std::unique_ptr<OrtTensorTypeAndShapeInfo, decltype(api->ReleaseTensorTypeAndShapeInfo)> shapeInfo(nullptr, api->ReleaseTensorTypeAndShapeInfo);
        if (!checkStatus(*api, api->GetTensorTypeAndShape(value.get(), std::out_ptr(shapeInfo)), result.error)) return result;
        ONNXTensorElementDataType type;
        if (!checkStatus(*api, api->GetTensorElementType(shapeInfo.get(), &type), result.error)) return result;
        if (type != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) { result.error = "model output is not float32"; return result; }
        std::size_t rank = 0;
        if (!checkStatus(*api, api->GetDimensionsCount(shapeInfo.get(), &rank), result.error)) return result;
        if (rank > kMaxRank) { result.error = "model output rank is outside the supported range"; return result; }
        FloatTensor tensor;
        tensor.shape.resize(rank);
        if (rank && !checkStatus(*api, api->GetDimensions(shapeInfo.get(), tensor.shape.data(), rank), result.error)) return result;
        std::size_t count = 0;
        if (!checkStatus(*api, api->GetTensorShapeElementCount(shapeInfo.get(), &count), result.error)) return result;
        if (count > kMaxTensorElements) {
            result.error = "model output tensor is too large";
            return result;
        }
        // GetTensorMutableData returns a pointer owned by ORT.  Passing a
        // pointer to our vector here does not make ORT write into that vector;
        // the API overwrites the pointer argument.  Copy the returned buffer
        // before releasing the OrtValue, otherwise every inference would
        // silently return a zero-filled tensor.
        void* data = nullptr;
        if (!checkStatus(*api, api->GetTensorMutableData(value.get(), &data), result.error)) return result;
        if (count > 0 && !data) {
            result.error = "ONNX Runtime returned a null tensor buffer";
            return result;
        }
        const auto* source = static_cast<const float*>(data);
        // An empty tensor may legally expose a null data pointer. Avoid even
        // zero-offset pointer arithmetic on that pointer.
        if (count) tensor.values.assign(source, source + count);
        result.outputs.push_back(std::move(tensor));
    }
    result.ok = result.error.empty() && !result.outputs.empty();
    if (session.profiling) SessionCache::recordProfilingRun(session);
    if (profile) {
        static thread_local std::unordered_map<std::string, RunProfile> profiles;
        auto& totals = profiles[session.cacheKey];
        ++totals.calls;
        totals.setup += setupMs; totals.run += runMs;
        totals.output += elapsed() - setupMs - runMs;
        if (totals.calls == 1 || totals.calls % 128 == 0)
            std::fprintf(stderr, "PF_DEBUG_INFERENCE key=%s calls=%zu setup_ms=%.3f run_ms=%.3f output_ms=%.3f\n",
                session.cacheKey.c_str(), totals.calls, totals.setup, totals.run, totals.output);
    }
    return result;
}

} // namespace pfgpu
