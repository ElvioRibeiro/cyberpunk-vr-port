#include "Render/FixedVrs.hpp"
#include "Render/DescriptorCache.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <iostream>
using namespace cvr::vrs;
static void Check(bool value){if(!value)throw std::runtime_error("VRS policy assertion failed");}
int main()try{
    Check(Settings{}.mode==0);
    auto s=Normalize({9,std::nanf(""),150,100,std::nanf("")});
    Check(s.mode==2 && s.width==70 && s.height==100 && s.offsetX==15 && s.offsetY==0);
    Check(BuildRates({},256,256,0,true).empty());Check(BuildRates({},256,256,12,true).empty());
    for(unsigned tile:{8u,16u,32u})for(unsigned w:{65u,1484u,2560u})for(unsigned h:{65u,1484u,2560u}) {
        for(auto test:{Settings{0},Settings{1,70,65,0,0},Settings{2,50,40,-25,30},Settings{2,100,100,0,0}}) {
            for(bool additional:{false,true}) {
                const auto map=BuildRates(test,w,h,tile,additional);const auto columns=(w+tile-1)/tile;
                Check(map.size()==size_t(columns)*((h+tile-1)/tile));
                for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x) {
                    const auto rate=map[size_t(y/tile)*columns+x/tile];
                    Check(rate==0 || rate==5 || (additional && rate==10));
                    const float dx=(float(x)+.5f)/w*100-50-test.offsetX,dy=(float(y)+.5f)/h*100-50-test.offsetY;
                    if(!test.mode || (std::abs(dx)<test.width*.5f && std::abs(dy)<test.height*.5f))Check(rate==0);
                }
            }
        }
    }
    // Exercise global tiles independently of eye-local coordinates, including a split inside a tile.
    for(unsigned tile:{8u,16u,32u})for(unsigned ew:{65u,260u,1484u})for(unsigned h:{65u,193u}) {
        for(auto policy:{Settings{0},Settings{1},Settings{2,40,30,25,-20},Settings{1,100,100}}){
            const auto map=BuildRates(policy,ew*2,h,tile,true,true);
            const auto normalized=Normalize(policy);const auto columns=(2*ew+tile-1)/tile;
            Check(map.size()==size_t(columns)*((h+tile-1)/tile));
            unsigned coarse[2]{};
            for(unsigned y=0;y<h;++y)for(unsigned x=0;x<ew*2;++x){
                const auto rate=map[size_t(y/tile)*columns+x/tile];Check(rate==0 || rate==5);
                const float dx=(float(x%ew)+.5f)/ew*100-50-normalized.offsetX;
                const float dy=(float(y)+.5f)/h*100-50-normalized.offsetY;
                if(!policy.mode || (std::abs(dx)<normalized.width*.5f && std::abs(dy)<normalized.height*.5f))Check(rate==0);
                if((x/tile)*tile<ew && (x/tile+1)*tile>ew)Check(rate==0);
                coarse[x/ew]+=rate!=0;
            }
            if(policy.mode && policy.width<100)Check(coarse[0] && coarse[1]);
        }
    }
    Check(BuildRates({1},521,193,16,false,true).empty());
    D3D12_VIEWPORT viewport{0,0,260,193,0,1};
    Check(StereoViewportEye(520,193,viewport)==0);viewport.TopLeftX=260;
    Check(StereoViewportEye(520,193,viewport)==1);
    for(auto invalid:{D3D12_VIEWPORT{260.5f,0,260,193,0,1},D3D12_VIEWPORT{0,1,260,193,0,1},
        D3D12_VIEWPORT{0,0,520,193,0,1},D3D12_VIEWPORT{0,0,260,192,0,1},
        D3D12_VIEWPORT{0,0,std::nanf(""),193,0,1},D3D12_VIEWPORT{0,0,260,193,.1f,1}})
        Check(StereoViewportEye(520,193,invalid)==-1);
    Check(StereoViewportEye(521,193,viewport)==-1);
    D3D12_RESOURCE_DESC target{};target.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    target.DepthOrArraySize=1;target.SampleDesc.Count=1;
    Check(StereoTargetCompatible(target,nullptr));
    D3D12_RENDER_TARGET_VIEW_DESC view{};view.ViewDimension=D3D12_RTV_DIMENSION_TEXTURE2D;
    Check(StereoTargetCompatible(target,&view));view.Texture2D.MipSlice=1;Check(!StereoTargetCompatible(target,&view));
    view.Texture2D.MipSlice=0;view.Texture2D.PlaneSlice=1;Check(!StereoTargetCompatible(target,&view));
    view={};view.ViewDimension=D3D12_RTV_DIMENSION_TEXTURE2DARRAY;Check(!StereoTargetCompatible(target,&view));
    target.SampleDesc.Count=4;Check(!StereoTargetCompatible(target,nullptr));
    target.SampleDesc.Count=1;target.DepthOrArraySize=2;Check(!StereoTargetCompatible(target,nullptr));
    cvr::render::DescriptorCache<2> descriptors;
    descriptors.Store(100,{1,520,193,true});descriptors.InvalidateView(100);
    Check(!descriptors.Read(100)->singleSample2DMipZero);
    descriptors.Store(100,{2,520,193,true});Check(descriptors.Read(100)->singleSample2DMipZero);
    Settings parsed;
    Check(ParseSetting("xr_vrs = 2",parsed));Check(ParseSetting("xr_vrs_width=80",parsed));
    Check(ParseSetting("xr_vrs_height=60",parsed));Check(ParseSetting("xr_vrs_offset_x=-5",parsed));Check(ParseSetting("xr_vrs_offset_y=10",parsed));
    Check(!ParseSetting("xr_vrs_extra=1",parsed));Check(parsed==Settings{2,80,60,-5,10});
    D3D12_GRAPHICS_PIPELINE_STATE_DESC d{};d.VS.BytecodeLength=d.PS.BytecodeLength=1;d.NumRenderTargets=3;d.SampleDesc.Count=1;d.DepthStencilState.DepthEnable=TRUE;
    Check(EligiblePipeline(d));d.BlendState.RenderTarget[0].BlendEnable=TRUE;Check(!EligiblePipeline(d));d.BlendState={};
    d.NumRenderTargets=1;Check(!EligiblePipeline(d));d.NumRenderTargets=4;d.SampleDesc.Count=4;Check(!EligiblePipeline(d));
    d.SampleDesc.Count=1;d.GS.BytecodeLength=1;Check(!EligiblePipeline(d));
    std::cout<<"PASS VRS center coverage, tile sizes, edge tiles, offsets, unsupported rates, defaults, parsing and opaque-pipeline gates\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
