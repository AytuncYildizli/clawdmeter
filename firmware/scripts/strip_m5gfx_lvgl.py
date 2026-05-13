"""Pre-build hook: strip M5GFX's vendored LVGL font subset.

M5GFX (bundled inside M5Unified) ships a partial LVGL font subset that
conflicts with the real `lvgl/lvgl` library when both are linked together
(duplicate symbols: lv_font_get_bitmap_fmt_txt, lv_font_get_glyph_dsc_fmt_txt,
lv_font_montserrat_14, lv_utils_bsearch, etc.).

We delete those .c/.cpp files from the M5GFX libdep before compilation so only
the real LVGL provides those symbols. M5GFX itself only needs its own headers
for type declarations; the implementations come from lvgl.
"""
Import("env")  # noqa: F821  (provided by SCons)
import os
import glob

libdeps_root = env.subst("$PROJECT_LIBDEPS_DIR/$PIOENV/M5GFX/src/lgfx")

# Directories where every .c / .cpp is a duplicate of LVGL fonts.
DIR_TARGETS = [
    os.path.join(libdeps_root, "v1", "lv_font"),
    os.path.join(libdeps_root, "Fonts", "lvgl"),
]

# lgfx_fonts.cpp's LVGLfont wrapper references LV_FONT_GLYPH_FORMAT_*_ALIGNED
# enum values that were removed in lvgl 9.5. Patch those references to inert
# sentinel values so the file still compiles (we don't use LVGLfont anyway —
# we drive LVGL directly).
LGFX_FONTS_PATH = os.path.join(libdeps_root, "v1", "lgfx_fonts.cpp")
LGFX_FONTS_REPLACEMENTS = [
    ("LV_FONT_GLYPH_FORMAT_A1_ALIGNED", "0xF1 /* removed in lvgl 9.5 */"),
    ("LV_FONT_GLYPH_FORMAT_A2_ALIGNED", "0xF2 /* removed in lvgl 9.5 */"),
    ("LV_FONT_GLYPH_FORMAT_A4_ALIGNED", "0xF4 /* removed in lvgl 9.5 */"),
]

removed = 0
for target_dir in DIR_TARGETS:
    if not os.path.isdir(target_dir):
        continue
    patterns = ["*.c", "*.cpp"]
    for pat in patterns:
        for path in glob.glob(os.path.join(target_dir, pat)):
            try:
                os.remove(path)
                removed += 1
                print(f"[strip_m5gfx_lvgl] removed {path}")
            except OSError as exc:
                print(f"[strip_m5gfx_lvgl] WARN could not remove {path}: {exc}")

if os.path.isfile(LGFX_FONTS_PATH):
    try:
        with open(LGFX_FONTS_PATH, "r") as f:
            content = f.read()
        patched = content
        applied = 0
        for old, new in LGFX_FONTS_REPLACEMENTS:
            if old in patched:
                patched = patched.replace(old, new)
                applied += 1
        if applied and patched != content:
            with open(LGFX_FONTS_PATH, "w") as f:
                f.write(patched)
            print(f"[strip_m5gfx_lvgl] patched lgfx_fonts.cpp ({applied} lvgl 9.5 compat replacements)")
    except OSError as exc:
        print(f"[strip_m5gfx_lvgl] WARN could not patch {LGFX_FONTS_PATH}: {exc}")

if removed:
    print(f"[strip_m5gfx_lvgl] stripped {removed} duplicate LVGL source files from M5GFX")
else:
    print("[strip_m5gfx_lvgl] nothing to strip (already clean or paths missing)")

# M5GFX's LVGL compat shim does __has_include("lvgl/lvgl.h") to detect the
# real lvgl library. With PIO LDF, lvgl's includeDir is the lvgl folder itself
# so the header is at .../lvgl/lvgl.h relative to its parent. Add the parent
# directory (libdeps root) to the include path so "lvgl/lvgl.h" resolves.
libdeps_parent = env.subst("$PROJECT_LIBDEPS_DIR/$PIOENV")
if os.path.isdir(os.path.join(libdeps_parent, "lvgl")):
    env.Append(CPPPATH=[libdeps_parent])
    print(f"[strip_m5gfx_lvgl] appended {libdeps_parent} to CPPPATH so lvgl/lvgl.h resolves")
