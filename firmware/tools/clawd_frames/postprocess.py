"""Post-process Gemini-generated sprite frames into 80x80 RGBA pixel-art.

Steps per frame:
1. Open raw PNG (Gemini outputs ~512-1024px white-bg images)
2. Threshold white pixels (R>240 AND G>240 AND B>240) to alpha=0
3. Auto-crop to content bounding box (alpha > 0)
4. Pad to square (so resize doesn't distort)
5. Resize to 80x80 with NEAREST (preserves visible pixel-art look)
6. Save as <stem>.png alongside the raw file
"""
from __future__ import annotations
import sys
from pathlib import Path
from PIL import Image


def whiten_to_alpha(img: Image.Image, threshold: int = 240) -> Image.Image:
    """Replace near-white pixels with full transparency."""
    img = img.convert("RGBA")
    data = img.getdata()
    new_data = []
    for r, g, b, a in data:
        if r >= threshold and g >= threshold and b >= threshold:
            new_data.append((255, 255, 255, 0))
        else:
            new_data.append((r, g, b, a))
    img.putdata(new_data)
    return img


def crop_to_content(img: Image.Image) -> Image.Image:
    bbox = img.getbbox()
    if bbox is None:
        return img
    return img.crop(bbox)


def pad_to_square(img: Image.Image) -> Image.Image:
    w, h = img.size
    side = max(w, h)
    canvas = Image.new("RGBA", (side, side), (255, 255, 255, 0))
    canvas.paste(img, ((side - w) // 2, (side - h) // 2))
    return canvas


def process(raw_path: Path, out_path: Path, size: int = 80) -> None:
    img = Image.open(raw_path)
    img = whiten_to_alpha(img)
    img = crop_to_content(img)
    img = pad_to_square(img)
    img = img.resize((size, size), Image.NEAREST)
    img.save(out_path, "PNG")
    print(f"  {raw_path.name} -> {out_path.name} ({size}x{size})")


def main() -> int:
    here = Path(__file__).parent
    frames = [
        ("01_idle_raw.png",  "clawd_idle.png"),
        ("02_blink_raw.png", "clawd_blink.png"),
        ("03_wave_raw.png",  "clawd_wave.png"),
        ("04_sleep_raw.png", "clawd_sleep.png"),
    ]
    for raw_name, out_name in frames:
        raw = here / raw_name
        out = here / out_name
        if not raw.exists():
            print(f"  WARN: {raw_name} missing, skipping")
            continue
        process(raw, out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
