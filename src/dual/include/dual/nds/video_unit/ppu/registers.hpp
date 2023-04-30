
#include <atom/bit.hpp>
#include <atom/integer.hpp>

namespace dual::nds {

struct DisplayControl {
  enum class Mapping {
    TwoDimensional = 0,
    OneDimensional = 1
  };

  int  bg_mode = 0;
  bool enable_bg0_3d = false;
  bool forced_blank = false;
  bool enable[8] {false};
  int  display_mode = 0;
  int  vram_block = 0;
  bool hblank_oam_update = false;
  int  tile_block = 0;
  int  map_block = 0;
  bool enable_extpal_bg = false;
  bool enable_extpal_obj = false;

  struct {
    Mapping mapping = Mapping::TwoDimensional;
    int boundary = 0;
  } tile_obj;

  struct {
    Mapping mapping = Mapping::TwoDimensional;
    int dimension = 0;
    int boundary = 0;
  } bitmap_obj;

  DisplayControl(u32 mask = 0xFFFFFFFF) : mask(mask) {}

  void Reset();
  auto ReadByte (uint offset) -> u8;
  void WriteByte(uint offset, u8 value);

  u32 ReadWord() {
    return ReadByte(0) <<  0 |
           ReadByte(1) <<  8 |
           ReadByte(2) << 16 |
           ReadByte(3) << 24;
  }

  void WriteWord(u32 value, u32 mask) {
    if(mask & 0x000000FFu) WriteByte(0, value >>  0);
    if(mask & 0x0000FF00u) WriteByte(1, value >>  8);
    if(mask & 0x00FF0000u) WriteByte(2, value >> 16);
    if(mask & 0xFF000000u) WriteByte(3, value >> 24);
  }

private:
  u32 mask;
};

struct BackgroundControl {
  union {
    atom::Bits< 0, 2, u16> priority;
    atom::Bits< 2, 4, u16> tile_block;
    atom::Bits< 6, 1, u16> enable_mosaic;
    atom::Bits< 7, 1, u16> full_palette;
    atom::Bits< 8, 5, u16> map_block;
    atom::Bits<13, 1, u16> palette_slot; // BG0-1
    atom::Bits<13, 1, u16> wraparound;   // BG2-3
    atom::Bits<14, 2, u16> size;

    u16 half{};
  };

  void Reset() {
    half = 0u;
  }

  u16 ReadHalf() {
    return half;
  }

  void WriteHalf(u16 value, u16 mask) {
    half = (value & mask) | (half & ~mask);
  }
};

struct BackgroundOffset {
  u16 half;

  void Reset() {
    half = 0u;
  }

  u16 ReadHalf() {
    return half;
  }

  void WriteHalf(u16 value, u16 mask) {
    const u16 write_mask = 0x01FFu & mask;

    half = (value & write_mask) | (half & ~write_mask);
  }
};

struct ReferencePoint {
  s32 initial;
  s32 _current;
  
  void Reset();
  void WriteByte(uint offset, u8 value);

  void WriteWord(u32 value, u32 mask) {
    if(mask & 0x000000FFu) WriteByte(0, value >>  0);
    if(mask & 0x0000FF00u) WriteByte(1, value >>  8);
    if(mask & 0x00FF0000u) WriteByte(2, value >> 16);
    if(mask & 0xFF000000u) WriteByte(3, value >> 24);
  }
};

struct RotateScaleParameter {
  s16 value;

  void Reset();
  void WriteByte(uint offset, u8 value);

  void WriteHalf(u16 value, u16 mask) {
    if(mask & 0x00FFu) WriteByte(0, value >> 0);
    if(mask & 0xFF00u) WriteByte(1, value >> 8);
  }
};

struct WindowRange {
  u8 min;
  u8 max;
  bool _changed;

  void Reset();
  void WriteByte(uint offset, u8 value);

  void WriteHalf(u16 value, u16 mask) {
    if(mask & 0x00FFu) WriteByte(0, value >> 0);
    if(mask & 0xFF00u) WriteByte(1, value >> 8);
  }
};

struct WindowLayerSelect {
  bool enable[2][6];

  void Reset();
  auto ReadByte(uint offset) -> u8;
  void WriteByte(uint offset, u8 value);

  u16 ReadHalf() {
    return ReadByte(0) << 0 | ReadByte(1) <<  8;
  }

  void WriteHalf(u16 value, u16 mask) {
    if(mask & 0x00FFu) WriteByte(0, value >> 0);
    if(mask & 0xFF00u) WriteByte(1, value >> 8);
  }
};

struct BlendControl {
  enum Effect {
    SFX_NONE,
    SFX_BLEND,
    SFX_BRIGHTEN,
    SFX_DARKEN
  } sfx;
  
  bool targets[2][6];
  u16 hword;

  void Reset();
  auto ReadByte(uint offset) -> u8;
  void WriteByte(uint offset, u8 value);

  u16 ReadHalf() {
    return ReadByte(0) << 0 | ReadByte(1) <<  8;
  }

  void WriteHalf(u16 value, u16 mask) {
    if(mask & 0x00FFu) WriteByte(0, value >> 0);
    if(mask & 0xFF00u) WriteByte(1, value >> 8);
  }
};

struct BlendAlpha {
  int a;
  int b;

  void Reset();
  auto ReadByte(uint offset) -> u8;
  void WriteByte(uint offset, u8 value);

  u16 ReadHalf() {
    return ReadByte(0) << 0 | ReadByte(1) <<  8;
  }

  void WriteHalf(u16 value, u16 mask) {
    if(mask & 0x00FFu) WriteByte(0, value >> 0);
    if(mask & 0xFF00u) WriteByte(1, value >> 8);
  }
};

struct BlendBrightness {
  int y;

  void Reset();
  void WriteByte(uint offset, u8 value);

  void WriteHalf(u16 value, u16 mask) {
    if(mask & 0x00FFu) WriteByte(0, value >> 0);
    if(mask & 0xFF00u) WriteByte(1, value >> 8);
  }
};

struct Mosaic {
  struct {
    int size_x;
    int size_y;
    int _counter_y;
  } bg, obj;
  
  void Reset();
  void WriteByte(uint offset, u8 value);

  void WriteHalf(u16 value, u16 mask) {
    if(mask & 0x00FFu) WriteByte(0, value >> 0);
    if(mask & 0xFF00u) WriteByte(1, value >> 8);
  }
};

struct MasterBrightness {
  enum class Mode {
    Disable = 0,
    Up = 1,
    Down = 2,
    Reserved = 3
  } mode;

  int factor;

  void Reset();
  auto ReadByte(uint offset) -> u8;
  void WriteByte(uint offset, u8 value);

  u16 ReadHalf() {
    return ReadByte(0) << 0 | ReadByte(1) <<  8;
  }

  void WriteHalf(u16 value, u16 mask) {
    if(mask & 0x00FFu) WriteByte(0, value >> 0);
    if(mask & 0xFF00u) WriteByte(1, value >> 8);
  }
};

} // namespace dual::nds
