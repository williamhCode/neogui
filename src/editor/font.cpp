#include "font.hpp"
#include "utils/logger.hpp"
#include "utils/unicode.hpp"
#include <ranges>
#include <string>
#include <boost/lexical_cast.hpp>

static auto SplitStr(std::string_view str, char delim) {
  return std::views::split(str, delim) |
         std::views::transform([](auto&& r) { return std::string_view(r); });
}

std::expected<FontFamily, std::string>
FontFamily::FromGuifont(std::string_view guifont, float dpiScale) {
  if (guifont.empty()) {
    return std::unexpected("Empty guifont");
  }

  // split string by colon, first string is required, options args can be any length
  std::string_view fontsStr;
  float height = 12;
  float width = 0;
  bool bold = false;
  bool italic = false;

  for (auto token : SplitStr(guifont, ':')) {
    if (token.empty()) continue;

    if (fontsStr.empty()) {
      fontsStr = token;
    } else {
      using namespace boost::conversion;
      if (token.starts_with("h")) {
        if (!try_lexical_convert(token.substr(1), height))
          return std::unexpected("Invalid guifont height: " + std::string(token));
      } else if (token.starts_with("w")) {
        if (!try_lexical_convert(token.substr(1), width))
          return std::unexpected("Invalid guifont width: " + std::string(token));
      } else if (token == "b") {
        bold = true;
      } else if (token == "i") {
        italic = true;
      }
    }
  }

  try {
    return FontFamily{
      .fonts = SplitStr(fontsStr, ',') | std::views::transform([&](auto&& fontName) {
        FontSet fontSet;
        auto makeFontHandle = [&](bool bold, bool italic) {
          auto font = Font::FromName(
            {
              .name = std::string(fontName),
              .height = height,
              .width = width,
              .bold = bold,
              .italic = italic,
            },
            dpiScale
          )
          .value(); // allow exception to propagate
          // reuse the existing FontHandle
          if (fontSet.normal && font.path == fontSet.normal->path) {
            return fontSet.normal;
          }
          // otherwise, create a new FontHandle
          return std::make_shared<Font>(std::move(font));
        };

        fontSet.normal = makeFontHandle(bold, italic);

        if (!(bold || italic)) {
          fontSet.bold = makeFontHandle(true, false);
          fontSet.italic = makeFontHandle(false, true);
          fontSet.boldItalic = makeFontHandle(true, true);
        }

        return fontSet;
      }) |
      std::ranges::to<std::vector>(),

      .textureAtlas{height, dpiScale},
    };
  } catch (const std::bad_expected_access<std::string>& e) {
    return std::unexpected(e.error());
  }
}

void FontFamily::ChangeDpiScale(float dpiScale) {
  std::vector<FontSet> newFonts;
  for (auto& fontSet : fonts) {
    FontSet& newFontSet = newFonts.emplace_back();
    auto makeFontHandle = [&](const FontHandle& fontHandle) -> FontHandle {
      // check if the normal font can be reused
      if (newFontSet.normal && fontHandle->path == newFontSet.normal->path) {
        return newFontSet.normal;
      }
      return std::make_shared<Font>(
        fontHandle->path, fontHandle->height, fontHandle->width, dpiScale
      );
    };

    newFontSet.normal = makeFontHandle(fontSet.normal);
    newFontSet.bold = makeFontHandle(fontSet.bold);
    newFontSet.italic = makeFontHandle(fontSet.italic);
    newFontSet.boldItalic = makeFontHandle(fontSet.boldItalic);
  }
  fonts = std::move(newFonts);

  textureAtlas = TextureAtlas(DefaultFont().height, dpiScale);
}

const Font& FontFamily::DefaultFont() const {
  return *fonts.front().normal;
}

const Font::GlyphInfo&
FontFamily::GetGlyphInfo(char32_t charcode, bool bold, bool italic) {
  for (const auto& fontSet : fonts) {
    const auto& font = [&] {
      if (bold && italic) {
        return fontSet.boldItalic ? fontSet.boldItalic : fontSet.normal;
      }
      if (bold) {
        return fontSet.bold ? fontSet.bold : fontSet.normal;
      }
      if (italic) {
        return fontSet.italic ? fontSet.italic : fontSet.normal;
      }
      return fontSet.normal;
    }();

    if (const auto* glyphInfo = font->GetGlyphInfo(charcode, textureAtlas)) {
      return *glyphInfo;
    }
  }

  const auto* glyphInfo = fonts.front().normal->GetGlyphInfo(' ', textureAtlas);
  if (glyphInfo == nullptr) {
    LOG_ERR(
      "Failed to get glyph info for codepoint: {}, {}", (uint32_t)charcode,
      UnicodeToUTF8(charcode)
    );
  }
  return *glyphInfo;
}
