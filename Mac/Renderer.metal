#import <metal_stdlib>
#import "MetalUtil.h"
using namespace metal;
using namespace Toastbox::MetalUtil;

namespace Toastbox {
namespace RendererShader {

vertex VertexOutput VertexShader(uint vidx [[vertex_id]]) {
    return Toastbox::MetalUtil::VertexShader(vidx);
}

fragment float4 FragmentShader(
    texture2d<float> txt [[texture(0)]],
    VertexOutput in [[stage_in]]
) {
    return Toastbox::MetalUtil::FragmentShader(txt, in);
}

template<typename T>
float LoadFloat(uint32_t w, uint32_t h, uint32_t samplesPerPixel, uint32_t maxValue, constant T* data, int2 pos) {
    return (float)data[samplesPerPixel*(w*pos.y + pos.x)] / maxValue;
}

template<typename T>
float4 LoadFloat4(uint32_t w, uint32_t h, uint32_t samplesPerPixel, uint32_t maxValue, constant T* data, int2 pos) {
    return float4(
        (float)data[samplesPerPixel*(w*pos.y + pos.x) + 0] / maxValue,
        (float)data[samplesPerPixel*(w*pos.y + pos.x) + 1] / maxValue,
        (float)data[samplesPerPixel*(w*pos.y + pos.x) + 2] / maxValue,
        1
    );
}

fragment float LoadFloatFromU8(
    constant uint32_t& w [[buffer(0)]],
    constant uint32_t& h [[buffer(1)]],
    constant uint32_t& samplesPerPixel [[buffer(2)]],
    constant uint32_t& maxValue [[buffer(3)]],
    constant uint8_t* data [[buffer(4)]],
    VertexOutput in [[stage_in]]
) {
    return LoadFloat(w, h, samplesPerPixel, maxValue, data, int2(in.pos.xy));
}

fragment float LoadFloatFromU16(
    constant uint32_t& w [[buffer(0)]],
    constant uint32_t& h [[buffer(1)]],
    constant uint32_t& samplesPerPixel [[buffer(2)]],
    constant uint32_t& maxValue [[buffer(3)]],
    constant uint16_t* data [[buffer(4)]],
    VertexOutput in [[stage_in]]
) {
    return LoadFloat(w, h, samplesPerPixel, maxValue, data, int2(in.pos.xy));
}

fragment float4 LoadFloat4FromU8(
    constant uint32_t& w [[buffer(0)]],
    constant uint32_t& h [[buffer(1)]],
    constant uint32_t& samplesPerPixel [[buffer(2)]],
    constant uint32_t& maxValue [[buffer(3)]],
    constant uint8_t* data [[buffer(4)]],
    VertexOutput in [[stage_in]]
) {
    return LoadFloat4(w, h, samplesPerPixel, maxValue, data, int2(in.pos.xy));
}

fragment float4 LoadFloat4FromU16(
    constant uint32_t& w [[buffer(0)]],
    constant uint32_t& h [[buffer(1)]],
    constant uint32_t& samplesPerPixel [[buffer(2)]],
    constant uint32_t& maxValue [[buffer(3)]],
    constant uint16_t* data [[buffer(4)]],
    VertexOutput in [[stage_in]]
) {
    return LoadFloat4(w, h, samplesPerPixel, maxValue, data, int2(in.pos.xy));
}

float SRGBGammaForward(float x) {
    // From http://www.brucelindbloom.com/index.html?Eqn_RGB_XYZ_Matrix.html
    if (x <= 0.0031308) return 12.92*x;
    return 1.055*pow(x, 1/2.4)-.055;
}

float SRGBGammaReverse(float x) {
    // From http://www.brucelindbloom.com/index.html?Eqn_RGB_XYZ_Matrix.html
    if (x <= 0.04045) return x/12.92;
    return pow((x+.055)/1.055, 2.4);
}

fragment float4 SRGBGamma(
    texture2d<float> txt [[texture(0)]],
    VertexOutput in [[stage_in]]
) {
    const float3 c_LSRGB = Sample::RGB(txt, int2(in.pos.xy));
    const float3 c_SRGB = float3{
        SRGBGammaForward(c_LSRGB.r),
        SRGBGammaForward(c_LSRGB.g),
        SRGBGammaForward(c_LSRGB.b)
    };
    return float4(c_SRGB, 1);
}

fragment float4 PremulAlpha(
    texture2d<float> txt [[texture(0)]],
    VertexOutput in [[stage_in]]
) {
    const int2 pos = int2(in.pos.xy);
    float4 s = Sample::RGBA(txt, pos);
    return float4(s.rgb*s.a, s.a);
}

fragment float Copy1To1(
    texture2d<float> txt [[texture(0)]],
    VertexOutput in [[stage_in]]
) {
    return Sample::R(txt, int2(in.pos.xy));
}

fragment float4 Copy1To4(
    texture2d<float> txt [[texture(0)]],
    VertexOutput in [[stage_in]]
) {
    const float c = Sample::R(txt, int2(in.pos.xy));
    return float4(c,c,c,1);
}

fragment float4 Copy4To4(
    texture2d<float> txt [[texture(0)]],
    VertexOutput in [[stage_in]]
) {
    return Sample::RGBA(txt, int2(in.pos.xy));
}

} // namespace RendererShader
} // namespace Toastbox
