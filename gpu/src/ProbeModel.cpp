#include "pfgpu/ProbeModel.hpp"

#include <cstdint>
#include <initializer_list>
#include <string>

// The probe model is a ~120 byte ONNX file. Writing it here with a hand-rolled
// protobuf writer keeps the repository free of binary fixtures (models/ is
// gitignored on purpose) while staying deterministic and inspectable: the bytes
// are the same on every platform and every build.
namespace pfgpu {
namespace {

// ── protobuf wire format ────────────────────────────────────────────────────
enum WireType : std::uint32_t {
    Varint = 0,
    LengthDelimited = 2,
};

void putVarint(std::string& out, std::uint64_t value)
{
    while (value >= 0x80u) {
        out.push_back(static_cast<char>((value & 0x7Fu) | 0x80u));
        value >>= 7;
    }
    out.push_back(static_cast<char>(value));
}

void putTag(std::string& out, std::uint32_t field, std::uint32_t wireType)
{
    putVarint(out, (static_cast<std::uint64_t>(field) << 3) | wireType);
}

void putVarintField(std::string& out, std::uint32_t field, std::uint64_t value)
{
    putTag(out, field, Varint);
    putVarint(out, value);
}

void putBytesField(std::string& out, std::uint32_t field, std::string_view bytes)
{
    putTag(out, field, LengthDelimited);
    putVarint(out, bytes.size());
    out.append(bytes);
}

// ── ONNX building blocks ────────────────────────────────────────────────────
// Field numbers follow onnx.proto: ModelProto/GraphProto/NodeProto/TypeProto.

std::string makeTensorShape(std::initializer_list<std::int64_t> dims)
{
    std::string shape;
    for (const std::int64_t dim : dims) {
        std::string dimension;
        putVarintField(dimension, 1 /* dim_value */,
                       static_cast<std::uint64_t>(dim));
        putBytesField(shape, 1 /* TensorShapeProto.dim */, dimension);
    }
    return shape;
}

// TypeProto.Tensor: elem_type + shape. 1 == TensorProto.DataType.FLOAT.
std::string makeFloatTensorType(std::initializer_list<std::int64_t> dims)
{
    std::string tensor;
    putVarintField(tensor, 1 /* elem_type */, 1);
    putBytesField(tensor, 2 /* shape */, makeTensorShape(dims));

    std::string typeProto;
    putBytesField(typeProto, 1 /* tensor_type */, tensor);
    return typeProto;
}

std::string makeValueInfo(std::string_view name, std::string_view typeProto)
{
    std::string valueInfo;
    putBytesField(valueInfo, 1 /* name */, name);
    putBytesField(valueInfo, 2 /* type */, typeProto);
    return valueInfo;
}

std::string buildProbeModel()
{
    // Graph: Y = Identity(X)
    std::string node;
    putBytesField(node, 1 /* input */, kProbeModelInputName);
    putBytesField(node, 2 /* output */, kProbeModelOutputName);
    putBytesField(node, 3 /* name */, "identity");
    putBytesField(node, 4 /* op_type */, "Identity");

    const std::string valueType = makeFloatTensorType({1, kProbeModelElementCount});

    std::string graph;
    putBytesField(graph, 1 /* node */, node);
    putBytesField(graph, 2 /* name */, "pf_probe");
    putBytesField(graph, 11 /* input */,
                  makeValueInfo(kProbeModelInputName, valueType));
    putBytesField(graph, 12 /* output */,
                  makeValueInfo(kProbeModelOutputName, valueType));

    // ir_version 7 and opset 13 are understood by every ONNX Runtime we support
    // (>= 1.12); there is no reason to demand a newer IR for an Identity.
    std::string opset;
    putVarintField(opset, 2 /* version */, 13);

    // onnx.proto: ModelProto { ir_version = 1, producer_name = 2, graph = 7,
    // opset_import = 8 }. opset_import is *not* field 2 — getting this wrong
    // makes ORT reject the model with "Missing opset in the model", which
    // would misreport every GPU provider as unusable.
    std::string model;
    putVarintField(model, 1 /* ir_version */, 7);
    putBytesField(model, 8 /* opset_import */, opset);
    putBytesField(model, 2 /* producer_name */, "parallel-finder");
    putBytesField(model, 7 /* graph */, graph);
    return model;
}

} // namespace

std::string_view probeModelBytes()
{
    static const std::string model = buildProbeModel();
    return model;
}

} // namespace pfgpu
