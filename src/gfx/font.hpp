#pragma once

#include "gfx/font/descriptor.hpp"
#include "gfx/texture_atlas.hpp"
#include "utils/region.hpp"
#include <expected>

#include <ft2build.h>
#include <freetype/freetype.h>

int FtInit();
void FtDone();

struct FT_FaceDeleter {
  void operator()(FT_Face face) {
    FT_Done_Face(face);
  }
};
using FT_FacePtr = std::unique_ptr<FT_FaceRec, FT_FaceDeleter>;

struct Font {
  FT_FacePtr face;

  std::string path;
  float height;     // font size
  int trueHeight; // font size * dpiScale
  float width;
  float dpiScale;

  glm::vec2 charSize;
  float ascender;
  float underlinePosition;
  float underlineThickness;

  struct GlyphInfo {
    // floats because of high dpi
    Region sizePositions;
    glm::vec2 bearing;
    // float advance;
    Region region;
  };
  using GlyphInfoMap = std::unordered_map<uint32_t, GlyphInfo>;
  GlyphInfoMap glyphInfoMap;

  static std::expected<Font, std::string>
  FromName(const FontDescriptorWithName& desc, float dpiScale);

  Font() = default;
  Font(std::string path, float height, float width, float dpiScale);

  // returns nullptr when charcode is not found.
  // updates glyphInfoMap when charcode not in map.
  const GlyphInfo* GetGlyphInfo(FT_ULong charcode, TextureAtlas& textureAtlas);
};
