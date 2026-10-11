#include <pfgpu/Inference.hpp>
#include <pfgpu/OrtRuntime.hpp>
#include <pfgpu/ProviderFactory.hpp>
#include <pfgpu/detail/PoseInput.hpp>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QLibrary>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <vector>

// A diagnostic only. Fixed float32 tensors, same provider settings, serialized
// sessions. Timings include input/output copies. Synthetic parity is necessary
// but does not establish classification accuracy or whole-analysis speedup.
namespace {
using Clock = std::chrono::steady_clock;
double ms(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}
struct Session {
    const OrtApi& api;
    OrtSession* value = nullptr;
    explicit Session(const OrtApi& a) : api(a) {}
    ~Session() { if (value) api.ReleaseSession(value); }
};
struct Binding {
    const OrtApi& api;
    OrtIoBinding* value = nullptr;
    OrtAllocator* allocator = nullptr;
    OrtMemoryInfo* memory = nullptr;
    OrtValue* input = nullptr;
    std::vector<OrtValue*> outputs;
    explicit Binding(const OrtApi& a) : api(a) {}
    ~Binding() {
        if (value) api.ReleaseIoBinding(value);
        for (auto* output : outputs) if (output) api.ReleaseValue(output);
        if (input) api.ReleaseValue(input);
        if (allocator) api.ReleaseAllocator(allocator);
        if (memory) api.ReleaseMemoryInfo(memory);
    }
};
void checked(const OrtApi& api, OrtStatus* status) {
    std::string error;
    if (!pfgpu::checkStatus(api, status, error)) throw std::runtime_error(error);
}
void create(Session& session, const std::filesystem::path& path, bool graph) {
    auto& api = session.api;
    OrtSessionOptions* options = nullptr;
    checked(api, api.CreateSessionOptions(&options));
    const auto release = [&] { api.ReleaseSessionOptions(options); };
    try {
        checked(api, api.SetIntraOpNumThreads(options, 1));
        checked(api, api.SetSessionGraphOptimizationLevel(options, ORT_ENABLE_ALL));
        checked(api, api.AddSessionConfigEntry(options, "session.intra_op.allow_spinning", "0"));
        checked(api, api.AddSessionConfigEntry(options, "session.inter_op.allow_spinning", "0"));
        std::string error;
        auto* env = pfgpu::sharedOrtEnv(error);
        if (!env) throw std::runtime_error(error);
        const auto* factory = pfgpu::findProviderFactory(pfgpu::Provider::Cuda);
        pfgpu::IProviderFactory::OptionList providerOptions{{"enable_cuda_graph", graph ? "1" : "0"}};
        if (!factory || !factory->configure(api, *options, providerOptions, error))
            throw std::runtime_error(error.empty() ? "CUDA factory unavailable" : error);
        checked(api, api.CreateSession(env, path.c_str(), options, &session.value));
        release();
    } catch (...) { release(); throw; }
}
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.size() != 4 && args.size() != 5) {
        std::cerr << "usage: pf_cuda_graph_probe model.onnx cudart.dll repetitions [pose-frames-directory]\n";
        return 2;
    }
    try {
        const auto* api = pfgpu::ortApi();
        if (!api) throw std::runtime_error(pfgpu::ortRuntimeStatus().error);
        QLibrary cuda(args[2]);
        if (!cuda.load()) throw std::runtime_error(cuda.errorString().toStdString());
        using Copy = int (*)(void*, const void*, size_t, int);
        using Synchronize = int (*)();
        const auto copy = reinterpret_cast<Copy>(cuda.resolve("cudaMemcpy"));
        const auto synchronize = reinterpret_cast<Synchronize>(cuda.resolve("cudaDeviceSynchronize"));
        if (!copy || !synchronize) throw std::runtime_error("CUDA copy/synchronization unavailable");
        const auto transfer = [&](void* dst, const void* src, size_t bytes, int kind) {
            if (copy(dst, src, bytes, kind) != 0) throw std::runtime_error("cudaMemcpy failed");
        };
        const auto path = std::filesystem::path(args[1].toStdWString());
        Session regular(*api), graph(*api);
        create(regular, path, false);
        pfgpu::SessionHandle handle;
        handle.session = regular.value;
        handle.provider = pfgpu::Provider::Cuda;
        const auto spec = pfgpu::describeSession(handle);
        if (!spec.ok || spec.input.shape.size() != 4) throw std::runtime_error("Requires one rank-four float input");
        pfgpu::FloatTensor input;
        input.shape = spec.input.shape;
        size_t elements = 1;
        for (size_t d = 0; d < input.shape.size(); ++d) {
            if (input.shape[d] <= 0) input.shape[d] = d == 0 ? 1 : (d == 1 ? 3 : 640);
            if (input.shape[d] > 4096) throw std::runtime_error("Input dimension too large");
            elements *= static_cast<size_t>(input.shape[d]);
        }
        if (elements > 64 * 1024 * 1024) throw std::runtime_error("Input too large");
        input.values.resize(elements);
        for (size_t i = 0; i < elements; ++i) input.values[i] = static_cast<float>((i * 37) % 251) / 250.f;
        std::vector<pfgpu::FloatTensor> realInputs;
        if (args.size() == 5) {
            if (input.shape[0] != 1 || input.shape[1] != 3)
                throw std::runtime_error("Real pose probe requires batch-one NCHW");
            const QDir frames(args[4]);
            const auto names = frames.entryList({"*.png", "*.jpg"}, QDir::Files, QDir::Name);
            if (names.isEmpty() || names.size() > 128) throw std::runtime_error("Expected 1..128 pose frames");
            pfgpu::detail::PoseInputWorkspace workspace;
            pfgpu::PoseEstimatorParams params;
            params.inputWidth = static_cast<int>(input.shape[3]);
            params.inputHeight = static_cast<int>(input.shape[2]);
            for (const auto& name : names) {
                const auto image = QImage(frames.filePath(name)).convertToFormat(QImage::Format_RGBA8888);
                if (image.isNull()) throw std::runtime_error("Cannot read pose frame");
                const pfgpu::PoseImage frame{image.width(), image.height(), image.constBits()};
                std::vector<pfgpu::detail::LetterboxTransform> transforms;
                realInputs.push_back(workspace.build(std::span(&frame, 1), 1, params, transforms));
            }
            input = realInputs.front();
        }
        auto reference = pfgpu::runFloat(handle, input);
        if (!reference.ok) throw std::runtime_error(reference.error);
        std::cerr << "regular ready, outputs=" << reference.outputs.size() << '\n';
        create(graph, path, true);
        Binding bound(*api);
        checked(*api, api->CreateMemoryInfo("Cuda", OrtArenaAllocator, 0, OrtMemTypeDefault, &bound.memory));
        checked(*api, api->CreateAllocator(graph.value, bound.memory, &bound.allocator));
        checked(*api, api->CreateTensorAsOrtValue(bound.allocator, input.shape.data(), input.shape.size(),
            ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &bound.input));
        checked(*api, api->CreateIoBinding(graph.value, &bound.value));
        OrtAllocator* cpu = nullptr;
        checked(*api, api->GetAllocatorWithDefaultOptions(&cpu));
        char* name = nullptr;
        checked(*api, api->SessionGetInputName(graph.value, 0, cpu, &name));
        const std::string inputName(name);
        checked(*api, api->AllocatorFree(cpu, name));
        checked(*api, api->BindInput(bound.value, inputName.c_str(), bound.input));
        for (size_t i = 0; i < reference.outputs.size(); ++i) {
            const auto& output = reference.outputs[i];
            OrtValue* value = nullptr;
            checked(*api, api->CreateTensorAsOrtValue(bound.allocator, output.shape.data(), output.shape.size(),
                ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &value));
            bound.outputs.push_back(value);
            checked(*api, api->BindOutput(bound.value, reference.outputNames[i].c_str(), value));
        }
        const auto runGraph = [&] {
            void* inputData = nullptr;
            checked(*api, api->GetTensorMutableData(bound.input, &inputData));
            transfer(inputData, input.values.data(), elements * sizeof(float), 1); // host to device
            if (synchronize() != 0) throw std::runtime_error("CUDA input synchronization failed");
            checked(*api, api->RunWithBinding(graph.value, nullptr, bound.value));
            checked(*api, api->SynchronizeBoundOutputs(bound.value));
            if (synchronize() != 0) throw std::runtime_error("CUDA output synchronization failed");
            auto outputs = reference.outputs;
            for (size_t i = 0; i < outputs.size(); ++i) {
                void* data = nullptr;
                checked(*api, api->GetTensorMutableData(bound.outputs[i], &data));
                transfer(outputs[i].values.data(), data, outputs[i].values.size() * sizeof(float), 2);
            }
            return outputs;
        };
        runGraph(); // capture, excluded from warmed timing
        size_t unequal = 0, total = 0;
        double maxDifference = 0;
        QJsonArray outputShapes;
        for (const auto& output : reference.outputs) {
            QJsonArray shape;
            for (auto d : output.shape) shape.append(static_cast<double>(d));
            outputShapes.append(shape);
        }
        const int repetitions = std::clamp(args[3].toInt(), 10, 10000);
        double regularMs = 0, graphMs = 0;
        for (int run = 0; run < repetitions; ++run) {
            // Alternate order and change inputs: stale capture outputs cannot pass.
            if (realInputs.empty())
                input.values[static_cast<size_t>(run) % elements] = static_cast<float>(run % 101) / 100.f;
            else input = realInputs[static_cast<size_t>(run) % realInputs.size()];
            pfgpu::InferenceResult normal;
            std::vector<pfgpu::FloatTensor> captured;
            const auto regularRun = [&] {
                const auto start = Clock::now(); normal = pfgpu::runFloat(handle, input); regularMs += ms(start);
                if (!normal.ok) throw std::runtime_error(normal.error);
            };
            const auto graphRun = [&] {
                const auto start = Clock::now(); captured = runGraph(); graphMs += ms(start);
            };
            if (run % 2) { graphRun(); regularRun(); } else { regularRun(); graphRun(); }
            const auto dumpRoot = qEnvironmentVariable("PF_CUDA_GRAPH_OUTPUT_ROOT");
            if (!dumpRoot.isEmpty() && run < static_cast<int>(realInputs.size())) {
                if (!QDir().mkpath(dumpRoot)) throw std::runtime_error("Cannot create diagnostic output directory");
                for (size_t o = 0; o < captured.size(); ++o) {
                    const auto dump = [&](const char* label, const std::vector<float>& values) {
                        QFile file(QDir(dumpRoot).filePath(QString("%1-%2-%3.f32").arg(run).arg(o).arg(label)));
                        if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly))
                            throw std::runtime_error("Diagnostic output already exists or cannot be written");
                        const auto bytes = static_cast<qint64>(values.size() * sizeof(float));
                        if (file.write(reinterpret_cast<const char*>(values.data()), bytes) != bytes)
                            throw std::runtime_error("Diagnostic output write failed");
                    };
                    dump("regular", normal.outputs[o].values);
                    dump("graph", captured[o].values);
                }
            }
            for (size_t o = 0; o < captured.size(); ++o) {
                if (captured[o].shape != normal.outputs[o].shape) throw std::runtime_error("Shape changed");
                for (size_t v = 0; v < captured[o].values.size(); ++v) {
                    const float a = captured[o].values[v], b = normal.outputs[o].values[v];
                    ++total;
                    if (std::memcmp(&a, &b, sizeof(float))) ++unequal;
                    if (!std::isfinite(a) || !std::isfinite(b)) throw std::runtime_error("Nonfinite output");
                    maxDifference = std::max(maxDifference, std::abs(static_cast<double>(a) - b));
                }
            }
        }
        const QJsonObject report{{"model", args[1]}, {"repetitions", repetitions},
            {"syntheticInput", realInputs.empty()}, {"realFrames", static_cast<int>(realInputs.size())},
            {"outputShapes", outputShapes},
            {"regularTotalMs", regularMs}, {"graphTotalMs", graphMs},
            {"speedup", regularMs / graphMs}, {"comparedFloats", static_cast<double>(total)},
            {"unequalFloats", static_cast<double>(unequal)}, {"maxAbsoluteDifference", maxDifference},
            {"bitwiseIdentical", unequal == 0}};
        std::cout << QJsonDocument(report).toJson(QJsonDocument::Indented).constData();
        return unequal ? 3 : 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
