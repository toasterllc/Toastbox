#pragma once

namespace Toastbox::Endian {

constexpr int16_t Swap(int16_t x) {
    return  (x&0xFF00)>>8   |
            (x&0x00FF)<< 8  ;
}

constexpr uint16_t Swap(uint16_t x) {
    return  (x&0xFF00)>>8   |
            (x&0x00FF)<< 8  ;
}

constexpr int32_t Swap(int32_t x) {
    return  (x&0xFF000000)>>24  |
            (x&0x00FF0000)>> 8  |
            (x&0x0000FF00)<< 8  |
            (x&0x000000FF)<<24  ;
}

constexpr uint32_t Swap(uint32_t x) {
    return  (x&0xFF000000)>>24  |
            (x&0x00FF0000)>> 8  |
            (x&0x0000FF00)<< 8  |
            (x&0x000000FF)<<24  ;
}

constexpr bool LittleEndian() {
    return __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__;
}

// Little <-> Host

constexpr uint8_t   LFH(uint8_t x)      { return x; }
constexpr int8_t    LFH(int8_t x)       { return x; }
constexpr uint8_t   HFL(uint8_t x)      { return x; }
constexpr int8_t    HFL(int8_t x)       { return x; }
constexpr uint16_t  LFH(uint16_t x)     { if (LittleEndian()) return x; else return Swap(x); }
constexpr int16_t   LFH(int16_t x)      { if (LittleEndian()) return x; else return Swap(x); }
constexpr uint16_t  HFL(uint16_t x)     { if (LittleEndian()) return x; else return Swap(x); }
constexpr int16_t   HFL(int16_t x)      { if (LittleEndian()) return x; else return Swap(x); }
constexpr uint32_t  LFH(uint32_t x)     { if (LittleEndian()) return x; else return Swap(x); }
constexpr int32_t   LFH(int32_t x)      { if (LittleEndian()) return x; else return Swap(x); }
constexpr uint32_t  HFL(uint32_t x)     { if (LittleEndian()) return x; else return Swap(x); }
constexpr int32_t   HFL(int32_t x)      { if (LittleEndian()) return x; else return Swap(x); }

constexpr uint8_t   LFH_U8(uint8_t x)   { return LFH(x); }
constexpr int8_t    LFH_S8(int8_t x)    { return LFH(x); }
constexpr uint8_t   HFL_U8(uint8_t x)   { return HFL(x); }
constexpr int8_t    HFL_S8(int8_t x)    { return HFL(x); }
constexpr uint16_t  LFH_U16(uint16_t x) { return LFH(x); }
constexpr int16_t   LFH_S16(int16_t x)  { return LFH(x); }
constexpr uint16_t  HFL_U16(uint16_t x) { return HFL(x); }
constexpr int16_t   HFL_S16(int16_t x)  { return HFL(x); }
constexpr uint32_t  LFH_U32(uint32_t x) { return LFH(x); }
constexpr int32_t   LFH_S32(int32_t x)  { return LFH(x); }
constexpr uint32_t  HFL_U32(uint32_t x) { return HFL(x); }
constexpr int32_t   HFL_S32(int32_t x)  { return HFL(x); }

// Big <-> Host

constexpr uint8_t   BFH(uint8_t x)      { return x; }
constexpr int8_t    BFH(int8_t x)       { return x; }
constexpr uint8_t   HFB(uint8_t x)      { return x; }
constexpr int8_t    HFB(int8_t x)       { return x; }
constexpr uint16_t  BFH(uint16_t x)     { if (!LittleEndian()) return x; else return Swap(x); }
constexpr int16_t   BFH(int16_t x)      { if (!LittleEndian()) return x; else return Swap(x); }
constexpr uint16_t  HFB(uint16_t x)     { if (!LittleEndian()) return x; else return Swap(x); }
constexpr int16_t   HFB(int16_t x)      { if (!LittleEndian()) return x; else return Swap(x); }
constexpr uint32_t  BFH(uint32_t x)     { if (!LittleEndian()) return x; else return Swap(x); }
constexpr int32_t   BFH(int32_t x)      { if (!LittleEndian()) return x; else return Swap(x); }
constexpr uint32_t  HFB(uint32_t x)     { if (!LittleEndian()) return x; else return Swap(x); }
constexpr int32_t   HFB(int32_t x)      { if (!LittleEndian()) return x; else return Swap(x); }

constexpr uint8_t   BFH_U8(uint8_t x)   { return BFH(x); }
constexpr int8_t    BFH_S8(int8_t x)    { return BFH(x); }
constexpr uint8_t   HFB_U8(uint8_t x)   { return HFB(x); }
constexpr int8_t    HFB_S8(int8_t x)    { return HFB(x); }
constexpr uint16_t  BFH_U16(uint16_t x) { return BFH(x); }
constexpr int16_t   BFH_S16(int16_t x)  { return BFH(x); }
constexpr uint16_t  HFB_U16(uint16_t x) { return HFB(x); }
constexpr int16_t   HFB_S16(int16_t x)  { return HFB(x); }
constexpr uint32_t  BFH_U32(uint32_t x) { return BFH(x); }
constexpr int32_t   BFH_S32(int32_t x)  { return BFH(x); }
constexpr uint32_t  HFB_U32(uint32_t x) { return HFB(x); }
constexpr int32_t   HFB_S32(int32_t x)  { return HFB(x); }

} // namespace Toastbox::Endian
