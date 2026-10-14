#include "pfgpu/Inference.hpp"

#include <limits>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <unordered_map>
#include <memory>
#include <mutex>
#include "InferenceInternal.hpp"
#include "CudaGraphRunner.hpp"

#include "pfgpu/OrtRuntime.hpp"

namespace pfgpu {

namespace {

constexpr std::size_t kMaxTensorElements = 64ULL * 1024ULL * 1024ULL;

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
    if (rank == 0 || rank > 8) {
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
    const auto releaseType = [api](OrtTypeInfo* info) { if (info) api->ReleaseTypeInfo(info); };
    std::size_t inputCount=0;
    if(!checkStatus(*api,api->SessionGetInputCount(session.session,&inputCount),result.error))return result;
    if(inputCount==0 || inputCount>8) {result.error="model input count is outside the supported range";return result;}
    result.inputs.resize(inputCount);
    for(std::size_t i=0;i<inputCount;++i) {
        OrtTypeInfo* inputInfo=nullptr;
        auto* inputStatus=api->SessionGetInputTypeInfo(session.session,i,&inputInfo);
        std::unique_ptr<OrtTypeInfo,decltype(releaseType)> inputOwner(inputInfo,releaseType);
        if(!checkStatus(*api,inputStatus,result.error)
            || !readTensorSpec(*api,inputInfo,result.inputs[i],result.error))return result;
    }
    result.input=result.inputs.front();
    std::size_t outputCount = 0;
    if (!checkStatus(*api, api->SessionGetOutputCount(session.session, &outputCount), result.error)) return result;
    if (outputCount == 0 || outputCount > 64) {
        result.error = "model output count is outside the supported range";
        return result;
    }
    result.outputs.resize(outputCount);
    for (std::size_t i = 0; i < outputCount; ++i) {
        OrtTypeInfo* outputInfo = nullptr;
        auto* outputStatus = api->SessionGetOutputTypeInfo(session.session, i, &outputInfo);
        std::unique_ptr<OrtTypeInfo, decltype(releaseType)> outputOwner(outputInfo, releaseType);
        if (!checkStatus(*api, outputStatus, result.error)) return result;
        const bool outputOk = readTensorSpec(*api, outputInfo, result.outputs[i], result.error);
        if (!outputOk) return result;
    }
    result.ok = true;
    return result;
}

InferenceResult detail::runFloatWithApi(const OrtApi& runtimeApi, const SessionHandle& session, const FloatTensor& input)
{
    return runFloatsWithApi(runtimeApi,session,std::span<const FloatTensor>(&input,1));
}

InferenceResult detail::runFloatsWithApi(const OrtApi& runtimeApi, const SessionHandle& session,
                                       std::span<const FloatTensor> inputs)
{
    const auto* profileFlag = std::getenv("PF_DEBUG_INFERENCE");
    const bool profile = profileFlag && *profileFlag;
    const auto begin = profile ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    const auto elapsed = [&] {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
    };
    InferenceResult result;
    const OrtApi* api = &runtimeApi;
    if (!api || !session.session) { result.error = "ONNX Runtime session is not available"; return result; }
    if(inputs.empty() || inputs.size()>8) {result.error="inference input count is outside the supported range";return result;}
    std::size_t totalElements=0;
    for (const auto& input : inputs) {
        if (input.shape.empty() || input.values.empty()) { result.error = "inference input tensor is empty"; return result; }
        if (input.shape.size() > 8) { result.error = "inference input rank is outside the supported range"; return result; }
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
        if (elements > kMaxTensorElements - totalElements) { result.error = "inference input tensors are too large"; return result; }
        totalElements += elements;
    }
    std::size_t modelInputs=0;
    if(!checkStatus(*api,api->SessionGetInputCount(session.session,&modelInputs),result.error))return result;
    if(modelInputs!=inputs.size()) {result.error="input count does not match model";return result;}

    struct Resources {
        explicit Resources(const OrtApi* runtime) : api(runtime) {}
        const OrtApi* api;
        OrtAllocator* allocator = nullptr;
        std::vector<char*> inputNames;
        OrtMemoryInfo* memory = nullptr;
        std::vector<OrtValue*> inputValues;
        std::vector<char*> names;
        std::vector<OrtValue*> outputs;
        ~Resources() {
            for (auto* value : outputs) if (value) api->ReleaseValue(value);
            for(auto* value:inputValues)if(value)api->ReleaseValue(value);
            if (memory) api->ReleaseMemoryInfo(memory);
            for (auto* name : names) freeName(name);
            for(auto* name:inputNames)freeName(name);
        }
        void freeName(void* name) {
            if (name && allocator) if (auto* status = api->AllocatorFree(allocator, name)) api->ReleaseStatus(status);
        }
    } resources{api};
    if (!checkStatus(*api, api->GetAllocatorWithDefaultOptions(&resources.allocator), result.error)) return result;
    resources.inputNames.resize(inputs.size());resources.inputValues.resize(inputs.size());
    if (!checkStatus(*api, api->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &resources.memory), result.error)) return result;
    for (std::size_t i = 0; i < inputs.size(); ++i) {
        if (!checkStatus(*api, api->SessionGetInputName(session.session, i, resources.allocator, &resources.inputNames[i]), result.error)) return result;
        const auto& input = inputs[i];
        if (!checkStatus(*api, api->CreateTensorWithDataAsOrtValue(resources.memory, const_cast<float*>(input.values.data()),
            input.values.size() * sizeof(float), input.shape.data(), input.shape.size(), ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &resources.inputValues[i]), result.error)) return result;
    }
    std::size_t outputCount = 0;
    if (!checkStatus(*api, api->SessionGetOutputCount(session.session, &outputCount), result.error)) return result;
    if (outputCount == 0 || outputCount > 64) { result.error = "model output count is outside the supported range"; return result; }
    resources.names.resize(outputCount);
    resources.outputs.resize(outputCount);
    std::vector<const char*> outputNames(outputCount);
    for (std::size_t i = 0; i < outputCount; ++i) {
        if (!checkStatus(*api, api->SessionGetOutputName(session.session, i, resources.allocator, &resources.names[i]), result.error)) return result;
        outputNames[i] = resources.names[i];
    }
    const std::vector<const char*> inputNames(resources.inputNames.begin(),resources.inputNames.end());
    const std::vector<const OrtValue*> inputValues(resources.inputValues.begin(),resources.inputValues.end());
    const double setupMs = profile ? elapsed() : 0;
    // DirectML sessions share mutable execution resources; serialize only this provider.
    std::unique_lock<std::mutex> runLock;
    if (session.provider == Provider::Dml && session.runMutex) runLock = std::unique_lock(*session.runMutex);
    const bool ran = checkStatus(*api, api->Run(session.session, nullptr, inputNames.data(), inputValues.data(), inputs.size(),
        outputNames.data(), outputNames.size(), resources.outputs.data()), result.error);
    if (runLock.owns_lock()) runLock.unlock();
    const double runMs = profile ? elapsed() - setupMs : 0;
    if (!ran) return result;
    for (const char* name : outputNames) result.outputNames.emplace_back(name);
    for (OrtValue*& value : resources.outputs) {
        if (!value) { result.error = "ONNX Runtime returned a null output"; return result; }
        OrtTensorTypeAndShapeInfo* shapeInfo = nullptr;
        const auto releaseShape = [api](OrtTensorTypeAndShapeInfo* shape) { if (shape) api->ReleaseTensorTypeAndShapeInfo(shape); };
        const bool shapeOk = checkStatus(*api, api->GetTensorTypeAndShape(value, &shapeInfo), result.error);
        std::unique_ptr<OrtTensorTypeAndShapeInfo, decltype(releaseShape)> shapeOwner(shapeInfo, releaseShape);
        if (!shapeOk) return result;
        ONNXTensorElementDataType type;
        if (!checkStatus(*api, api->GetTensorElementType(shapeInfo, &type), result.error)) return result;
        if (type != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) { result.error = "model output is not float32"; return result; }
        std::size_t rank = 0;
        if (!checkStatus(*api, api->GetDimensionsCount(shapeInfo, &rank), result.error)) return result;
        if (rank > 8) { result.error = "model output rank is outside the supported range"; return result; }
        FloatTensor tensor;
        tensor.shape.resize(rank);
        if (!checkStatus(*api, api->GetDimensions(shapeInfo, tensor.shape.data(), rank), result.error)) return result;
        std::size_t count = 0;
        if (!checkStatus(*api, api->GetTensorShapeElementCount(shapeInfo, &count), result.error)) return result;
        if (count > kMaxTensorElements) { result.error = "model output tensor is too large"; return result; }
        void* data = nullptr;
        if (!checkStatus(*api, api->GetTensorMutableData(value, &data), result.error)) return result;
        if (count > 0 && !data) { result.error = "ONNX Runtime returned a null tensor buffer"; return result; }
        if (count) {
            const auto* source = static_cast<const float*>(data);
            tensor.values.assign(source, source + count);
        }
        result.outputs.push_back(std::move(tensor));
        api->ReleaseValue(value); value = nullptr;
    }
    result.ok = result.error.empty() && !result.outputs.empty();
    if (session.profiling) SessionCache::recordProfilingRun(session);
    if (profile) {
        static thread_local std::unordered_map<std::string, RunProfile> profiles;
        if (profiles.size() >= 64 && !profiles.contains(session.cacheKey)) profiles.clear();
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

InferenceResult runFloat(const SessionHandle& session, const FloatTensor& input)
{
    const auto* api = ortApi();
    if (!api) { InferenceResult result; result.error = "ONNX Runtime session is not available"; return result; }
    if (session.cudaGraph) {
        auto result = session.cudaGraph->run(input);
        if (result.ok && session.profiling) SessionCache::recordProfilingRun(session);
        return result;
    }
    return detail::runFloatWithApi(*api, session, input);
}

InferenceResult runFloats(const SessionHandle& session,std::span<const FloatTensor> inputs)
{
    if(inputs.size()==1)return runFloat(session,inputs.front());
    const auto* api=ortApi();
    if(!api) {InferenceResult result;result.error="ONNX Runtime session is not available";return result;}
    return detail::runFloatsWithApi(*api,session,inputs);
}

} // namespace pfgpu
