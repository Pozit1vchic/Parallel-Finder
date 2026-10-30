#pragma once
#include <string>
#include <string_view>
#include <vector>
#include <cstdint>
#include <initializer_list>

namespace pftest {
// A deterministic constant pose-output graph for workflow tests. Its people
// are synthetic; this fixture makes no claim about recognition accuracy.
inline std::string twoPeopleModel()
{
    const auto varint=[](std::string& out,std::uint64_t v) {
        while(v>=128) {out.push_back(char((v&127)|128));v>>=7;} out.push_back(char(v));
    };
    const auto integer=[&](std::string& out,unsigned field,std::uint64_t v) {varint(out,field<<3);varint(out,v);};
    const auto bytes=[&](std::string& out,unsigned field,std::string_view v) {
        varint(out,(field<<3)|2);varint(out,v.size());out.append(v);
    };
    const auto info=[&](std::string_view name,std::initializer_list<std::uint64_t> dims) {
        std::string shape,tensor,type,result;
        for(auto d:dims) {std::string dim;integer(dim,1,d);bytes(shape,1,dim);}
        integer(tensor,1,1);bytes(tensor,2,shape);bytes(type,1,tensor);
        bytes(result,1,name);bytes(result,2,type);return result;
    };
    std::vector<float> values(2*57,0);
    for(std::size_t i=0;i<2;++i) {
        const float x=40+i*320;
        values[i*57]=x;values[i*57+1]=160;values[i*57+2]=x+240;values[i*57+3]=480;
        values[i*57+4]=.95f;
        for(std::size_t j=0;j<17;++j) {
            values[i*57+6+j*3]=x+60+(j%2)*100;
            values[i*57+7+j*3]=180+j*16.0f;
            values[i*57+8+j*3]=.95f;
        }
    }
    std::string initializer,node,graph,opset,model;
    for(auto d:{1,2,57}) integer(initializer,1,d);
    integer(initializer,2,1);bytes(initializer,8,"poses");
    bytes(initializer,9,std::string_view(reinterpret_cast<const char*>(values.data()),values.size()*sizeof(float)));
    bytes(node,1,"poses");bytes(node,2,"Y");bytes(node,4,"Identity");
    bytes(graph,1,node);bytes(graph,2,"two_people_workflow");bytes(graph,5,initializer);
    bytes(graph,11,info("X",{1,3,640,640}));bytes(graph,12,info("Y",{1,2,57}));
    integer(opset,2,13);integer(model,1,7);bytes(model,7,graph);bytes(model,8,opset);
    return model;
}
}
