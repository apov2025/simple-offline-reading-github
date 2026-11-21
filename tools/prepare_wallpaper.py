#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""小纸 Pico 壁纸离线转换；不连接设备。/ Offline Read Pico wallpaper conversion.

Requires Pillow. Native format: 684×1216, 4bpp, low nibble is the left pixel.
"""
import argparse
import hashlib
import io
import json
import math
import os
from pathlib import Path
import sys
import tempfile

from PIL import Image, ImageCms, ImageOps

WIDTH, HEIGHT = 684, 1216
SIZE = (WIDTH, HEIGHT)
RAW_SIZE = WIDTH * HEIGHT // 2


def encode_raw(gray, dither='none'):
    """量化并按原生顺序打包。/ Quantize and pack in native pixel order."""
    if gray.mode != 'L' or gray.size != SIZE:
        raise ValueError('Encoder requires a 684x1216 grayscale image.')
    if dither not in ('none', 'fs'):
        raise ValueError('Dither must be none or fs.')
    pixels = gray.tobytes()
    if dither == 'none':
        levels = pixels.translate(bytes((value + 8) // 17 for value in range(256)))
    else:
        # 真正的16级Floyd–Steinberg，不调用1bit转换。/ True 16-level Floyd–Steinberg, never a 1-bit conversion.
        levels = bytearray(len(pixels))
        current = [0.0] * (WIDTH + 2)
        for y in range(HEIGHT):
            following = [0.0] * (WIDTH + 2)
            row = y * WIDTH
            for x in range(WIDTH):
                value = pixels[row + x] + current[x + 1]
                level = max(0, min(15, math.floor(value / 17.0 + 0.5)))
                levels[row + x] = level
                error = value - level * 17.0
                current[x + 2] += error * 7 / 16
                following[x] += error * 3 / 16
                following[x + 1] += error * 5 / 16
                following[x + 2] += error / 16
            current = following
    return bytes(levels[i] | (levels[i + 1] << 4) for i in range(0, len(levels), 2))


def decode_raw(raw):
    """最终字节反解成预览，拒绝错误长度。/ Decode the final bytes and reject incorrect length."""
    if len(raw) != RAW_SIZE:
        raise ValueError(f'Raw wallpaper must be exactly {RAW_SIZE} bytes; got {len(raw)}.')
    pixels = bytearray(WIDTH * HEIGHT)
    pixels[0::2] = bytes((value & 15) * 17 for value in raw)
    pixels[1::2] = bytes((value >> 4) * 17 for value in raw)
    return Image.frombytes('L', SIZE, bytes(pixels))


def prepare_image(source, fit='contain', ignore_icc=False):
    """EXIF、色彩、透明白底、等比缩放。/ EXIF, color, white alpha composition and proportional resize."""
    with Image.open(source) as opened:
        if getattr(opened, 'n_frames', 1) != 1:
            raise ValueError('Animated/multipage input is ambiguous; export one still image first.')
        source_format, source_size = opened.format, opened.size
        icc = opened.info.get('icc_profile')
        image = ImageOps.exif_transpose(opened)
        image.load()
    oriented_size = image.size
    rgba = image.convert('RGBA')
    alpha = rgba.getchannel('A')
    warnings = []
    color_policy = 'assumed-sRGB'
    if icc and not ignore_icc:
        try:
            profile = ImageCms.ImageCmsProfile(io.BytesIO(icc))
            # CMYK/L配置使用其原模式，其余转RGB并保留透明度。/ Keep CMYK/L profile input; other modes use RGB with alpha preserved separately.
            color_input = image if image.mode in ('CMYK', 'L', 'LAB') else rgba.convert('RGB')
            rgb = ImageCms.profileToProfile(color_input, profile, ImageCms.createProfile('sRGB'), outputMode='RGB')
            color_policy = 'embedded-ICC-to-sRGB'
        except (OSError, ValueError, TypeError, ImageCms.PyCMSError) as error:
            raise ValueError('Embedded ICC profile could not be converted. Export a valid sRGB image, or explicitly use --ignore-icc after reviewing the color difference.') from error
    else:
        rgb = rgba.convert('RGB')
        if icc:
            warnings.append('Embedded ICC profile explicitly ignored; colors may differ from the source.')
            color_policy = 'ignored-embedded-ICC'
    rgba = rgb.convert('RGBA')
    rgba.putalpha(alpha)
    image = Image.alpha_composite(Image.new('RGBA', rgba.size, 'white'), rgba).convert('RGB')
    if fit == 'contain':
        resized = ImageOps.contain(image, SIZE, method=Image.Resampling.LANCZOS)
        x, y = (WIDTH - resized.width) // 2, (HEIGHT - resized.height) // 2
        canvas = Image.new('RGB', SIZE, 'white')
        canvas.paste(resized, (x, y))
        placement = {'x': x, 'y': y, 'width': resized.width, 'height': resized.height}
    elif fit == 'cover':
        canvas = ImageOps.fit(image, SIZE, method=Image.Resampling.LANCZOS, centering=(0.5, 0.5))
        placement = {'x': 0, 'y': 0, 'width': WIDTH, 'height': HEIGHT}
    else:
        raise ValueError('Fit must be contain or cover.')
    return ImageOps.grayscale(canvas), {
        'format': source_format, 'encoded_size': list(source_size),
        'oriented_size': list(oriented_size), 'color_policy': color_policy,
    }, placement, warnings


def write_outputs(items, source, force):
    """先完整暂存，再发布；默认不覆盖。/ Stage complete artifacts before publishing; no overwrite by default."""
    source = source.resolve()
    targets = [path.resolve() for path in items]
    if len(set(targets)) != len(targets) or source in targets:
        raise ValueError('Output paths must be distinct and must never replace the source image/raw file.')
    for path in items:
        if path.exists() and (not force or not path.is_file()):
            raise ValueError(f'Output already exists: {path.name}; use --force to replace generated files.')
        if not path.parent.is_dir():
            raise ValueError(f'Output directory does not exist: {path.parent}')
    staged = []
    created = []
    try:
        for path, data in items.items():
            with tempfile.NamedTemporaryFile(dir=path.parent, prefix='.wallpaper-', delete=False) as temp:
                temp.write(data)
                staged.append((Path(temp.name), path))
        for temp, path in staged:
            if force:
                os.replace(temp, path)
            else:
                os.link(temp, path)
                created.append(path)
                temp.unlink()
    except Exception:
        # 无覆盖模式发布失败时撤回本次新增文件。/ Roll back newly published files if no-overwrite publication fails.
        if not force:
            for path in created:
                path.unlink(missing_ok=True)
        raise
    finally:
        for temp, _ in staged:
            temp.unlink(missing_ok=True)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', nargs='?', type=Path, help='PNG/JPEG or another supported still image')
    parser.add_argument('-o', '--output', type=Path, help='Destination .4bpp; default is the source basename')
    parser.add_argument('--fit', choices=('contain', 'cover'), default='contain', help='contain keeps the whole image on white; cover explicitly crops the center')
    parser.add_argument('--dither', choices=('none', 'fs'), default='none', help='none: 16 gray levels; fs: optional 16-level Floyd–Steinberg for photos')
    parser.add_argument('--ignore-icc', action='store_true', help='Explicitly ignore an embedded ICC profile, including an invalid one')
    parser.add_argument('--validate', type=Path, metavar='RAW', help='Check an existing raw file and generate its decoded preview + metadata')
    parser.add_argument('--force', action='store_true', help='Replace existing generated outputs; never replace the input')
    args = parser.parse_args(argv)
    try:
        if bool(args.image) == bool(args.validate):
            raise ValueError('Provide either an input image or --validate RAW, never both.')
        if args.validate and (args.output or args.fit != 'contain' or args.dither != 'none' or args.ignore_icc):
            raise ValueError('--validate accepts only RAW and optional --force.')
        source = args.validate or args.image
        if not source.is_file():
            raise ValueError('Input must be an existing regular file.')
        source_bytes = source.read_bytes()
        metadata = {
            'schema': 'read-pico-wallpaper-v1', 'width': WIDTH, 'height': HEIGHT,
            'raw_bytes': RAW_SIZE, 'header_bytes': 0, 'scan_order': 'row-major-top-left',
            'packing': 'low-nibble-left/high-nibble-right', 'black': 0, 'white': 15,
            'rotation': 'none',
        }
        if args.validate:
            raw, output = source_bytes, source
            metadata['operation'] = 'validate-raw'
            metadata['note'] = 'Checks byte count and decoding only; any exact-length byte stream is structurally valid. Physical display quality is not verified.'
            warnings = []
        else:
            output = args.output or source.with_suffix('.4bpp')
            if output.suffix != '.4bpp' or output.name.startswith('.'):
                raise ValueError('Output must use lowercase .4bpp and must not be a hidden file.')
            if len(output.name.encode('utf8')) > 95:
                raise ValueError('Wallpaper filename must be at most 95 UTF-8 bytes including .4bpp; use a short name such as 001.4bpp.')
            gray, info, placement, warnings = prepare_image(io.BytesIO(source_bytes), args.fit, args.ignore_icc)
            raw = encode_raw(gray, args.dither)
            info['sha256'] = hashlib.sha256(source_bytes).hexdigest()
            metadata.update(operation='convert-image', source=info, fit=args.fit,
                            placement=placement, resampling='Lanczos', gray_levels=16,
                            dither=args.dither, alpha_background='white')
        preview = decode_raw(raw)
        buffer = io.BytesIO()
        preview.save(buffer, format='PNG', optimize=False)
        png = buffer.getvalue()
        metadata.update(raw_sha256=hashlib.sha256(raw).hexdigest(),
                        preview_sha256=hashlib.sha256(png).hexdigest(), warnings=warnings)
        items = {} if args.validate else {output: raw}
        items[output.with_suffix('.preview.png')] = png
        items[output.with_suffix('.json')] = (json.dumps(metadata, ensure_ascii=False, indent=2, sort_keys=True) + '\n').encode('utf8')
        write_outputs(items, source, args.force)
        for warning in warnings:
            print('Warning: ' + warning, file=sys.stderr)
        print(f'{"Validated" if args.validate else "Created"}: {output.name}; {RAW_SIZE} bytes; SHA256 {metadata["raw_sha256"]}')
        print(f'Preview: {output.with_suffix(".preview.png").name}; metadata: {output.with_suffix(".json").name}')
        return 0
    except (ValueError, OSError, Image.DecompressionBombError) as error:
        print('Error: ' + str(error), file=sys.stderr)
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
