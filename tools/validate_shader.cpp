#include "effect_preprocessor.hpp"
#include "effect_parser.hpp"
#include "effect_codegen.hpp"
#include <windows.h>
#include <d3dcompiler.h>
#include <iostream>
#include <memory>
int main(int argc,char **argv) {
    if(argc!=2) return 2;
    reshadefx::preprocessor pp;
    pp.add_include_path(std::filesystem::path(argv[1]).parent_path());
    pp.add_macro_definition("__RESHADE__","60800"); pp.add_macro_definition("__RENDERER__","0xb000");
    pp.add_macro_definition("BUFFER_WIDTH","1920");pp.add_macro_definition("BUFFER_HEIGHT","1080");
    pp.add_macro_definition("BUFFER_RCP_WIDTH","(1.0/1920.0)");pp.add_macro_definition("BUFFER_RCP_HEIGHT","(1.0/1080.0)");
    pp.add_macro_definition("BUFFER_COLOR_BIT_DEPTH","8");
    if(!pp.append_file(argv[1])) { std::cerr<<pp.errors(); return 1; }
    reshadefx::parser parser;
    std::unique_ptr<reshadefx::codegen> cg(reshadefx::create_codegen_hlsl(50,true,false));
    if(!parser.parse(pp.output(),cg.get())) { std::cerr<<parser.errors(); return 1; }
    const std::string source=cg->finalize_code();
    for(auto &entry:cg->module().entry_points) {
        ID3DBlob *code=nullptr,*errors=nullptr;
        const char *profile=entry.second==reshadefx::shader_type::vertex?"vs_5_0":entry.second==reshadefx::shader_type::compute?"cs_5_0":"ps_5_0";
        HRESULT hr=D3DCompile(source.data(),source.size(),argv[1],nullptr,nullptr,entry.first.c_str(),profile,
            D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&errors);
        if(errors) { std::cerr<<static_cast<const char*>(errors->GetBufferPointer()); errors->Release(); }
        if(code)code->Release();
        if(FAILED(hr)) return 1;
        std::cout<<"PASS "<<entry.first<<" "<<profile<<"\n";
    }
    return 0;
}
