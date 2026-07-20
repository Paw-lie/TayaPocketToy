#!/usr/bin/env python3
"""
Generate C++ sprite assets from images under a Sprites folder.

Behavior:
- Each subfolder under Sprites can contain multiple animations.
- Frames are grouped by filename stem (trailing digits removed), e.g. Apple1, Apple2.
- Generated animation names include category path and stem, e.g. food_apple.
- Frames are sorted by natural filename order.
- Black/dark pixels become ON bits (white on OLED).
- Output format is SSD1306 vertical pages, LSB=top.

Outputs:
- GeneratedSprites.h
- GeneratedSprites.cpp

The generated files can be included from your Arduino sketch and mapped into Assets.cpp.
"""

from __future__ import annotations

import argparse
from collections import OrderedDict
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, List

try:
    from PIL import Image
except ImportError as exc:
    raise SystemExit("Pillow is required. Install with: pip install pillow") from exc


IMAGE_EXTS = {".png", ".bmp", ".jpg", ".jpeg", ".webp"}


def sanitize_identifier(name: str) -> str:
    cleaned = re.sub(r"[^0-9A-Za-z_]", "_", name)
    if not cleaned:
        cleaned = "sprite"
    if cleaned[0].isdigit():
        cleaned = f"_{cleaned}"
    return cleaned


def natural_key(path: Path):
    parts = re.split(r"(\d+)", path.stem.lower())
    keyed = []
    for part in parts:
        if part.isdigit():
            keyed.append(int(part))
        else:
            keyed.append(part)
    return keyed


def discover_animation_folders(sprites_root: Path) -> List[Path]:
    folders = [p for p in sprites_root.rglob("*") if p.is_dir() and discover_images(p)]
    folders.sort(key=lambda p: p.relative_to(sprites_root).as_posix().lower())
    return folders


def discover_images(folder: Path) -> List[Path]:
    files = [p for p in folder.iterdir() if p.is_file() and p.suffix.lower() in IMAGE_EXTS]
    files.sort(key=natural_key)
    return files


def normalize_name_part(raw: str) -> str:
    value = re.sub(r"[^0-9A-Za-z]+", "_", raw.strip().lower()).strip("_")
    return value or "none"


def animation_name_from_images(images: List[Path], fallback: str) -> str:
    # Frames are expected as name + index (e.g. idle_1, idle_2).
    stripped_stems = []
    for img_path in images:
        stripped = re.sub(r"[_\-\s]*\d+$", "", img_path.stem)
        stripped_stems.append(normalize_name_part(stripped))

    candidates = [name for name in stripped_stems if name != "none"]
    if candidates:
        return candidates[0]

    return normalize_name_part(fallback)


def frame_group_key(image_path: Path) -> str:
    stripped = re.sub(r"[_\-\s]*\d+$", "", image_path.stem)
    return normalize_name_part(stripped)


def folder_category_name(sprites_root: Path, folder: Path) -> str:
    rel_parts = [normalize_name_part(part) for part in folder.relative_to(sprites_root).parts]
    non_empty = [part for part in rel_parts if part != "none"]
    if not non_empty:
        return "misc"
    return "_".join(non_empty)


def load_mono_image(path: Path) -> Image.Image:
    # Keep alpha; transparent pixels are treated as OFF.
    return Image.open(path).convert("RGBA")


def image_to_ssd1306_vertical_lsb(img: Image.Image, threshold: int) -> bytes:
    width, height = img.size
    pages = (height + 7) // 8
    out = bytearray(width * pages)
    rgba = img.load()

    for y in range(height):
        for x in range(width):
            r, g, b, a = rgba[x, y]
            if a == 0:
                continue

            # Black/dark pixel -> ON bit (white on monochrome OLED)
            luminance = (r * 299 + g * 587 + b * 114) // 1000
            if luminance > threshold:
                continue

            page = y // 8
            bit = y % 8
            index = page * width + x
            out[index] |= 1 << bit

    return bytes(out)


def bytes_as_cpp_hex(data: bytes, indent: str = "  ", per_line: int = 16) -> str:
    rows = []
    for i in range(0, len(data), per_line):
        chunk = data[i : i + per_line]
        rows.append(indent + ", ".join(f"0x{b:02x}" for b in chunk))
    return ",\n".join(rows)


def is_empty_frame(data: bytes) -> bool:
    return not any(data)


@dataclass
class FrameDef:
    symbol: str
    width: int
    height: int
    data: bytes


@dataclass
class AnimationDefGen:
    name: str
    symbol: str
    frame_symbols: List[str]
    width: int
    height: int


def generate_assets(sprites_root: Path, threshold: int) -> tuple[List[FrameDef], List[AnimationDefGen]]:
    frames: List[FrameDef] = []
    animations: List[AnimationDefGen] = []
    skipped_empty_frames = 0
    used_anim_names: dict[str, int] = {}

    for folder in discover_animation_folders(sprites_root):
        images = discover_images(folder)
        if not images:
            continue

        grouped_images: "OrderedDict[str, List[Path]]" = OrderedDict()
        for img_path in images:
            key = frame_group_key(img_path)
            if key not in grouped_images:
                grouped_images[key] = []
            grouped_images[key].append(img_path)

        category_name = folder_category_name(sprites_root, folder)

        for group_key, group_frames in grouped_images.items():
            # If grouping fails, keep a deterministic per-folder fallback.
            stem_name = group_key if group_key != "none" else animation_name_from_images(group_frames, fallback=folder.name)
            anim_name = f"{category_name}_{stem_name}"
            if anim_name in used_anim_names:
                used_anim_names[anim_name] += 1
                anim_name = f"{anim_name}_{used_anim_names[anim_name]}"
            else:
                used_anim_names[anim_name] = 1

            anim_symbol = sanitize_identifier(anim_name)
            frame_symbols: List[str] = []
            base_w = None
            base_h = None

            for idx, img_path in enumerate(group_frames):
                img = load_mono_image(img_path)
                w, h = img.size

                if base_w is None:
                    base_w, base_h = w, h
                elif w != base_w or h != base_h:
                    raise ValueError(
                        f"Animation '{anim_name}' has mixed frame sizes: {img_path.name} is {w}x{h}, expected {base_w}x{base_h}"
                    )

                symbol = f"k{anim_symbol}_frame_{idx + 1}"
                data = image_to_ssd1306_vertical_lsb(img, threshold)

                if is_empty_frame(data):
                    skipped_empty_frames += 1
                    print(f"Skipping empty frame: {img_path}")
                    continue

                frame_symbols.append(symbol)
                frames.append(FrameDef(symbol=symbol, width=w, height=h, data=data))

            if not frame_symbols:
                print(f"Skipping animation with no non-empty frames: {anim_name}")
                continue

            animations.append(
                AnimationDefGen(
                    name=anim_name,
                    symbol=f"k{anim_symbol}",
                    frame_symbols=frame_symbols,
                    width=base_w or 0,
                    height=base_h or 0,
                )
            )

    if not animations:
        raise ValueError(f"No animation frames found under: {sprites_root}")

    if skipped_empty_frames:
        print(f"Skipped {skipped_empty_frames} empty frame(s).")

    return frames, animations


def write_header(out_h: Path) -> None:
    text = """#pragma once

#include <Arduino.h>
#include \"Assets.h\"

namespace GeneratedSprites {

const AnimationDef& animationAt(uint8_t index);
uint8_t animationCount();
const char* animationName(uint8_t index);

}  // namespace GeneratedSprites
"""
    out_h.write_text(text, encoding="ascii")


def write_cpp(out_cpp: Path, header_name: str, frames: Iterable[FrameDef], animations: Iterable[AnimationDefGen]) -> None:
    frame_list = list(frames)
    anim_list = list(animations)

    lines: List[str] = []
    lines.append(f'#include "{header_name}"')
    lines.append("")
    lines.append("namespace {")
    lines.append("")

    for frame in frame_list:
        lines.append(f"const uint8_t {frame.symbol}[] PROGMEM = {{")
        lines.append(bytes_as_cpp_hex(frame.data))
        lines.append("};")
        lines.append("")

    for anim in anim_list:
        frame_table = f"{anim.symbol}_frames"
        lines.append(f"const SpriteFrame {frame_table}[] = {{")
        for fs in anim.frame_symbols:
            lines.append(f"  {{{fs}, {anim.width}, {anim.height}, 0, 0, true}},")
        lines.append("};")
        lines.append(f"const AnimationDef {anim.symbol} = {{{frame_table}, {len(anim.frame_symbols)}}};")
        lines.append("")

    lines.append("const AnimationDef* kAnimations[] = {")
    for anim in anim_list:
        lines.append(f"  &{anim.symbol},")
    lines.append("};")
    lines.append("")

    lines.append("const char* kAnimationNames[] = {")
    for anim in anim_list:
        escaped = anim.name.replace('"', '\\"')
        lines.append(f'  "{escaped}",')
    lines.append("};")
    lines.append("")

    lines.append("constexpr uint8_t kAnimationCount = static_cast<uint8_t>(sizeof(kAnimations) / sizeof(kAnimations[0]));")
    lines.append("")
    lines.append("}  // namespace")
    lines.append("")
    lines.append("namespace GeneratedSprites {")
    lines.append("")
    lines.append("const AnimationDef& animationAt(uint8_t index) {")
    lines.append("  return *kAnimations[index % kAnimationCount];")
    lines.append("}")
    lines.append("")
    lines.append("uint8_t animationCount() {")
    lines.append("  return kAnimationCount;")
    lines.append("}")
    lines.append("")
    lines.append("const char* animationName(uint8_t index) {")
    lines.append("  return kAnimationNames[index % kAnimationCount];")
    lines.append("}")
    lines.append("")
    lines.append("}  // namespace GeneratedSprites")
    lines.append("")

    out_cpp.write_text("\n".join(lines), encoding="ascii")


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate C++ sprite assets from image folders.")
    parser.add_argument("--sprites", type=Path, default=Path("Sprites"), help="Path to sprite root folder")
    parser.add_argument(
        "--out-header",
        type=Path,
        default=Path("sketch_jul10a") / "GeneratedSprites.h",
        help="Output header path",
    )
    parser.add_argument(
        "--out-cpp",
        type=Path,
        default=Path("sketch_jul10a") / "GeneratedSprites.cpp",
        help="Output source path",
    )
    parser.add_argument(
        "--threshold",
        type=int,
        default=128,
        help="Darkness threshold [0..255]; lower is stricter black-only",
    )

    args = parser.parse_args()

    sprites_root = args.sprites.resolve()
    out_h = args.out_header.resolve()
    out_cpp = args.out_cpp.resolve()

    if not sprites_root.exists() or not sprites_root.is_dir():
        raise SystemExit(f"Sprites folder not found: {sprites_root}")

    threshold = max(0, min(255, args.threshold))

    frames, animations = generate_assets(sprites_root, threshold)

    out_h.parent.mkdir(parents=True, exist_ok=True)
    out_cpp.parent.mkdir(parents=True, exist_ok=True)

    write_header(out_h)
    write_cpp(out_cpp, out_h.name, frames, animations)

    print(f"Generated {out_h}")
    print(f"Generated {out_cpp}")
    print(f"Animations: {len(animations)}")
    print(f"Frames: {len(frames)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
