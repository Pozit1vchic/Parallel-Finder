#include "pfgpu/Inference.hpp"

#include <limits>

#include "pfgpu/OrtRuntime.hpp"

namespace pfgpu {

InferenceResult runFloat(const SessionHandle& session, const FloatTensor& input)
{
    InferenceResult result;
    const OrtApi* api = ortApi();
    if (!api || !session.session) { result.error = "ONNX Runtime session is not available"; return result; }
    if (input.shape.empty() || input.values.empty()) { result.error = "inference input tensor is empty"; return result; }
    std::size_t elements = 1;
    for (const auto dimension : input.shape) {
        if (dimension <= 0 || static_cast<std::uint64_t>(dimension) > std::numeric_limits<std::size_t>::max() / elements) {
            result.error = "inference input shape is invalid"; return result;
        }
        elements *= static_cast<std::size_t>(dimension);
    }
    if (elements != input.values.size()) { result.error = "input shape does not match value count"; return result; }

    OrtAllocator* allocator = nullptr;
    if (!checkStatus(*api, api->GetAllocatorWithDefaultOptions(&allocator), result.error)) return result;
    char* inputName = nullptr;
    if (!checkStatus(*api, api->SessionGetInputName(session.session, 0, allocator, &inputName), result.error)) return result;
    OrtMemoryInfo* memory = nullptr;
    if (!checkStatus(*api, api->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &memory), result.error)) {
        api->AllocatorFree(allocator, inputName); return result;
    }
    OrtValue* inputValue = nullptr;
    if (!checkStatus(*api, api->CreateTensorWithDataAsOrtValue(memory, const_cast<float*>(input.values.data()),
        input.values.size() * sizeof(float), input.shape.data(), input.shape.size(), ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &inputValue), result.error)) {
        api->ReleaseMemoryInfo(memory); api->AllocatorFree(allocator, inputName); return result;
    }
    const char* inputNames[] = {inputName};
    std::size_t outputCount = 0;
    if (!checkStatus(*api, api->SessionGetOutputCount(session.session, &outputCount), result.error)) {
        api->ReleaseValue(inputValue); api->ReleaseMemoryInfo(memory); api->AllocatorFree(allocator, inputName); return result;
    }
    std::vector<const char*> outputNames(outputCount);
    std::vector<char*> ownedNames(outputCount);
    for (std::size_t i = 0; i < outputCount; ++i) {
        if (!checkStatus(*api, api->SessionGetOutputName(session.session, i, allocator, &ownedNames[i]), result.error)) {
            for (char* name : ownedNames) if (name) api->AllocatorFree(allocator, name);
            api->ReleaseValue(inputValue); api->ReleaseMemoryInfo(memory); api->AllocatorFree(allocator, inputName); return result;
        }
        outputNames[i] = ownedNames[i];
    }
    std::vector<OrtValue*> outputs(outputCount);
    const OrtValue* inputValues[] = {inputValue};
    const OrtStatus* runStatus = api->Run(session.session, nullptr, inputNames, inputValues, 1,
                                           outputNames.data(), outputNames.size(), outputs.data());
    const bool ran = checkStatus(*api, const_cast<OrtStatus*>(runStatus), result.error);
    for (char* name : ownedNames) if (name) api->AllocatorFree(allocator, name);
    api->AllocatorFree(allocator, inputName);
    api->ReleaseValue(inputValue);
    api->ReleaseMemoryInfo(memory);
    if (!ran) { for (auto* value : outputs) if (value) api->ReleaseValue(value); return result; }
    for (OrtValue* value : outputs) {
        if (!value) { result.error = "ONNX Runtime returned a null output"; continue; }
        OrtTensorTypeAndShapeInfo* shapeInfo = nullptr;
        if (!checkStatus(*api, api->GetTensorTypeAndShape(value, &shapeInfo), result.error)) { api->ReleaseValue(value); continue; }
        ONNXTensorElementDataType type;
        api->GetTensorElementType(shapeInfo, &type);
        if (type != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) { result.error = "model output is not float32"; api->ReleaseTensorTypeAndShapeInfo(shapeInfo); api->ReleaseValue(value); continue; }
        std::size_t rank = 0; api->GetDimensionsCount(shapeInfo, &rank);
        FloatTensor tensor; tensor.shape.resize(rank); api->GetDimensions(shapeInfo, tensor.shape.data(), rank);
        std::size_t count = 0;
        if (!checkStatus(*api, api->GetTensorShapeElementCount(shapeInfo, &count), result.error)) { api->ReleaseTensorTypeAndShapeInfo(shapeInfo); api->ReleaseValue(value); continue; }
        // GetTensorMutableData returns a pointer owned by ORT.  Passing a
        // pointer to our vector here does not make ORT write into that vector;
        // the API overwrites the pointer argument.  Copy the returned buffer
        // before releasing the OrtValue, otherwise every inference would
        // silently return a zero-filled tensor.
        void* data = nullptr;
        if (!checkStatus(*api, api->GetTensorMutableData(value, &data), result.error)) { api->ReleaseTensorTypeAndShapeInfo(shapeInfo); api->ReleaseValue(value); continue; }
        if (count > 0 && !data) {
            result.error = "ONNX Runtime returned a null tensor buffer";
            api->ReleaseTensorTypeAndShapeInfo(shapeInfo);
            api->ReleaseValue(value);
            continue;
        }
        const auto* source = static_cast<const float*>(data);
        tensor.values.assign(source, source + count);
        result.outputs.push_back(std::move(tensor));
        api->ReleaseTensorTypeAndShapeInfo(shapeInfo); api->ReleaseValue(value);
    }
    result.ok = result.error.empty() && !result.outputs.empty();
    return result;
}

} // namespace pfgpu
