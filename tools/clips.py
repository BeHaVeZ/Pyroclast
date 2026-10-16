"""Turn a --capture-sequence folder into an animated WebP or GIF, or a contact sheet to choose ticks.

    python tools/clips.py sheet DIR OUT.png [--from T] [--to T] [--count 24] [--crop X,Y,W,H]
    python tools/clips.py encode DIR OUT.webp|OUT.gif [--from T] [--to T] [--step N] [--ranges A-B:N,...]
                                 [--fps F] [--width W] [--crop X,Y,W,H] [--quality Q] [--dither none]

Frames are DIR/tick-NNNNNN.png, a tick 1/60 s: --step 2 at 30 fps plays in real time. --ranges replaces --from, --to
and --step, so one clip can change speed. --crop is in captured pixels and applies before scaling to --width. GIF uses
one adaptive palette with Floyd-Steinberg dithering (--dither none: banded but smaller). Needs Pillow.
"""

import argparse
import pathlib
import re
import sys

from PIL import Image, ImageDraw

TICK = re.compile(r"tick-(\d{6})\.png$")


def ticks(folder, first, last):
    found = {}
    for path in pathlib.Path(folder).iterdir():
        match = TICK.search(path.name)
        if match:
            found[int(match.group(1))] = path
    chosen = [t for t in sorted(found) if (first is None or t >= first) and (last is None or t <= last)]
    if not chosen:
        sys.exit(f"no frames in {folder} between {first} and {last}")
    return [(t, found[t]) for t in chosen]


def parse_crop(text):
    if text is None:
        return None
    x, y, w, h = (int(v) for v in text.split(","))
    return (x, y, x + w, y + h)


def load(path, crop, width):
    image = Image.open(path).convert("RGB")
    if crop:
        image = image.crop(crop)
    if width and image.width != width:
        height = round(image.height * width / image.width / 2) * 2
        image = image.resize((width, height), Image.LANCZOS)
    return image


def sheet(args):
    frames = ticks(args.folder, args.first, args.last)
    count = min(args.count, len(frames))
    picked = [frames[round(i * (len(frames) - 1) / max(count - 1, 1))] for i in range(count)]
    columns = 4
    images = [load(path, parse_crop(args.crop), 320) for _, path in picked]
    w, h = images[0].size
    rows = (len(images) + columns - 1) // columns
    canvas = Image.new("RGB", (columns * w, rows * (h + 16)), "black")
    draw = ImageDraw.Draw(canvas)
    for i, ((tick, _), image) in enumerate(zip(picked, images)):
        x, y = (i % columns) * w, (i // columns) * (h + 16)
        canvas.paste(image, (x, y + 16))
        draw.text((x + 4, y + 2), f"tick {tick}", fill="white")
    canvas.save(args.output)


def chosen_frames(args):
    if not args.ranges:
        return ticks(args.folder, args.first, args.last)[:: args.step]
    frames = []
    for part in args.ranges.split(","):
        span, step = part.split(":")
        first, last = (int(v) for v in span.split("-"))
        frames += ticks(args.folder, first, last)[:: int(step)]
    return frames


def encode(args):
    frames = chosen_frames(args)
    crop = parse_crop(args.crop)
    images = [load(path, crop, args.width) for _, path in frames]
    duration = round(1000 / args.fps)
    output = pathlib.Path(args.output)
    if output.suffix.lower() == ".gif":
        samples = [images[round(i * (len(images) - 1) / 15)] for i in range(16)]
        w, h = samples[0].size
        mosaic = Image.new("RGB", (4 * w, 4 * h))
        for i, sample in enumerate(samples):
            mosaic.paste(sample, ((i % 4) * w, (i // 4) * h))
        palette = mosaic.quantize(colors=256, method=Image.Quantize.MEDIANCUT)
        dither = Image.Dither.NONE if args.dither == "none" else Image.Dither.FLOYDSTEINBERG
        images = [image.quantize(palette=palette, dither=dither) for image in images]
        images[0].save(output, save_all=True, append_images=images[1:], duration=duration, loop=0, optimize=True)
    else:
        images[0].save(output, save_all=True, append_images=images[1:], duration=duration, loop=0,
                       quality=args.quality, method=6)
    size = output.stat().st_size
    print(f"{output}: {len(images)} frames, {images[0].width}x{images[0].height}, "
          f"{len(images) * duration / 1000:.1f} s, {size / 1e6:.2f} MB")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    for name in ("sheet", "encode"):
        command = commands.add_parser(name)
        command.add_argument("folder")
        command.add_argument("output")
        command.add_argument("--from", dest="first", type=int)
        command.add_argument("--to", dest="last", type=int)
        command.add_argument("--crop")
    commands.choices["sheet"].add_argument("--count", type=int, default=24)
    encoder = commands.choices["encode"]
    encoder.add_argument("--step", type=int, default=2)
    encoder.add_argument("--ranges")
    encoder.add_argument("--dither", choices=("floyd", "none"), default="floyd")
    encoder.add_argument("--fps", type=float, default=30)
    encoder.add_argument("--width", type=int, default=800)
    encoder.add_argument("--quality", type=int, default=80)
    args = parser.parse_args()
    sheet(args) if args.command == "sheet" else encode(args)


if __name__ == "__main__":
    main()
