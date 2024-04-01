#include "font.hpp"
#include "glm/common.hpp"
#include "utils/logger.hpp"
#include "utils/region.hpp"
#include "webgpu_utils/webgpu.hpp"
#include "gfx/instance.hpp"

#include <vector>

using namespace wgpu;

static FT_Library library;
static bool ftInitialized = false;

static FT_Face nerdFace;

Font::Font(const std::string& path, int _size, float _dpiScale)
    : size(_size), dpiScale(_dpiScale) {
  if (!ftInitialized) {
    if (FT_Init_FreeType(&library)) {
      throw std::runtime_error("Failed to initialize FreeType library");
    }
    ftInitialized = true;

    std::string nerdFontPath(
      ROOT_DIR "/res/Hack/HackNerdFont-Regular.ttf"
      // ROOT_DIR "/res/Hack/HackNerdFontMono-Regular.ttf"
      // ROOT_DIR "/res/NerdFontsSymbolsOnly/SymbolsNerdFont-Regular.ttf"
      // ROOT_DIR "/res/NerdFontsSymbolsOnly/SymbolsNerdFontMono-Regular.ttf"
    );
    if (FT_New_Face(library, nerdFontPath.c_str(), 0, &nerdFace)) {
      throw std::runtime_error("Failed to load nerd font");
    }
  }

  if (FT_New_Face(library, path.c_str(), 0, &face)) {
    throw std::runtime_error("Failed to load font");
  }

  // winding order is clockwise starting from top left
  trueSize = size * dpiScale;
  FT_Set_Pixel_Sizes(face, 0, trueSize);

  charSize.x = (face->size->metrics.max_advance >> 6) / dpiScale;
  charSize.y = (face->size->metrics.height >> 6) / dpiScale;

  texCharSize = glm::max(charSize, glm::vec2(size, size)) + glm::vec2(1, 1);

  positions = MakeRegion({0, 0}, texCharSize);

  uint32_t numChars = 128;

  // start off by rendering the first 128 characters
  for (uint32_t i = 0; i < numChars; i++) {
    GetGlyphInfoOrAdd(i);
  }

  UpdateTexture();
}

const Font::GlyphInfo& Font::GetGlyphInfoOrAdd(FT_ULong charcode) {
  auto glyphIndex = FT_Get_Char_Index(face, charcode);

  FT_Face currFace;
  GlyphInfoMap* currMap;
  float vertOffset = 0;
  bool isNerd = false;

  if (glyphIndex != 0) {
    auto it = glyphInfoMap.find(glyphIndex);
    if (it != glyphInfoMap.end()) {
      return it->second;
    }

    currFace = face;
    currMap = &glyphInfoMap;

  } else {
    // is nerdfont
    glyphIndex = FT_Get_Char_Index(nerdFace, charcode);
    auto it = nerdGlyphInfoMap.find(glyphIndex);
    if (it != nerdGlyphInfoMap.end()) {
      return it->second;
    }

    currFace = nerdFace;
    currMap = &nerdGlyphInfoMap;

    FT_Set_Pixel_Sizes(nerdFace, 0, trueSize);

    // isNerd = true;
    // TODO: make nerdfont vertical offset an option
    // vertOffset = charSize.y / 12;
  }
  // if (charcode >= 0x2500 && charcode <= 0x25FF) {
  //   vertOffset = charSize.y * 0.2;
  //   vertOffset = floor(vertOffset * dpiScale) / dpiScale;
  // }

  dirty = true;

  auto numGlyphs = glyphInfoMap.size() + nerdGlyphInfoMap.size() + 1;
  atlasHeight = (numGlyphs + atlasWidth - 1) / atlasWidth;

  textureSize = {texCharSize.x * atlasWidth, texCharSize.y * atlasHeight};
  bufferSize = textureSize * dpiScale;

  textureData.resize(bufferSize.x * bufferSize.y, {0, 0, 0, 0});

  auto index = numGlyphs - 1;

  FT_Load_Glyph(currFace, glyphIndex, FT_LOAD_RENDER);
  auto& glyph = *(currFace->glyph);
  auto& bitmap = glyph.bitmap;

  glm::vec2 pos{
    (index % atlasWidth) * texCharSize.x,
    (index / atlasWidth) * texCharSize.y,
  };
  auto truePos = pos * dpiScale;

  for (size_t yy = 0; yy < bitmap.rows; yy++) {
    for (size_t xx = 0; xx < bitmap.width; xx++) {
      auto index = truePos.x + xx + (truePos.y + yy) * bufferSize.x;
      textureData[index].r = 255;
      textureData[index].g = 255;
      textureData[index].b = 255;
      textureData[index].a = bitmap.buffer[xx + yy * bitmap.width];
    }
  }

  auto pair = currMap->emplace(
    glyphIndex,
    GlyphInfo{
      // .size =
      //   {
      //     bitmap.width / dpiScale,
      //     bitmap.rows / dpiScale,
      //   },
      .bearing = isNerd ?
        glm::vec2(0, 0) :
        glm::vec2{
          glyph.bitmap_left / dpiScale,
          glyph.bitmap_top / dpiScale + vertOffset,
        },
      // .advance = (glyph.advance.x >> 6) / dpiScale,
      .region = MakeRegion(pos, texCharSize),
    }
  );

  return pair.first->second;
}

void Font::UpdateTexture() {
  if (!dirty) return;

  textureSizeBuffer =
    utils::CreateUniformBuffer(ctx.device, sizeof(glm::vec2), &textureSize);

  textureView =
    utils::CreateBindingTexture(
      ctx.device,
      {static_cast<uint32_t>(bufferSize.x), static_cast<uint32_t>(bufferSize.y)},
      wgpu::TextureFormat::RGBA8Unorm, textureData.data()
    )
      .CreateView();

  Sampler sampler = ctx.device.CreateSampler( //
    ToPtr(SamplerDescriptor{
      .addressModeU = AddressMode::ClampToEdge,
      .addressModeV = AddressMode::ClampToEdge,
      .magFilter = FilterMode::Nearest,
      .minFilter = FilterMode::Nearest,
    })
  );

  fontTextureBG = utils::MakeBindGroup(
    ctx.device, ctx.pipeline.fontTextureBGL,
    {
      {0, textureSizeBuffer},
      {1, textureView},
      {2, sampler},
    }
  );

  dirty = false;
}
