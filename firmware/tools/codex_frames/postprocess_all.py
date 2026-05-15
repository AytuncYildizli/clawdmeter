"""Post-process all 4 Codex frames (raw Gemini PNGs -> 160x160 RGBA with
transparent BG). Run from anywhere.

Matches the Clawd pipeline:
  - White-near pixels -> alpha=0
  - Crop to opaque bbox to remove white-padding margin
  - Resize 160x160 nearest-neighbor (preserves pixel-art)
"""
from PIL import Image
from pathlib import Path

HERE = Path(__file__).parent
FRAMES = ["idle", "blink", "wave", "sleep"]
WHITE_THRESHOLD = 230


def process(name: str) -> None:
    raw = HERE / f"codex_{name}_raw.png"
    out = HERE / f"codex_{name}.png"
    img = Image.open(raw).convert("RGBA")
    new = []
    for r, g, b, a in img.getdata():
        if r >= WHITE_THRESHOLD and g >= WHITE_THRESHOLD and b >= WHITE_THRESHOLD:
            new.append((0, 0, 0, 0))
        else:
            new.append((r, g, b, 255))
    img.putdata(new)
    bbox = img.getbbox()
    if bbox:
        img = img.crop(bbox)
    img = img.resize((160, 160), Image.NEAREST)
    img.save(out)
    print(f"{raw.name} -> {out.name}")


if __name__ == "__main__":
    for name in FRAMES:
        process(name)
