#include "layout_math.h"

namespace layout {

Point big_number_origin(int glyph_w, int glyph_h) {
    return { (SCREEN_W - glyph_w) / 2, (SCREEN_H - glyph_h) / 2 };
}

Point provider_tag_origin(int text_w, int /*text_h*/) {
    return { (SCREEN_W - text_w) / 2, TOP_MARGIN };
}

Point page_indicator_origin() {
    return { SCREEN_W / 2, SCREEN_H - BOTTOM_INDICATOR_OFFSET };
}

Point repo_label_origin(int text_w, int /*text_h*/) {
    return { (SCREEN_W - text_w) / 2, REPO_LABEL_Y };
}

}  // namespace layout
