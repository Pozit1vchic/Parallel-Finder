#pragma once

#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>

namespace pftest {

// A tiny Identity graph with a selected fixed shape. These component fixtures
// test metadata refresh through real ORT, not video/inference performance.
inline std::string identityModel(std::initializer_list<std::uint64_t> dimensions)
{
    const auto varint = [](std::string& out, std::uint64_t value) {
        while (value >= 128) {
            out.push_back(static_cast<char>((value & 127) | 128));
            value >>= 7;
        }
        out.push_back(static_cast<char>(value));
    };
    const auto integer = [&](std::string& out, unsigned field, std::uint64_t value) {
        varint(out, field << 3);
        varint(out, value);
    };
    const auto bytes = [&](std::string& out, unsigned field, std::string_view value) {
        varint(out, (field << 3) | 2);
        varint(out, value.size());
        out.append(value);
    };
    std::string shape;
    for (const auto value : dimensions) {
        std::string dimension;
        integer(dimension, 1, value);
        bytes(shape, 1, dimension);
    }
    std::string tensor, type;
    integer(tensor, 1, 1); // FLOAT
    bytes(tensor, 2, shape);
    bytes(type, 1, tensor);
    const auto valueInfo = [&](std::string_view name) {
        std::string result;
        bytes(result, 1, name);
        bytes(result, 2, type);
        return result;
    };
    std::string node, graph, opset, model;
    bytes(node, 1, "X");
    bytes(node, 2, "Y");
    bytes(node, 4, "Identity");
    bytes(graph, 1, node);
    bytes(graph, 2, "metadata_refresh");
    bytes(graph, 11, valueInfo("X"));
    bytes(graph, 12, valueInfo("Y"));
    integer(opset, 2, 13);
    integer(model, 1, 7);
    bytes(model, 7, graph);
    bytes(model, 8, opset);
    return model;
}

} // namespace pftest
