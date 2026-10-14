#include <gtest/gtest.h>
#include "../../gpu/src/InferenceInternal.hpp"
#include <limits>
#include <string>
#include <atomic>
#include <array>
#include <barrier>
#include <chrono>
#include <thread>

namespace {
struct RuntimeSpy {
    int names = 0, freedNames = 0, values = 0, freedValues = 0;
    int memories = 0, freedMemories = 0, shapes = 0, freedShapes = 0;
    int releasedStatuses = 0, runs = 0;
    std::size_t inputs = 1, outputs = 2, rank = 1, elements = 2, receivedInputs=0;
    std::vector<std::string> receivedInputNames;
    int inputCreates=0;
    std::string fail;
    bool nullData = false;
    int statusToken = 0;
};
thread_local RuntimeSpy spy;
std::atomic_int activeRuns{0}, peakRuns{0};
std::atomic_bool delayRun{false};
OrtStatus* failure(const char* stage) noexcept
{ return spy.fail == stage ? reinterpret_cast<OrtStatus*>(&spy.statusToken) : nullptr; }
const char* ORT_API_CALL errorMessage(const OrtStatus*) noexcept { return "test runtime failure"; }
void ORT_API_CALL releaseStatus(OrtStatus*) noexcept { ++spy.releasedStatuses; }
OrtStatus* ORT_API_CALL allocator(OrtAllocator** out) noexcept
{ *out = reinterpret_cast<OrtAllocator*>(&spy.statusToken); return nullptr; }
OrtStatus* ORT_API_CALL freeName(OrtAllocator*, void* value) noexcept
{ delete[] static_cast<char*>(value); ++spy.freedNames; return nullptr; }
OrtStatus* ORT_API_CALL inputName(const OrtSession*, std::size_t index, OrtAllocator*, char** out) noexcept
{ *out = new char[3]{'x',static_cast<char>('0'+index),0}; ++spy.names;
  return index==1 && spy.fail=="second-input-name" ? failure("second-input-name"):failure("input-name"); }
OrtStatus* ORT_API_CALL inputCount(const OrtSession*,std::size_t* out) noexcept
{*out=spy.inputs;return failure("input-count");}
OrtStatus* ORT_API_CALL outputName(const OrtSession*, std::size_t index, OrtAllocator*, char** out) noexcept
{ *out = new char[2]{'y',0}; ++spy.names; return index == 1 ? failure("output-name") : nullptr; }
OrtStatus* ORT_API_CALL memoryInfo(OrtAllocatorType, OrtMemType, OrtMemoryInfo** out) noexcept
{ *out = reinterpret_cast<OrtMemoryInfo*>(new int(1)); ++spy.memories; return failure("memory"); }
void ORT_API_CALL releaseMemory(OrtMemoryInfo* value) noexcept
{ delete reinterpret_cast<int*>(value); ++spy.freedMemories; }
OrtValue* newValue() { ++spy.values; return reinterpret_cast<OrtValue*>(new int(1)); }
OrtStatus* ORT_API_CALL tensor(const OrtMemoryInfo*, void*, std::size_t, const std::int64_t*,
                             std::size_t, ONNXTensorElementDataType, OrtValue** out) noexcept
{ *out = newValue();++spy.inputCreates;
  return spy.inputCreates==2 && spy.fail=="second-input-value" ? failure("second-input-value"):failure("input-value"); }
void ORT_API_CALL releaseValue(OrtValue* value) noexcept
{ delete reinterpret_cast<int*>(value); ++spy.freedValues; }
OrtStatus* ORT_API_CALL outputCount(const OrtSession*, std::size_t* out) noexcept
{ *out = spy.outputs; return failure("output-count"); }
OrtStatus* ORT_API_CALL run(OrtSession*, const OrtRunOptions*, const char* const* names, const OrtValue* const*,
                           std::size_t inputs, const char* const*, std::size_t count, OrtValue** out) noexcept
{
    ++spy.runs;
    spy.receivedInputs=inputs;
    for(std::size_t i=0;i<inputs;++i)spy.receivedInputNames.emplace_back(names[i]);
    if (delayRun) {
        const int active = ++activeRuns;
        int previous = peakRuns.load();
        while (previous < active && !peakRuns.compare_exchange_weak(previous, active)) {}
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        --activeRuns;
    }
    for (std::size_t i=0; i<count; ++i) out[i]=newValue();
    return failure("run");
}
OrtStatus* ORT_API_CALL shape(const OrtValue*, OrtTensorTypeAndShapeInfo** out) noexcept
{ *out=reinterpret_cast<OrtTensorTypeAndShapeInfo*>(new int(1)); ++spy.shapes; return failure("shape"); }
void ORT_API_CALL releaseShape(OrtTensorTypeAndShapeInfo* value) noexcept
{ delete reinterpret_cast<int*>(value); ++spy.freedShapes; }
OrtStatus* ORT_API_CALL elementType(const OrtTensorTypeAndShapeInfo*, ONNXTensorElementDataType* out) noexcept
{ *out=ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT; return failure("type"); }
OrtStatus* ORT_API_CALL rank(const OrtTensorTypeAndShapeInfo*, std::size_t* out) noexcept
{ *out=spy.rank; return failure("rank"); }
OrtStatus* ORT_API_CALL dimensions(const OrtTensorTypeAndShapeInfo*, std::int64_t* out, std::size_t count) noexcept
{ for(std::size_t i=0;i<count;++i) out[i]=static_cast<std::int64_t>(spy.elements); return failure("dimensions"); }
OrtStatus* ORT_API_CALL elements(const OrtTensorTypeAndShapeInfo*, std::size_t* out) noexcept
{ *out=spy.elements; return failure("elements"); }
OrtStatus* ORT_API_CALL data(OrtValue*, void** out) noexcept
{ static float values[]{4.25F,-2.5F}; *out=spy.nullData ? nullptr : values; return failure("data"); }
OrtApi api()
{
    OrtApi result{};
    result.GetErrorMessage=errorMessage; result.ReleaseStatus=releaseStatus;
    result.GetAllocatorWithDefaultOptions=allocator; result.AllocatorFree=freeName;
    result.SessionGetInputName=inputName; result.SessionGetOutputName=outputName;
    result.SessionGetInputCount=inputCount;
    result.CreateCpuMemoryInfo=memoryInfo; result.ReleaseMemoryInfo=releaseMemory;
    result.CreateTensorWithDataAsOrtValue=tensor; result.ReleaseValue=releaseValue;
    result.SessionGetOutputCount=outputCount; result.Run=run;
    result.GetTensorTypeAndShape=shape; result.ReleaseTensorTypeAndShapeInfo=releaseShape;
    result.GetTensorElementType=elementType; result.GetDimensionsCount=rank;
    result.GetDimensions=dimensions; result.GetTensorShapeElementCount=elements;
    result.GetTensorMutableData=data;
    return result;
}
pfgpu::InferenceResult infer()
{
    pfgpu::SessionHandle session;
    session.session=reinterpret_cast<OrtSession*>(&spy.statusToken);
    return pfgpu::detail::runFloatWithApi(api(),session,{{1,2},{4.25F,-2.5F}});
}
void expectReleased()
{
    EXPECT_EQ(spy.names,spy.freedNames);
    EXPECT_EQ(spy.memories,spy.freedMemories);
    EXPECT_EQ(spy.values,spy.freedValues);
    EXPECT_EQ(spy.shapes,spy.freedShapes);
}
}

TEST(InferenceFailures, EveryFailureReleasesPartialResourcesAndRemainingOutputs)
{
    for (const auto* stage : {"input-count","input-name","memory","input-value","output-count","output-name",
                             "run","shape","type","rank","dimensions","elements","data"}) {
        SCOPED_TRACE(stage);
        spy={}; spy.fail=stage;
        const auto result=infer();
        EXPECT_FALSE(result.ok);
        EXPECT_EQ(result.error,"test runtime failure");
        EXPECT_EQ(spy.releasedStatuses,1);
        expectReleased();
    }
}

TEST(InferenceFailures, MultipleInputsBindInGraphOrderAndReleasePartialSecondInputs)
{
    pfgpu::SessionHandle session;session.session=reinterpret_cast<OrtSession*>(&spy.statusToken);
    const std::array<pfgpu::FloatTensor,2> inputs{{{{1,2},{4.25F,-2.5F}},{{1,2},{1.F,3.F}}}};
    for(const auto* stage:{"second-input-name","second-input-value","run","data"}) {
        SCOPED_TRACE(stage);spy={};spy.inputs=2;spy.fail=stage;
        EXPECT_FALSE(pfgpu::detail::runFloatsWithApi(api(),session,inputs).ok);
        EXPECT_EQ(spy.releasedStatuses,1);expectReleased();
    }
    spy={};spy.inputs=2;
    const auto result=pfgpu::detail::runFloatsWithApi(api(),session,inputs);
    ASSERT_TRUE(result.ok)<<result.error;
    EXPECT_EQ(spy.receivedInputs,2U);
    EXPECT_EQ(spy.receivedInputNames,(std::vector<std::string>{"x0","x1"}));
    expectReleased();
    spy={};spy.inputs=1;
    EXPECT_FALSE(pfgpu::detail::runFloatsWithApi(api(),session,inputs).ok);
    EXPECT_EQ(spy.runs,0);EXPECT_EQ(spy.names,0);expectReleased();
    auto malformed=inputs;malformed[1].shape={2,2};
    spy={};spy.inputs=2;
    EXPECT_FALSE(pfgpu::detail::runFloatsWithApi(api(),session,malformed).ok);
    EXPECT_EQ(spy.runs,0);EXPECT_EQ(spy.names,0);expectReleased();
}

TEST(InferenceConcurrency, SharedDirectMlSessionNeverOverlapsRuntimeRun)
{
    pfgpu::SessionHandle session;
    int token = 0;
    session.session = reinterpret_cast<OrtSession*>(&token);
    session.provider = pfgpu::Provider::Dml;
    session.runMutex = std::make_shared<std::mutex>();
    activeRuns = 0; peakRuns = 0; delayRun = true;
    struct Reset { ~Reset() { delayRun = false; } } reset;
    std::barrier start(4);
    std::atomic_bool valid{true};
    std::array<std::jthread, 4> workers;
    for (auto& worker : workers) {
        worker = std::jthread([&, borrowed = session] {
            spy = {};
            start.arrive_and_wait();
            const auto output = pfgpu::detail::runFloatWithApi(api(), borrowed, {{1,2},{4.25F,-2.5F}});
            if (!output.ok || output.outputs[0].values != std::vector<float>{4.25F,-2.5F}
                || spy.values != spy.freedValues || spy.names != spy.freedNames) valid = false;
        });
    }
    for (auto& worker : workers) worker.join();
    EXPECT_TRUE(valid);
    EXPECT_EQ(peakRuns.load(), 1);
    EXPECT_EQ(activeRuns.load(), 0);
}

TEST(InferenceFailures, RejectsOutputMetadataBeforeUnboundedAllocation)
{
    for (const auto count : {std::size_t(0),std::size_t(65),std::numeric_limits<std::size_t>::max()}) {
        spy={}; spy.outputs=count;
        EXPECT_FALSE(infer().ok);
        EXPECT_EQ(spy.runs,0);
        expectReleased();
    }
    spy={}; spy.rank=std::numeric_limits<std::size_t>::max();
    EXPECT_FALSE(infer().ok);
    expectReleased();
    spy={}; spy.elements=std::numeric_limits<std::size_t>::max();
    EXPECT_FALSE(infer().ok);
    expectReleased();
}

TEST(InferenceFailures, HandlesEmptyTensorWithoutNullPointerArithmeticAndCopiesNormalOutput)
{
    spy={}; spy.elements=0; spy.nullData=true;
    auto result=infer();
    ASSERT_TRUE(result.ok) << result.error;
    ASSERT_EQ(result.outputs.size(),2U);
    EXPECT_TRUE(result.outputs[0].values.empty());
    expectReleased();
    spy={}; spy.nullData=true;
    EXPECT_FALSE(infer().ok);
    expectReleased();
    spy={};
    result=infer();
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.outputs[0].values,(std::vector<float>{4.25F,-2.5F}));
    EXPECT_EQ(result.outputs[1].values,result.outputs[0].values);
    expectReleased();
}
