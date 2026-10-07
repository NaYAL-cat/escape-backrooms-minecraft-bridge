// Runs the shipping ReShade effect against analytic geometry on an offscreen D3D11 device.
// No game, swap chain, window, synthetic input, or screenshots of user applications.
#include "effect_preprocessor.hpp"
#include "effect_parser.hpp"
#include "effect_codegen.hpp"
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <memory>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
using Microsoft::WRL::ComPtr;
void require(bool good,const std::string &why) { if(!good) throw std::runtime_error(why); }
void hrcheck(HRESULT hr,const char *why) { require(SUCCEEDED(hr),std::string(why)+" HRESULT="+std::to_string(hr)); }
struct Texture { ComPtr<ID3D11Texture2D> tex; ComPtr<ID3D11ShaderResourceView> srv; ComPtr<ID3D11UnorderedAccessView> uav; ComPtr<ID3D11RenderTargetView> rtv; int w=0,h=0; DXGI_FORMAT format; };
struct Program { ComPtr<ID3D11VertexShader> vs; ComPtr<ID3D11PixelShader> ps; ComPtr<ID3D11ComputeShader> cs; };
struct Vec { double x,y,z; Vec operator+(Vec b)const{return{x+b.x,y+b.y,z+b.z};} Vec operator-(Vec b)const{return{x-b.x,y-b.y,z-b.z};} Vec operator*(double v)const{return{x*v,y*v,z*v};} };
double dot(Vec a,Vec b){return a.x*b.x+a.y*b.y+a.z*b.z;}
struct Box { Vec lo,hi; unsigned colour; };
struct Hit { double t=1e9; int id=-1; Vec p; };
Hit trace(Vec origin,Vec ray,const std::vector<Box>& boxes) {
    Hit h;
    for(int j=0;j<int(boxes.size());j++) {
        double tnear=0,tfar=1e9;
        const auto &b=boxes[j];
        double o[3]={origin.x,origin.y,origin.z},d[3]={ray.x,ray.y,ray.z};
        double lo[3]={b.lo.x,b.lo.y,b.lo.z},hi[3]={b.hi.x,b.hi.y,b.hi.z};
        for(int axis=0;axis<3;axis++) {
            if(std::abs(d[axis])<1e-12) { if(o[axis]<lo[axis]||o[axis]>hi[axis]) tfar=-1; continue; }
            double a=(lo[axis]-o[axis])/d[axis],c=(hi[axis]-o[axis])/d[axis];
            tnear=std::max(tnear,std::min(a,c));tfar=std::min(tfar,std::max(a,c));
        }
        if(tnear>0 && tfar>=tnear && tnear<h.t) h={tnear,j,origin+ray*tnear};
    }
    return h;
}
float rawDepth(double z,double n,double f) { return z>1e8?0:float((n*f/z-n)/(f-n)); }
struct Runner {
    int w,h,sw,sh; bool measureGpu=false; double gpuMs=0;
    ComPtr<ID3D11Device> dev; ComPtr<ID3D11DeviceContext> ctx;
    std::unique_ptr<reshadefx::codegen> cg;
    std::map<std::string,Texture> textures;
    std::map<std::string,Program> programs;
    std::vector<ComPtr<ID3D11SamplerState>> samplers;
    std::vector<uint8_t> uniforms; ComPtr<ID3D11Buffer> cb;
    ComPtr<ID3D11RasterizerState> raster;
    ComPtr<ID3D11DepthStencilState> noDepth;
    Runner(int width,int height,int sourceWidth,int sourceHeight,bool hardware):w(width),h(height),sw(sourceWidth),sh(sourceHeight) {
        measureGpu=hardware; D3D_FEATURE_LEVEL level;
        hrcheck(D3D11CreateDevice(nullptr,hardware?D3D_DRIVER_TYPE_HARDWARE:D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&dev,&level,&ctx),"D3D11 device");
        reshadefx::preprocessor pp;
        pp.add_include_path("host/reshade/shaders");
        pp.add_macro_definition("__RESHADE__","60800");pp.add_macro_definition("__RENDERER__","0xb000");
        pp.add_macro_definition("BUFFER_WIDTH",std::to_string(w));pp.add_macro_definition("BUFFER_HEIGHT",std::to_string(h));
        pp.add_macro_definition("BUFFER_RCP_WIDTH","(1.0/"+std::to_string(w)+".0)");pp.add_macro_definition("BUFFER_RCP_HEIGHT","(1.0/"+std::to_string(h)+".0)");
        pp.add_macro_definition("BUFFER_COLOR_BIT_DEPTH","8");
        require(pp.append_file("host/reshade/shaders/ETBMinecraft.fx"),pp.errors());
        reshadefx::parser parser; cg.reset(reshadefx::create_codegen_hlsl(50,true,false));
        require(parser.parse(pp.output(),cg.get()),parser.errors());
        auto &m=cg->module();auto code=cg->finalize_code();
        for(auto &ep:m.entry_points) {
            ComPtr<ID3DBlob> blob,error;
            const char *profile=ep.second==reshadefx::shader_type::compute?"cs_5_0":ep.second==reshadefx::shader_type::vertex?"vs_5_0":"ps_5_0";
            HRESULT hr=D3DCompile(code.data(),code.size(),"ETBMinecraft.fx",nullptr,nullptr,ep.first.c_str(),profile,D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&blob,&error);
            if(FAILED(hr) && error) std::cerr<<static_cast<char*>(error->GetBufferPointer());
            hrcheck(hr,"D3DCompile");auto &p=programs[ep.first];
            if(ep.second==reshadefx::shader_type::compute)hrcheck(dev->CreateComputeShader(blob->GetBufferPointer(),blob->GetBufferSize(),nullptr,&p.cs),"CS");
            if(ep.second==reshadefx::shader_type::pixel)hrcheck(dev->CreatePixelShader(blob->GetBufferPointer(),blob->GetBufferSize(),nullptr,&p.ps),"PS");
            if(ep.second==reshadefx::shader_type::vertex)hrcheck(dev->CreateVertexShader(blob->GetBufferPointer(),blob->GetBufferSize(),nullptr,&p.vs),"VS");
        }
        for(auto &t:m.textures) {
            auto &v=textures[t.unique_name];bool guest=t.semantic.rfind("MC",0)==0;
            v.w=t.semantic.empty()?t.width:guest?sw:w;v.h=t.semantic.empty()?t.height:guest?sh:h;
            v.format=t.semantic.empty()?DXGI_FORMAT(t.format):(t.semantic=="DEPTH"||t.semantic=="MCDEPTH")?DXGI_FORMAT_R32_FLOAT:DXGI_FORMAT_R8G8B8A8_UNORM;
            D3D11_TEXTURE2D_DESC d={};d.Width=v.w;d.Height=v.h;d.ArraySize=1;d.MipLevels=1;d.Format=v.format;d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_DEFAULT;
            d.BindFlags=D3D11_BIND_SHADER_RESOURCE|(t.storage_access?D3D11_BIND_UNORDERED_ACCESS:0)|(t.render_target?D3D11_BIND_RENDER_TARGET:0);
            hrcheck(dev->CreateTexture2D(&d,nullptr,&v.tex),"texture");hrcheck(dev->CreateShaderResourceView(v.tex.Get(),nullptr,&v.srv),"SRV");
            if(t.storage_access)hrcheck(dev->CreateUnorderedAccessView(v.tex.Get(),nullptr,&v.uav),"UAV");
            if(t.render_target)hrcheck(dev->CreateRenderTargetView(v.tex.Get(),nullptr,&v.rtv),"RTV");
        }
        for(auto &s:m.samplers) {
            D3D11_SAMPLER_DESC d={};d.Filter=D3D11_FILTER(s.filter);d.AddressU=D3D11_TEXTURE_ADDRESS_MODE(s.address_u);d.AddressV=D3D11_TEXTURE_ADDRESS_MODE(s.address_v);d.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;d.MaxLOD=D3D11_FLOAT32_MAX;d.ComparisonFunc=D3D11_COMPARISON_ALWAYS;
            ComPtr<ID3D11SamplerState> state;hrcheck(dev->CreateSamplerState(&d,&state),"sampler");samplers.push_back(state);
        }
        uniforms.resize((m.total_uniform_size+15)&~15);
        for(auto &u:m.uniforms)if(u.has_initializer_value)std::memcpy(uniforms.data()+u.offset,u.initializer_value.as_uint,u.size);
        D3D11_BUFFER_DESC bd={};bd.ByteWidth=unsigned(uniforms.size());bd.Usage=D3D11_USAGE_DEFAULT;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        hrcheck(dev->CreateBuffer(&bd,nullptr,&cb),"constant buffer");
        D3D11_RASTERIZER_DESC rd={};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;rd.DepthClipEnable=true;
        hrcheck(dev->CreateRasterizerState(&rd,&raster),"rasterizer");
        D3D11_DEPTH_STENCIL_DESC dd={};dd.DepthFunc=D3D11_COMPARISON_ALWAYS;hrcheck(dev->CreateDepthStencilState(&dd,&noDepth),"no depth");
        set("McPlanes",std::array<float,3>{.05f,1024,7});set("HostPlanes",std::array<float,2>{.1f,20000});
        set("McActive",1);set("HostReversedZ",1);set("Reproject",1);set("CameraLocked",0);set("DepthBias",.04f);
    }
    template<class T>void set(const char *name,T value) {
        for(auto &u:cg->module().uniforms)if(u.name==name) { require(sizeof(T)<=u.size,"uniform size");std::memcpy(uniforms.data()+u.offset,&value,sizeof(T));return; }
        throw std::runtime_error(std::string("unknown uniform ")+name);
    }
    Texture &semantic(const char* name) {for(auto&t:cg->module().textures)if(t.semantic==name||t.name==name)return textures.at(t.unique_name);throw std::runtime_error(name);}
    void update(const char* name,const void* pixels) {auto&t=semantic(name);ctx->UpdateSubresource(t.tex.Get(),0,nullptr,pixels,t.w*4,0);}
    void draw() {
        ComPtr<ID3D11Query> timing,start,end;
        if(measureGpu) {
            D3D11_QUERY_DESC q={D3D11_QUERY_TIMESTAMP_DISJOINT,0};hrcheck(dev->CreateQuery(&q,&timing),"timing query");
            q.Query=D3D11_QUERY_TIMESTAMP;hrcheck(dev->CreateQuery(&q,&start),"start timestamp");hrcheck(dev->CreateQuery(&q,&end),"end timestamp");
            ctx->Begin(timing.Get());ctx->End(start.Get());
        }
        auto&m=cg->module();ctx->UpdateSubresource(cb.Get(),0,nullptr,uniforms.data(),0,0);
        ID3D11Buffer *c=cb.Get();ctx->CSSetConstantBuffers(0,1,&c);ctx->PSSetConstantBuffers(0,1,&c);ctx->VSSetConstantBuffers(0,1,&c);
        ctx->RSSetState(raster.Get());ctx->OMSetDepthStencilState(noDepth.Get(),0);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        for(auto &pass:m.techniques[0].passes) {
            // Test the real visibility and world composite. Lighting/bloom are disabled defaults.
            if(pass.name=="Light"||pass.name=="Bright"||pass.name=="Final")continue;
            ID3D11ShaderResourceView *empty[32]={};ID3D11UnorderedAccessView* emptyU[8]={};
            ctx->CSSetShaderResources(0,32,empty);ctx->PSSetShaderResources(0,32,empty);ctx->CSSetUnorderedAccessViews(0,8,emptyU,nullptr);ctx->OMSetRenderTargets(0,nullptr,nullptr);
            ID3D11ShaderResourceView* views[32]={};ID3D11SamplerState* states[32]={};ID3D11UnorderedAccessView* stores[8]={};
            for(auto &b:pass.texture_bindings) {auto&s=m.samplers[b.index];views[b.index]=textures.at(s.texture_name).srv.Get();}
            for(auto &b:pass.sampler_bindings)states[b.entry_point_binding]=samplers[b.index].Get();
            for(auto &b:pass.storage_bindings) {
                auto&st=m.storages[b.index];stores[b.index]=textures.at(st.texture_name).uav.Get();
                for(auto &read:pass.texture_bindings)require(m.samplers[read.index].texture_name!=st.texture_name,"SRV/UAV alias in "+pass.name);
            }
            if(!pass.cs_entry_point.empty()) {
                ctx->CSSetShaderResources(0,32,views);ctx->CSSetSamplers(0,unsigned(samplers.size()),states);ctx->CSSetUnorderedAccessViews(0,8,stores,nullptr);
                ctx->CSSetShader(programs.at(pass.cs_entry_point).cs.Get(),nullptr,0);
                ctx->Dispatch(pass.viewport_width,pass.viewport_height,1);
            } else {
                ID3D11RenderTargetView *targets[8]={};int n=0;
                for(auto &name:pass.render_target_names){if(name.empty())break;targets[n++]=textures.at(name).rtv.Get();}
                ctx->OMSetRenderTargets(n,targets,nullptr);D3D11_VIEWPORT vp={0,0,float(w),float(h),0,1};ctx->RSSetViewports(1,&vp);
                ctx->PSSetShaderResources(0,32,views);ctx->PSSetSamplers(0,unsigned(samplers.size()),states);
                ctx->VSSetShader(programs.at(pass.vs_entry_point).vs.Get(),nullptr,0);ctx->PSSetShader(programs.at(pass.ps_entry_point).ps.Get(),nullptr,0);
                ctx->Draw(pass.num_vertices,0);
            }
        }
        if(measureGpu){ctx->End(end.Get());ctx->End(timing.Get());}
        ctx->ClearState();
        if(measureGpu){
            D3D11_QUERY_DATA_TIMESTAMP_DISJOINT d={};UINT64 a=0,b=0;
            while(ctx->GetData(timing.Get(),&d,sizeof(d),0)==S_FALSE)Sleep(1);
            hrcheck(ctx->GetData(start.Get(),&a,sizeof(a),0),"timestamp read");hrcheck(ctx->GetData(end.Get(),&b,sizeof(b),0),"timestamp read");
            gpuMs=d.Disjoint?0:double(b-a)*1000.0/d.Frequency;
        }
    }
    std::vector<unsigned> pixels(const char* name="CompositeTex") {
        auto &t=semantic(name);D3D11_TEXTURE2D_DESC d; t.tex->GetDesc(&d);d.Usage=D3D11_USAGE_STAGING;d.BindFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> staging;hrcheck(dev->CreateTexture2D(&d,nullptr,&staging),"readback");ctx->CopyResource(staging.Get(),t.tex.Get());
        D3D11_MAPPED_SUBRESOURCE data;hrcheck(ctx->Map(staging.Get(),0,D3D11_MAP_READ,0,&data),"map");
        std::vector<unsigned> out(w*h);for(int y=0;y<h;y++)std::memcpy(out.data()+y*w,static_cast<char*>(data.pData)+y*data.RowPitch,w*4);ctx->Unmap(staging.Get(),0);return out;
    }
};

void test(Runner &r,const std::string &name,Vec translation,double yaw,double pitch,bool wall,bool empty=false) {
    const double tanFov=std::tan(35.0*3.141592653589793/180.0);
    double cy=std::cos(yaw),sy=std::sin(yaw),cp=std::cos(pitch),sp=std::sin(pitch);
    std::array<Vec,3> rows={Vec{cy,sy*sp,sy*cp},Vec{0,cp,-sp},Vec{-sy,cy*sp,cy*cp}};
    r.set("WarpTan",std::array<float,3>{float(tanFov),float(tanFov),float(r.sw)/r.sh});
    r.set("WarpT",std::array<float,3>{float(translation.x),float(translation.y),float(translation.z)});
    const char* rn[]={"WarpRow0","WarpRow1","WarpRow2"};
    for(int j=0;j<3;j++)r.set(rn[j],std::array<float,3>{float(rows[j].x),float(rows[j].y),float(rows[j].z)});
    // Solid cube plus a five-part articulated silhouette, and a farther partly occluded block.
    std::vector<Box> boxes={
        {{-.8,-.5,-2.5},{-.15,.15,-1.9},0xff2020e0u},
        {{.18,-.18,-2.6},{.48,.38,-2.4},0xff30dc30u},
        {{.17,.38,-2.59},{.49,.70,-2.39},0xff30dc30u},
        {{.18,-.62,-2.58},{.28,-.18,-2.42},0xff30dc30u},
        {{.38,-.62,-2.58},{.48,-.18,-2.42},0xff30dc30u},
        {{.0,.03,-2.58},{.18,.37,-2.42},0xff30dc30u},
        {{.48,.03,-2.58},{.66,.37,-2.42},0xff30dc30u},
        {{-.5,-.35,-4.0},{.4,.62,-3.7},0xffe09030u}};
    if(name=="dense foreground")boxes={{{-100,-100,-3.1},{100,100,-3},0xff2020e0u}};
    if(empty)boxes.clear();
    const unsigned background=0xff282828;
    std::vector<unsigned> colour(r.sw*r.sh),host(r.w*r.h,background),overlay(r.sw*r.sh,0);
    std::vector<float> depth(r.sw*r.sh),hostDepth(r.w*r.h);
    for(int y=0;y<r.sh;y++)for(int x=0;x<r.sw;x++) {
        Vec ray={(2*(x+.5)/r.sw-1)*tanFov*r.sw/r.sh,(2*(y+.5)/r.sh-1)*tanFov,-1};
        auto hit=trace({0,0,0},ray,boxes);int i=y*r.sw+x;
        if(hit.id>=0){colour[i]=boxes[hit.id].colour;depth[i]=rawDepth(-hit.p.z,.05,1024);}
    }
    std::vector<unsigned> expected(r.w*r.h,background);std::vector<bool> captured(r.w*r.h,true);
    for(int y=0;y<r.h;y++)for(int x=0;x<r.w;x++) {
        int i=y*r.w+x;double zh=wall && x>r.w/2?1.2:1e9;
        hostDepth[i]=rawDepth(zh,.1,20000);
        Vec ray={(2*(x+.5)/r.w-1)*tanFov*r.w/r.h,(1-2*(y+.5)/r.h)*tanFov,-1};
        Vec direction={dot(rows[0],ray),dot(rows[1],ray),dot(rows[2],ray)};
        auto hit=trace(translation,direction,boxes);
        if(hit.id>=0 && hit.t<zh+.04) {
            expected[i]=boxes[hit.id].colour;
            auto original=trace({0,0,0},hit.p,boxes);
            captured[i]=original.id==hit.id && original.t>.9999 && hit.p.z<-.05 &&
                std::abs(hit.p.x/-hit.p.z)<tanFov*r.sw/r.sh && std::abs(hit.p.y/-hit.p.z)<tanFov;
        }
    }
    r.update("MCWORLD",colour.data());r.update("MCDEPTH",depth.data());r.update("MCOVERLAY",overlay.data());
    r.update("COLOR",host.data());r.update("DEPTH",hostDepth.data());r.draw();auto actual=r.pixels();
    auto projectedDepth=r.pixels("WarpDepthTex"),projectedSource=r.pixels("WarpSourceTex");
    int orphaned=0;for(int i=0;i<r.w*r.h;i++)if(projectedDepth[i]!=0xffffffffu && projectedSource[i]==0xffffffffu)orphaned++;
    std::cout<<"  depth without colour: "<<orphaned<<"\n";
    require(orphaned==0,"depth/source visibility pairing failed");
    int missing=0,core=0,ghost=0,backgroundCore=0,wallLeak=0,holes=0;
    for(int y=2;y<r.h-2;y++)for(int x=2;x<r.w-2;x++) {
        int i=y*r.w+x;bool interior=captured[i];
        for(int dy=-2;dy<=2;dy++)for(int dx=-2;dx<=2;dx++)if(expected[i]!=expected[(y+dy)*r.w+x+dx])interior=false;
        if(!interior)continue;
        bool correct=(actual[i]&0xffffff)==(expected[i]&0xffffff);
        if(expected[i]==background){backgroundCore++;if(!correct)ghost++;}
        else{core++;if(!correct){missing++;if((actual[i]&0xffffff)==(background&0xffffff))holes++;}}
        if(wall && x>r.w/2+2 && !correct)wallLeak++;
    }
    std::cout<<name<<" "<<r.w<<"x"<<r.h<<" source "<<r.sw<<"x"<<r.sh<<": missing "<<missing<<"/"<<core<<" (empty "<<holes<<"), ghosts "<<ghost<<"/"<<backgroundCore<<", wall leaks "<<wallLeak;if(r.measureGpu)std::cout<<", GPU "<<r.gpuMs<<" ms";std::cout<<"\n";
    require(core>100 || empty,"oracle scene must contain visible geometry");
    require(missing<=std::max(2,core/100),"more than 1% of captured interior is missing in "+name);
    require(ghost==0,"unexpected separated silhouettes/history in "+name);
    require(wallLeak==0,"guest is showing through the host wall");
}
int main(int argc,char **argv) {
    try {
        bool hardware=argc>1 && std::string(argv[1])=="--hardware";
        int w=hardware?1920:320,h=hardware?1200:200;
        Runner r(w,h,w,h,hardware);
        test(r,"identity",{0,0,0},0,0,false);
        test(r,"left strafe / initially empty background",{-.30,0,0},0,0,false);
        test(r,"right strafe",{.30,0,0},0,0,false);
        test(r,"approach",{0,0,-.30},0,0,false);
        test(r,"yaw",{0,0,0},.19,0,false);
        test(r,"pitch",{0,0,0},0,.12,false);
        test(r,"translation and rotation",{-.25,.1,-.15},-.12,.06,false);
        test(r,"host depth occlusion",{-.25,0,0},.07,0,true);
        test(r,"dense foreground",{-.25,0,0},.07,0,false);
        test(r,"empty frame clears old silhouettes",{-.25,0,0},.07,0,false,true);
        if(!hardware){
            Runner scaled(320,200,304,190,false);test(scaled,"16:10 resized guest",{-.25,0,0},.1,.05,true);
            Runner wide(320,180,320,180,false);test(wide,"16:9",{-.25,0,0},.1,.05,true);
        }
        std::cout<<"PASS actual shipping shader on "<<(hardware?"hardware":"D3D11 WARP")<<"; no game launched.\n";
        return 0;
    }catch(const std::exception &e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}
}
