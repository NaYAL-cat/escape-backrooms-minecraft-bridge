#pragma once
#include <cstdint>
namespace bridge_upload {
struct Layout { uint32_t row_bytes, row_pitch; uint64_t layer_stride, total_bytes; };
constexpr Layout layout(uint32_t width, uint32_t height) {
    const uint32_t bytes=width*4, pitch=(bytes+255u)&~255u;
    const uint64_t layer=(uint64_t(pitch)*height+511u)&~uint64_t(511u);
    return {bytes,pitch,layer,layer*3};
}
constexpr bool reusable(uint64_t retire, uint64_t completed) { return retire==0 || retire<=completed; }
}
