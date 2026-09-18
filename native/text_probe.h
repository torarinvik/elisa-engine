#pragma once
// FreeType and HarfBuzz integration for text: the plan keeps text and UI on
// shared ecosystem components rather than reimplementing font handling. The
// probe loads a system font through FreeType, rasterizes one glyph to check its
// bitmap and advance, and shapes a short string through HarfBuzz to check the
// glyph count. It needs no rendering surface, so it runs in the headless probe.
#include "probe_support.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb-ft.h>
#include <hb.h>

#include <cstdio>
#include <cstdlib>
#include <vector>

namespace probe {

inline const char* first_openable_font() {
    static const char* candidates[] = {
        "/System/Library/Fonts/Supplemental/Arial.ttf",
        "/System/Library/Fonts/Supplemental/Andale Mono.ttf",
        "/System/Library/Fonts/Supplemental/Verdana.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
    };
    const char* configured = std::getenv("ELISA_TEXT_FONT");
    if (configured != nullptr && configured[0] != '\0') {
        return configured;
    }
    for (const char* candidate : candidates) {
        std::FILE* file = std::fopen(candidate, "rb");
        if (file != nullptr) {
            std::fclose(file);
            return candidate;
        }
    }
    return nullptr;
}

inline bool probe_text() {
    const char* font_path = first_openable_font();
    if (!check(font_path != nullptr, "a system font is available")) {
        return false;
    }
    FT_Library library;
    if (!check(FT_Init_FreeType(&library) == 0, "freetype init")) {
        return false;
    }
    FT_Face face;
    if (!check(FT_New_Face(library, font_path, 0, &face) == 0, "freetype font load")) {
        FT_Done_FreeType(library);
        return false;
    }
    FT_Set_Pixel_Sizes(face, 0, 32);
    if (!check(FT_Load_Char(face, 'E', FT_LOAD_RENDER) == 0, "freetype glyph rasterize")) {
        FT_Done_Face(face);
        FT_Done_FreeType(library);
        return false;
    }
    const int width = (int)face->glyph->bitmap.width;
    const int rows = (int)face->glyph->bitmap.rows;
    const int advance = (int)(face->glyph->advance.x >> 6);

    hb_font_t* hb_font = hb_ft_font_create(face, NULL);
    hb_buffer_t* buffer = hb_buffer_create();
    const char* text = "Elisa";
    hb_buffer_add_utf8(buffer, text, -1, 0, -1);
    hb_buffer_guess_segment_properties(buffer);
    hb_shape(hb_font, buffer, NULL, 0);
    unsigned int glyph_count = 0;
    hb_glyph_position_t* positions = hb_buffer_get_glyph_positions(buffer, &glyph_count);
    long total_advance = 0;
    for (unsigned int index = 0; index < glyph_count; ++index) {
        total_advance += positions[index].x_advance;
    }
    std::fprintf(stdout, "text: font=%s glyph=%dx%d advance=%d shaped_glyphs=%u text_advance=%.2f\n",
        font_path, width, rows, advance, glyph_count, total_advance / 64.0);
    hb_buffer_destroy(buffer);
    hb_font_destroy(hb_font);
    FT_Done_Face(face);
    FT_Done_FreeType(library);
    return check(width > 0 && rows > 0 && advance > 0 && glyph_count == 5,
        "freetype rasterizes a glyph and harfbuzz shapes five glyphs");
}

} // namespace probe
