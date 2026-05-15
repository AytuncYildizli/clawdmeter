"""Post-process the Gemini-generated Codex sprite into 160x160 RGB565 with
transparent background.

Same pipeline as Clawd:
  - Read raw PNG (typically 1024x1024 or 512x512)
  - Replace near-white pixels with alpha=0 (BG removal)
  - Crop to opaque bounding box
  - Resize to 160x160 nearest-neighbor (preserves pixel-art look)
  - Save as clawd_codex.png for the header generator

Run from firmware/tools/codex_frames/.
"""
from PIL import Image
from pathlib import Path

HERE = Path(__file__).parent
RAW = HERE / "codex_idle_raw.png"
OUT = HERE / "codex_idle.png"

# Anything brighter than this treated as background; transparent in output.
WHITE_THRESHOLD = 230


def main() -> None:
    img = Image.open(RAW).convert("RGBA")
    px = img.getdata()
    new = []
    for r, g, b, a in px:
        if r >= WHITE_THRESHOLD and g >= WHITE_THRESHOLD and b >= WHITE_THRESHOLD:
            new.append((0, 0, 0, 0))
        else:
            new.append((r, g, b, 255))
    img.putdata(new)

    # Crop to opaque bounding box so we don't include white-padding margin
    bbox = img.getbbox()
    if bbox:
        img = img.crop(bbox)

    img = img.resize((160, 160), Image.NEAREST)
    img.save(OUT)
    print(f"{RAW.name} -> {OUT.name} ({img.size[0]}x{img.size[1]})")


if __name__ == "__main__":
    main()
