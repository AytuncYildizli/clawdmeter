#pragma once

namespace layout {

constexpr int SCREEN_W = 320;
constexpr int SCREEN_H = 240;
constexpr int TOP_MARGIN = 8;
constexpr int BOTTOM_INDICATOR_OFFSET = 4;
constexpr int REPO_LABEL_Y = 215;

struct Point { int x; int y; };

Point big_number_origin(int glyph_w, int glyph_h);
Point provider_tag_origin(int text_w, int text_h);
Point page_indicator_origin();
Point repo_label_origin(int text_w, int text_h);

}  // namespace layout
