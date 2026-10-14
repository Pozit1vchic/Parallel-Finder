#include "CudaGraphRunner.hpp"
#include "pfgpu/OrtRuntime.hpp"
#include <limits>
#include <mutex>
#include <stdexcept>
#include <cstdio>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <thread>
#include <cstring>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace pfgpu::detail {
namespace {
// ORT CUDA Graph sessions must run on the same thread, not merely under a
// mutex. One worker owns all audit runs and prevents overlapping global capture.
class GraphExecutor {
    std::mutex mutex_;
    std::condition_variable changed_;
    std::deque<std::function<void()>> jobs_;
    std::thread worker_;
public:
    GraphExecutor() : worker_([this] {
        for (;;) {
            std::function<void()> job;
            {
                std::unique_lock lock(mutex_);
                changed_.wait(lock, [this]{ return !jobs_.empty(); });
                job = std::move(jobs_.front()); jobs_.pop_front();
            }
            job();
        }
    }) {}
    InferenceResult invoke(std::function<InferenceResult()> operation) {
        auto task = std::make_shared<std::packaged_task<InferenceResult()>>(std::move(operation));
        auto result = task->get_future();
        {
            const std::lock_guard lock(mutex_);
            jobs_.emplace_back([task]{ (*task)(); });
        }
        changed_.notify_one();
        return result.get();
    }
};
GraphExecutor& graphExecutor() {
    // Like the process OrtEnv, this worker deliberately lives until process
    // termination; ORT background pools must not race shutdown destruction.
    static auto* executor = new GraphExecutor;
    return *executor;
}
constexpr size_t maxElements = 64ULL * 1024 * 1024;
void check(const OrtApi& api, OrtStatus* status) {
    std::string error;
    if (!checkStatus(api, status, error)) throw std::runtime_error(error);
}
size_t count(const std::vector<std::int64_t>& shape) {
    if (shape.empty() || shape.size() > 8) throw std::runtime_error("Invalid CUDA graph tensor rank");
    size_t elements = 1;
    for (const auto dimension : shape) {
        if (dimension <= 0 || static_cast<std::uint64_t>(dimension) > maxElements / elements)
            throw std::runtime_error("CUDA graph requires bounded positive tensor dimensions");
        elements *= static_cast<size_t>(dimension);
    }
    return elements;
}
}
struct CudaGraphRunner::Impl {
    const OrtApi& api;
    OrtSession* session;
    int deviceId;
    OrtIoBinding* binding = nullptr;
    OrtAllocator* allocator = nullptr;
    OrtMemoryInfo* memory = nullptr;
    OrtValue* inputValue = nullptr;
    OrtRunOptions* uncaptured = nullptr;
    std::vector<OrtValue*> outputValues;
    std::vector<std::int64_t> inputShape;
    std::vector<FloatTensor> outputTemplates;
    std::vector<std::string> outputNames;
    using Copy = int (*)(void*, const void*, size_t, int);
    using Synchronize = int (*)();
    using SetDevice = int (*)(int);
    Copy copy = nullptr;
    Synchronize synchronize = nullptr;
    SetDevice setDevice = nullptr;
    bool ready = false;
    bool regularReady = false;
    unsigned parityChecks = 0;
    std::string failure;
    Impl(const OrtApi& a, OrtSession* s, int device) : api(a), session(s), deviceId(device) {}
    ~Impl() {
        if (binding) api.ReleaseIoBinding(binding);
        for (auto* output : outputValues) if (output) api.ReleaseValue(output);
        if (inputValue) api.ReleaseValue(inputValue);
        if (allocator) api.ReleaseAllocator(allocator);
        if (memory) api.ReleaseMemoryInfo(memory);
        if (uncaptured) api.ReleaseRunOptions(uncaptured);
    }
    void loadRuntime() {
#ifdef _WIN32
        const auto module = GetModuleHandleW(L"cudart64_12.dll");
        if (!module) throw std::runtime_error("CUDA graph audit requires the loaded CUDA 12 runtime");
        copy = reinterpret_cast<Copy>(GetProcAddress(module, "cudaMemcpy"));
        synchronize = reinterpret_cast<Synchronize>(GetProcAddress(module, "cudaDeviceSynchronize"));
        setDevice = reinterpret_cast<SetDevice>(GetProcAddress(module, "cudaSetDevice"));
#endif
        if (!copy || !synchronize || !setDevice) throw std::runtime_error("CUDA graph copy/synchronization API unavailable");
    }
    InferenceResult regular(const FloatTensor& input) {
        // Handles a changed input shape without capturing an unbounded number
        // of graphs. It still uses the same float32 CUDA provider and weights.
        OrtMemoryInfo* cpuInfo = nullptr;
        OrtValue* value = nullptr;
        std::vector<OrtValue*> outputs(outputNames.size());
        struct Cleanup {
            Impl& self; OrtMemoryInfo*& info; OrtValue*& input; std::vector<OrtValue*>& outputs;
            ~Cleanup() {
                for (auto* output : outputs) if (output) self.api.ReleaseValue(output);
                if (input) self.api.ReleaseValue(input);
                if (info) self.api.ReleaseMemoryInfo(info);
            }
        } cleanup{*this, cpuInfo, value, outputs};
        check(api, api.CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &cpuInfo));
        check(api, api.CreateTensorWithDataAsOrtValue(cpuInfo, const_cast<float*>(input.values.data()),
            input.values.size()*sizeof(float), input.shape.data(), input.shape.size(),
            ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &value));
        OrtAllocator* cpu = nullptr;
        check(api, api.GetAllocatorWithDefaultOptions(&cpu));
        char* name = nullptr;
        check(api, api.SessionGetInputName(session, 0, cpu, &name));
        const std::string inputName(name);
        check(api, api.AllocatorFree(cpu, name));
        std::vector<const char*> names;
        for (const auto& outputName : outputNames) names.push_back(outputName.c_str());
        const char* inputs[]{inputName.c_str()}; const OrtValue* values[]{value};
        check(api, api.Run(session, uncaptured, inputs, values, 1, names.data(), names.size(), outputs.data()));
        InferenceResult result; result.outputNames = outputNames;
        for (auto* output : outputs) result.outputs.push_back(readOutput(output));
        result.ok = true;
        return result;
    }
    FloatTensor readOutput(OrtValue* value) {
        OrtTensorTypeAndShapeInfo* info = nullptr;
        check(api, api.GetTensorTypeAndShape(value, &info));
        struct Cleanup { const OrtApi& api; OrtTensorTypeAndShapeInfo* info; ~Cleanup(){api.ReleaseTensorTypeAndShapeInfo(info);} } cleanup{api,info};
        ONNXTensorElementDataType type;
        check(api, api.GetTensorElementType(info, &type));
        if (type != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) throw std::runtime_error("CUDA graph output is not float32");
        size_t rank = 0;
        check(api, api.GetDimensionsCount(info, &rank));
        if (!rank || rank > 8) throw std::runtime_error("Invalid CUDA graph output rank");
        FloatTensor output; output.shape.resize(rank);
        check(api, api.GetDimensions(info, output.shape.data(), rank));
        const size_t elements = count(output.shape);
        void* data = nullptr; check(api, api.GetTensorMutableData(value, &data));
        if (!data) throw std::runtime_error("Null CUDA graph output");
        output.values.assign(static_cast<float*>(data), static_cast<float*>(data)+elements);
        return output;
    }
    void prepareRegular() {
        if(regularReady)return;
        check(api, api.CreateRunOptions(&uncaptured));
        check(api, api.AddRunConfigEntry(uncaptured, "gpu_graph_id", "-1"));
        size_t outputs = 0; check(api, api.SessionGetOutputCount(session, &outputs));
        if (!outputs || outputs > 64) throw std::runtime_error("Invalid CUDA graph output count");
        OrtAllocator* cpu = nullptr; check(api, api.GetAllocatorWithDefaultOptions(&cpu));
        for (size_t i = 0; i < outputs; ++i) {
            char* name = nullptr; check(api, api.SessionGetOutputName(session, i, cpu, &name));
            outputNames.emplace_back(name); check(api, api.AllocatorFree(cpu, name));
        }
        regularReady=true;
    }
    void initialize(const FloatTensor& input) {
        prepareRegular();
        loadRuntime();
        if (setDevice(deviceId)) throw std::runtime_error("CUDA graph device selection failed");
        OrtAllocator* cpu = nullptr; check(api, api.GetAllocatorWithDefaultOptions(&cpu));
        auto first = regular(input);
        outputTemplates = std::move(first.outputs);
        for (auto& output : outputTemplates) output.values.clear();
        check(api, api.CreateMemoryInfo("Cuda", OrtArenaAllocator, deviceId, OrtMemTypeDefault, &memory));
        check(api, api.CreateAllocator(session, memory, &allocator));
        check(api, api.CreateTensorAsOrtValue(allocator, input.shape.data(), input.shape.size(),
            ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &inputValue));
        inputShape = input.shape;
        check(api, api.CreateIoBinding(session, &binding));
        char* name = nullptr; check(api, api.SessionGetInputName(session, 0, cpu, &name));
        const std::string inputName(name); check(api, api.AllocatorFree(cpu, name));
        check(api, api.BindInput(binding, inputName.c_str(), inputValue));
        for (size_t i = 0; i < outputTemplates.size(); ++i) {
            const auto& shape = outputTemplates[i].shape;
            OrtValue* output = nullptr;
            check(api, api.CreateTensorAsOrtValue(allocator, shape.data(), shape.size(),
                ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &output));
            outputValues.push_back(output);
            check(api, api.BindOutput(binding, outputNames[i].c_str(), output));
        }
        ready = true;
        std::fprintf(stderr, "PF_DEBUG_CUDA_GRAPH initialized outputs=%zu elements=%zu\n", outputNames.size(), input.values.size());
    }
};
CudaGraphRunner::CudaGraphRunner(const OrtApi& api, OrtSession* session, int deviceId)
    : impl_(std::make_unique<Impl>(api, session, deviceId)) {}
CudaGraphRunner::~CudaGraphRunner() = default;
InferenceResult CudaGraphRunner::run(const FloatTensor& input) {
    try {
        if (count(input.shape) != input.values.size()) {
            InferenceResult result; result.error = "CUDA graph input size mismatch"; return result;
        }
    } catch (const std::exception& error) {
        InferenceResult result; result.error = error.what(); return result;
    }
    return graphExecutor().invoke([this, &input] {
    InferenceResult result;
    try {
        auto& state = *impl_;
        if (!state.failure.empty()) {state.prepareRegular();return state.regular(input);}
        if (!state.ready) state.initialize(input);
        if (state.setDevice(state.deviceId)) throw std::runtime_error("CUDA graph device selection failed");
        if (input.shape != state.inputShape) return state.regular(input);
        // Validate real inputs, including the first captured execution, with
        // the identical session in ordinary float32 mode. A numerical change
        // disables replay for this session instead of changing detections.
        InferenceResult reference;
        if(state.parityChecks<5)reference=state.regular(input);
        void* data = nullptr; check(state.api, state.api.GetTensorMutableData(state.inputValue, &data));
        if (state.copy(data, input.values.data(), input.values.size()*sizeof(float), 1) || state.synchronize())
            throw std::runtime_error("CUDA graph input transfer failed");
        check(state.api, state.api.RunWithBinding(state.session, nullptr, state.binding));
        check(state.api, state.api.SynchronizeBoundOutputs(state.binding));
        if (state.synchronize()) throw std::runtime_error("CUDA graph output synchronization failed");
        result.outputNames = state.outputNames; result.outputs = state.outputTemplates;
        for (size_t i = 0; i < result.outputs.size(); ++i) {
            auto& output = result.outputs[i]; output.values.resize(count(output.shape));
            check(state.api, state.api.GetTensorMutableData(state.outputValues[i], &data));
            if (state.copy(output.values.data(), data, output.values.size()*sizeof(float), 2))
                throw std::runtime_error("CUDA graph output transfer failed");
        }
        result.ok = true;
        if(state.parityChecks<5) {
            bool same=result.outputNames==reference.outputNames && result.outputs.size()==reference.outputs.size();
            for(size_t i=0;same && i<result.outputs.size();++i) {
                const auto& a=result.outputs[i];const auto& b=reference.outputs[i];
                same=a.shape==b.shape && a.values.size()==b.values.size()
                    && std::memcmp(a.values.data(),b.values.data(),a.values.size()*sizeof(float))==0;
            }
            if(!same) {
                state.failure="CUDA graph float32 parity mismatch";
                std::fprintf(stderr,"PF_DEBUG_CUDA_GRAPH disabled reason=%s\n",state.failure.c_str());
                return reference;
            }
            ++state.parityChecks;
        }
    } catch (const std::exception& error) {
        impl_->failure=error.what();
        std::fprintf(stderr,"PF_DEBUG_CUDA_GRAPH disabled reason=%s\n",impl_->failure.c_str());
        try {impl_->prepareRegular();return impl_->regular(input);}
        catch(const std::exception& fallback) {result.error=impl_->failure+"; ordinary CUDA: "+fallback.what();}
    }
    return result;
    });
}
}
