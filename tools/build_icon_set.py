#!/usr/bin/env python3
"""Build the icon set that gets compiled into the firmware.

The panel used to get every widget icon from openHAB, one HTTP GET per tile per
state change. That works, but it costs a round trip on the critical path of
every page load, and it is the one thing on the tile that a panel with a healthy
WLAN and a slow server cannot draw. The icons are small, they almost never
change, and there are only a few hundred of them -- so they can simply be in the
firmware.

This script produces that set: it fetches the openHAB classic icon set (through
tools/fetch_openhab_icons.py, which owns the listing, the download and the
rasterizing), reduces each icon to a 16-colour palette and writes the lot out
as LVGL indexed image records -- a 16-entry palette of {blue, green, red,
alpha} bytes, then the pixels packed two per byte, high nibble first -- as one
blob plus a sorted name table in

    main/icons/icon_set_data.h

A record, not a PNG: the panel is the opposite of flash-poor, and what a PNG
costs is RAM -- lodepng's working set for the decode plus a 4096-byte ARGB8888
held per tile, on a heap that also feeds BLE and MQTT. An LVGL indexed image
is what the renderer reads palette and pixels from directly: no decode, no
transient, and 576 bytes held per icon instead of 4096. The hundred-odd bytes
more flash per icon are the cheap side of that trade.

Sixteen colours is not a compromise for this artwork. The classic icons are
line art: a handful of flat colours and an antialiased edge. What the palette
has to hold is mostly alpha steps, which is why the quantizer works in RGBA
rather than RGB and why fully transparent pixels are given a palette entry of
their own -- averaging them in with the edge is what produces a halo.

    tools/build_icon_set.py                     # the whole set, 32x32
    tools/build_icon_set.py --size 24
    tools/build_icon_set.py light heating       # just these, with state variants

The generated header is NOT committed, for the reason the other two icon tools
give: the classic icon set is copyright the openHAB project and licensed under
the EPL-2.0, which this project's GPL-3.0 cannot take in. Without it the build
is exactly what it was before -- every icon comes from the server over HTTP.

Requires network access and one of inkscape, rsvg-convert or ImageMagick. The
PNG work is done here in stdlib Python (zlib and nothing else), so there is no
Pillow or pngquant to install.

See also:
  tools/fetch_openhab_icons.py  the same set as plain PNG files, to look at
  tools/fetch_sim_icons.py      the simulator's offline fixture, as C arrays
"""

import argparse
import importlib.util
import os
import struct
import subprocess
import shutil
import sys
import tempfile
import zlib

DEFAULT_OUTPUT = os.path.join("main", "icons", "icon_set_data.h")

PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"

# Channels per colour type, indexed by the PNG colour type itself. The gaps are
# the values PNG does not define.
CHANNELS = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}


def load_fetcher():
    """Import tools/fetch_openhab_icons.py as a module.

    By path rather than by name: this script is run as tools/build_icon_set.py
    from the project root, so the tools directory is not on sys.path and a
    plain import would only work when the cwd happened to be right.
    """
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                        "fetch_openhab_icons.py")
    spec = importlib.util.spec_from_file_location("fetch_openhab_icons", path)

    if spec is None or spec.loader is None:
        sys.exit("Cannot load %s" % path)

    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)

    return module


# ------------------------------------------------------------------ PNG input

def png_chunks(data):
    """Yield (type, body) for every chunk, in file order."""
    if data[:8] != PNG_SIGNATURE:
        raise ValueError("not a PNG")

    offset = 8

    while offset + 8 <= len(data):
        (length,) = struct.unpack(">I", data[offset:offset + 4])
        chunk_type = data[offset + 4:offset + 8]

        yield chunk_type, data[offset + 8:offset + 8 + length]

        # 4 length + 4 type + body + 4 CRC. The CRC is not checked: the file was
        # written by the rasterizer we just ran, two lines ago, into a directory
        # we made.
        offset += 12 + length


def paeth(a, b, c):
    p = a + b - c
    pa = abs(p - a)
    pb = abs(p - b)
    pc = abs(p - c)

    if pa <= pb and pa <= pc:
        return a
    if pb <= pc:
        return b

    return c


def unfilter(raw, height, stride, bpp):
    """Undo the per-row filters. `bpp` is the filter's byte step, at least 1."""
    out = bytearray()
    prev = bytearray(stride)
    pos = 0

    for row in range(height):
        if pos + 1 + stride > len(raw):
            raise ValueError("truncated image data at row %d" % row)

        filter_type = raw[pos]
        pos += 1
        line = bytearray(raw[pos:pos + stride])
        pos += stride

        if filter_type == 1:                            # Sub
            for i in range(bpp, stride):
                line[i] = (line[i] + line[i - bpp]) & 0xFF
        elif filter_type == 2:                          # Up
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 0xFF
        elif filter_type == 3:                          # Average
            for i in range(stride):
                left = line[i - bpp] if i >= bpp else 0
                line[i] = (line[i] + ((left + prev[i]) >> 1)) & 0xFF
        elif filter_type == 4:                          # Paeth
            for i in range(stride):
                left = line[i - bpp] if i >= bpp else 0
                upper_left = prev[i - bpp] if i >= bpp else 0
                line[i] = (line[i] + paeth(left, prev[i], upper_left)) & 0xFF
        elif filter_type != 0:
            raise ValueError("unknown filter type %d on row %d" % (filter_type, row))

        out += line
        prev = line

    return out


def unpack_samples(line, width, channels, bit_depth):
    """One row of raw bytes to a list of 8-bit samples."""
    if bit_depth == 8:
        return list(line[:width * channels])

    if bit_depth == 16:
        # The low byte of each sample is dropped: the icons are 8-bit artwork
        # upsampled by the rasterizer at worst, and the firmware draws at 8.
        return [line[i] for i in range(0, width * channels * 2, 2)]

    # Sub-byte depths, which only ever arrive for a palette or greyscale image.
    per_byte = 8 // bit_depth
    mask = (1 << bit_depth) - 1
    samples = []

    for index in range(width * channels):
        byte = line[index // per_byte]
        shift = 8 - bit_depth - (index % per_byte) * bit_depth
        samples.append((byte >> shift) & mask)

    return samples


def png_decode(data):
    """Decode a PNG to (width, height, RGBA bytes).

    Enough of the format for what a rasterizer emits, and no more: every colour
    type, bit depths 1 to 16, and no interlacing. Adam7 is rejected rather than
    handled -- neither inkscape nor rsvg-convert nor ImageMagick produces it
    unasked, and a silent wrong answer would be worse than stopping.
    """
    width = height = bit_depth = color_type = interlace = None
    palette = b""
    transparency = b""
    idat = bytearray()

    for chunk_type, body in png_chunks(data):
        if chunk_type == b"IHDR":
            (width, height, bit_depth, color_type,
             compression, filter_method, interlace) = struct.unpack(">IIBBBBB", body)

            if compression != 0 or filter_method != 0:
                raise ValueError("unsupported compression or filter method")
            if interlace != 0:
                raise ValueError("interlaced PNG")
            if color_type not in CHANNELS:
                raise ValueError("unknown colour type %d" % color_type)
        elif chunk_type == b"PLTE":
            palette = body
        elif chunk_type == b"tRNS":
            transparency = body
        elif chunk_type == b"IDAT":
            idat += body
        elif chunk_type == b"IEND":
            break

    if width is None:
        raise ValueError("no IHDR")

    channels = CHANNELS[color_type]
    stride = (width * channels * bit_depth + 7) // 8
    # The filter works on whole bytes, and on at least one: for anything below
    # 8 bits per pixel the step is a single byte.
    bpp = max(1, (channels * bit_depth) // 8)

    raw = unfilter(zlib.decompress(bytes(idat)), height, stride, bpp)

    rgba = bytearray(width * height * 4)
    # Greyscale below 8 bits is stored at full range, so a 4-bit 0x0F is white.
    grey_scale = 255 // ((1 << bit_depth) - 1) if bit_depth < 8 else 1

    for y in range(height):
        samples = unpack_samples(raw[y * stride:(y + 1) * stride], width,
                                 channels, bit_depth)

        for x in range(width):
            out = (y * width + x) * 4
            base = x * channels

            if color_type == 6:
                rgba[out:out + 4] = bytes(samples[base:base + 4])
            elif color_type == 2:
                rgba[out:out + 3] = bytes(samples[base:base + 3])
                rgba[out + 3] = 255
            elif color_type == 4:
                grey = samples[base] * (grey_scale if bit_depth < 8 else 1)
                rgba[out] = rgba[out + 1] = rgba[out + 2] = grey
                rgba[out + 3] = samples[base + 1]
            elif color_type == 0:
                grey = samples[base] * grey_scale
                rgba[out] = rgba[out + 1] = rgba[out + 2] = grey
                rgba[out + 3] = 255
            else:                                       # colour type 3
                index = samples[base]

                if (index + 1) * 3 > len(palette):
                    raise ValueError("palette index %d is outside PLTE" % index)

                rgba[out:out + 3] = palette[index * 3:index * 3 + 3]
                rgba[out + 3] = transparency[index] if index < len(transparency) else 255

    return width, height, rgba


# ----------------------------------------------------------------- quantizing

def widest_channel(box):
    """The RGBA channel the colours in `box` are furthest apart on."""
    widest = 0
    widest_extent = -1

    for channel in range(4):
        values = [color[channel] for color, _ in box]
        extent = max(values) - min(values)

        if extent > widest_extent:
            widest_extent = extent
            widest = channel

    return widest, widest_extent


def split_box(box):
    """Cut `box` in two at the median, by pixel count rather than by colour.

    Weighting by count is what keeps a colour that covers half the icon from
    sharing a palette entry with one that covers four pixels.
    """
    channel, _ = widest_channel(box)
    ordered = sorted(box, key=lambda item: item[0][channel])
    half = sum(count for _, count in ordered) // 2
    running = 0

    for index, (_, count) in enumerate(ordered):
        running += count

        if running >= half and index + 1 < len(ordered):
            return [ordered[:index + 1], ordered[index + 1:]]

    # Everything is in the first colour bar the last. Both halves are non-empty,
    # which is what the caller needs; len(box) >= 2 is checked before we get here.
    return [ordered[:-1], ordered[-1:]]


def median_cut(entries, max_boxes):
    """Partition (colour, count) pairs into at most max_boxes groups."""
    if not entries or max_boxes < 1:
        return []

    boxes = [entries]

    while len(boxes) < max_boxes:
        target = -1
        target_score = 0

        for index, box in enumerate(boxes):
            if len(box) < 2:
                continue

            # How much is lost by leaving this box whole: how far apart its
            # colours are, over how many pixels wear the difference.
            _, extent = widest_channel(box)
            score = extent * sum(count for _, count in box)

            if score > target_score:
                target_score = score
                target = index

        if target < 0:                                  # nothing left to split
            break

        boxes.extend(split_box(boxes.pop(target)))

    return boxes


def box_average(box):
    """The colour that stands for a box: its pixel-weighted mean.

    A box holding one colour averages to exactly that colour, which is what
    makes an icon with 16 colours or fewer come through untouched.
    """
    total = sum(count for _, count in box)
    result = []

    for channel in range(4):
        weighted = sum(color[channel] * count for color, count in box)
        result.append((weighted + total // 2) // total)

    return tuple(result)


def quantize(rgba, max_colors):
    """Reduce RGBA bytes to a palette of at most max_colors and an index array.

    Returns (palette, indices, transparent_index); transparent_index is None
    when the image has no fully transparent pixel.
    """
    counts = {}

    for offset in range(0, len(rgba), 4):
        color = (rgba[offset], rgba[offset + 1], rgba[offset + 2], rgba[offset + 3])
        counts[color] = counts.get(color, 0) + 1

    # Every fully transparent pixel is the same pixel whatever colour it claims
    # to be, so they collapse into one entry that the quantizer never sees. Two
    # things come of that: the edge does not get a halo from transparent black
    # being averaged into it, and a 15-colour budget is spent entirely on
    # colours that are actually visible.
    has_transparent = any(color[3] == 0 for color in counts)
    opaque = [(color, count) for color, count in counts.items() if color[3] != 0]

    budget = max_colors - (1 if has_transparent else 0)
    palette = [box_average(box) for box in median_cut(opaque, budget)]

    # Sorted by alpha so that everything the tRNS chunk has to describe sits at
    # the front of the palette and the chunk can stop early. The transparent
    # entry sorts to index 0 on its own, since no other entry has alpha 0.
    palette.sort(key=lambda color: (color[3], color[:3]))

    if has_transparent:
        palette.insert(0, (0, 0, 0, 0))

    transparent_index = 0 if has_transparent else None
    first_opaque = 1 if has_transparent else 0

    if not palette:                                     # a fully empty icon
        palette = [(0, 0, 0, 0)]
        transparent_index = 0

    nearest = {}
    indices = []

    for offset in range(0, len(rgba), 4):
        color = (rgba[offset], rgba[offset + 1], rgba[offset + 2], rgba[offset + 3])

        if color[3] == 0:
            indices.append(transparent_index)
            continue

        index = nearest.get(color)

        if index is None:
            best = first_opaque
            best_distance = None

            # Deliberately never the transparent entry: a barely visible pixel
            # is nearer to it than to anything else, and letting it snap there
            # is how an antialiased edge loses its outermost ring.
            for candidate in range(first_opaque, len(palette)):
                entry = palette[candidate]
                distance = sum((color[c] - entry[c]) ** 2 for c in range(4))

                if best_distance is None or distance < best_distance:
                    best_distance = distance
                    best = candidate

            index = best
            nearest[color] = index

        indices.append(index)

    return palette, indices, transparent_index


# ----------------------------------------------------------------- PNG output

def i4_record(width, height, palette, indices):
    """Write one icon as an LVGL I4 record: the palette as sixteen {blue,
    green, red, alpha} entries -- the order lv_color32_t has in memory, zero
    padded when the icon needs fewer -- then the pixels packed two per byte,
    high nibble first, which is LVGL's I4 order. Fixed size for a given
    --size, so the firmware recognises a record by its length alone: a PNG of
    the same image would carry its 0x89 signature instead."""
    if width % 2 != 0:
        raise ValueError("an odd width cannot pack two pixels per byte")

    if len(palette) > 16:
        raise ValueError("%d colours do not fit in 4 bits" % len(palette))

    record = bytearray()

    for color in palette:
        red, green, blue, alpha = color
        record += bytes((blue, green, red, alpha))

    record += bytes(4 * (16 - len(palette)))

    for y in range(height):
        row = indices[y * width:(y + 1) * width]

        for x in range(0, width, 2):
            record.append((row[x] << 4) | row[x + 1])

    return bytes(record)


def i4_decode(width, height, record):
    """Read a record back to RGBA, sharing no code with the writer beyond the
    format comment above. Exists for verify_roundtrip."""
    if len(record) != 64 + width * height // 2:
        raise ValueError("record is %d bytes, not %d"
                         % (len(record), 64 + width * height // 2))

    rgba = bytearray(width * height * 4)

    for y in range(height):
        for x in range(width):
            packed = record[64 + y * (width // 2) + x // 2]
            index = packed >> 4 if x % 2 == 0 else packed & 0x0F
            blue, green, red, alpha = record[index * 4:index * 4 + 4]
            offset = (y * width + x) * 4
            rgba[offset:offset + 4] = bytes((red, green, blue, alpha))

    return rgba


def chunk(chunk_type, body):
    return (struct.pack(">I", len(body)) + chunk_type + body
            + struct.pack(">I", zlib.crc32(chunk_type + body) & 0xFFFFFFFF))


def png_encode_indexed(width, height, palette, indices, bit_depth):
    """Write an indexed PNG at 1, 2 or 4 bits per pixel."""
    if len(palette) > (1 << bit_depth):
        raise ValueError("%d colours do not fit in %d bit(s)" % (len(palette), bit_depth))

    header = struct.pack(">IIBBBBB", width, height, bit_depth, 3, 0, 0, 0)

    plte = b"".join(bytes(color[:3]) for color in palette)

    # Only as far as the last entry that is not fully opaque; PNG takes every
    # entry the chunk does not mention as opaque.
    last_translucent = -1

    for index, color in enumerate(palette):
        if color[3] != 255:
            last_translucent = index

    trns = bytes(color[3] for color in palette[:last_translucent + 1])

    per_byte = 8 // bit_depth
    row_bytes = (width * bit_depth + 7) // 8
    raw = bytearray()

    for y in range(height):
        # Filter type 0. The others exist to make neighbouring *bytes* similar,
        # which for packed palette indices means comparing pixel pairs -- it
        # costs a byte a row here and saves nothing measurable on 32x32 line art.
        raw.append(0)
        packed = bytearray(row_bytes)

        for x in range(width):
            shift = 8 - bit_depth - (x % per_byte) * bit_depth
            packed[x * bit_depth // 8] |= indices[y * width + x] << shift

        raw += packed

    body = [chunk(b"IHDR", header), chunk(b"PLTE", plte)]

    if trns:
        body.append(chunk(b"tRNS", trns))

    body.append(chunk(b"IDAT", zlib.compress(bytes(raw), 9)))
    body.append(chunk(b"IEND", b""))

    return PNG_SIGNATURE + b"".join(body)


# ----------------------------------------------------------------- verifying

def expected_rgba(width, height, palette, indices):
    out = bytearray(width * height * 4)

    for pixel, index in enumerate(indices):
        out[pixel * 4:pixel * 4 + 4] = bytes(palette[index])

    return out


def verify_roundtrip(name, record, width, height, palette, indices):
    """Decode the record just written and check it is the quantized image."""
    got_rgba = i4_decode(width, height, record)

    if bytes(got_rgba) != bytes(expected_rgba(width, height, palette, indices)):
        raise ValueError("%s: re-read pixels differ from the quantized image" % name)


def verify_external(name, width, height, palette, indices, magick):
    """The same check through ImageMagick, which shares no code with this file.

    Worth the extra process for at least one icon: a round trip through one
    decoder cannot catch a convention this file has wrong at both ends. The
    record itself has no independent reader, so what ImageMagick gets is the
    same palette and indices re-encoded as a PNG -- which pins the colours and
    the pixel stream, the things a wrong quantizer would corrupt. What that
    leaves uncovered is the record's own conventions -- BGRA order, nibble
    order -- and those have exactly one reader that matters: the firmware's
    renderer, where a wrong one is not subtle.
    """
    png = png_encode_indexed(width, height, palette, indices, 4)

    result = subprocess.run([magick, "png:-", "-depth", "8", "RGBA:-"],
                            input=png, stdout=subprocess.PIPE,
                            stderr=subprocess.DEVNULL, check=True)

    expected = bytes(expected_rgba(width, height, palette, indices))

    if len(result.stdout) != len(expected):
        raise ValueError("%s: ImageMagick read %d bytes where %d were written"
                         % (name, len(result.stdout), len(expected)))

    for pixel in range(width * height):
        offset = pixel * 4

        if result.stdout[offset + 3] != expected[offset + 3]:
            raise ValueError("%s: alpha differs at pixel %d" % (name, pixel))

        # The colour under a fully transparent pixel means nothing, and
        # ImageMagick is entitled to report it as anything it likes.
        if expected[offset + 3] == 0:
            continue

        if result.stdout[offset:offset + 3] != expected[offset:offset + 3]:
            raise ValueError("%s: colour differs at pixel %d -- ImageMagick reads "
                             "this PNG differently than we wrote it" % (name, pixel))


# ------------------------------------------------------------------- emitting

def c_bytes(data, per_line=16):
    lines = []

    for offset in range(0, len(data), per_line):
        chunk_data = data[offset:offset + per_line]
        lines.append("    " + "".join("0x%02x," % byte for byte in chunk_data))

    return "\n".join(lines)


def emit(path, icons, size, source_ref):
    """Write the generated header: one blob, and a table sorted by name."""
    icons = sorted(icons, key=lambda item: item[0])

    blob = bytearray()
    table = []

    for name, record in icons:
        if len(record) > 0xFFFF:
            sys.exit("%s is %d bytes, which the table's 16-bit size cannot hold"
                     % (name, len(record)))

        table.append((name, len(blob), len(record)))
        blob += record

    directory = os.path.dirname(path)

    if directory:
        os.makedirs(directory, exist_ok=True)

    with open(path, "w") as out:
        out.write("/*\n")
        out.write(" * Generated by tools/build_icon_set.py -- do not edit, do not commit.\n")
        out.write(" *\n")
        out.write(" * %dx%d renderings of the openHAB classic icon set (openhab-webui@%s),\n"
                  % (size, size, source_ref))
        out.write(" * quantized to 16 colours and stored as LVGL indexed images: a 16-entry\n")
        out.write(" * palette of {blue, green, red, alpha} bytes, then the pixels packed two\n")
        out.write(" * per byte, high nibble first. The set is copyright the openHAB project\n")
        out.write(" * and licensed under the EPL-2.0: https://github.com/openhab/openhab-webui\n")
        out.write(" *\n")
        out.write(" * That license is incompatible with this project's GPL-3.0, which is why\n")
        out.write(" * this file is generated locally and excluded from the repository.\n")
        out.write(" *\n")
        out.write(" * The table is sorted by name, by plain byte value, because icon_set.cpp\n")
        out.write(" * binary searches it and walks the run of state variants sharing a prefix.\n")
        out.write(" */\n\n")
        out.write("#ifndef ICON_SET_DATA_H\n")
        out.write("#define ICON_SET_DATA_H\n\n")
        out.write("#define ICON_SET_GENERATED 1\n")
        out.write("#define ICON_SET_PIXEL_SIZE %d\n" % size)
        out.write("#define ICON_SET_COUNT %d\n" % len(table))
        out.write("#define ICON_SET_BLOB_SIZE %d\n\n" % len(blob))

        out.write("static const unsigned char icon_set_blob[ICON_SET_BLOB_SIZE] = {\n")
        out.write(c_bytes(blob))
        out.write("\n};\n\n")

        out.write("static const struct\n{\n")
        out.write("    const char    *name;\n")
        out.write("    unsigned int   offset;\n")
        out.write("    unsigned short size;\n")
        out.write("} icon_set_table[ICON_SET_COUNT] = {\n")

        for name, offset, length in table:
            out.write("    { \"%s\", %du, %du },\n" % (name, offset, length))

        out.write("};\n\n")
        out.write("#endif /* ICON_SET_DATA_H */\n")

    return len(blob)


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("names", nargs="*", metavar="ICON",
                        help="icons to include, state variants included "
                             "(default: the whole set)")
    parser.add_argument("--size", type=int, default=32,
                        help="icon edge length in pixels (default: 32, the size "
                             "the firmware draws tile icons at)")
    parser.add_argument("--colors", type=int, default=16,
                        help="palette entries per icon (default: 16)")
    parser.add_argument("--output", default=DEFAULT_OUTPUT,
                        help="generated header (default: %s)" % DEFAULT_OUTPUT)
    parser.add_argument("--ref", default="main",
                        help="openhab-webui git ref to fetch from (default: main)")
    parser.add_argument("--jobs", type=int, default=8,
                        help="parallel downloads (default: 8)")
    parser.add_argument("--max-bytes", type=int, default=4096,
                        help="drop an icon larger than this (default: 4096)")
    parser.add_argument("--verify-all", action="store_true",
                        help="cross-check every icon against ImageMagick, not "
                             "just the first")
    args = parser.parse_args()

    if args.size < 1:
        sys.exit("--size must be at least 1.")
    if args.size % 2 != 0:
        sys.exit("--size must be even: a record packs two pixels per byte.")
    if not 2 <= args.colors <= 16:
        sys.exit("--colors must be between 2 and 16.")

    fetcher = load_fetcher()

    rasterizer_name, rasterize = fetcher.find_rasterizer()
    base_url = fetcher.RAW_BASE_URL % args.ref

    names = fetcher.list_icon_names(args.ref)

    if args.names:
        names = fetcher.select(names, args.names)

    print("fetching %d icon(s) from openhab-webui@%s, rasterizing with %s at %dx%d"
          % (len(names), args.ref, rasterizer_name, args.size, args.size))

    magick = shutil.which("magick") or shutil.which("convert")

    if magick is None:
        print("note: no ImageMagick, so the encoder is only checked against "
              "this script's own decoder")

    icons = []
    failures = []
    verified = 0

    with tempfile.TemporaryDirectory() as tmpdir:
        downloaded, download_failures = fetcher.download(names, base_url, tmpdir, args.jobs)
        failures.extend(download_failures)

        if downloaded:
            rasterize(downloaded, tmpdir, args.size, args.jobs)

        converted, rasterizer_failures = fetcher.collect(downloaded, tmpdir, False)
        failures.extend(rasterizer_failures)

        for name, _ in converted:
            with open(os.path.join(tmpdir, name + ".png"), "rb") as png_file:
                source = png_file.read()

            try:
                width, height, rgba = png_decode(source)
                palette, indices, _ = quantize(rgba, args.colors)
                record = i4_record(width, height, palette, indices)

                verify_roundtrip(name, record, width, height, palette, indices)

                if magick is not None and (args.verify_all or verified == 0):
                    verify_external(name, width, height, palette, indices, magick)
                    verified += 1
            except Exception as exc:                    # noqa: BLE001 - per icon
                failures.append((name, "%s" % exc))
                continue

            if len(record) > args.max_bytes:
                failures.append((name, "%d bytes exceeds --max-bytes=%d"
                                 % (len(record), args.max_bytes)))
                continue

            icons.append((name, record))

    if not icons:
        sys.exit("No icons were generated.")

    total = emit(args.output, icons, args.size, args.ref)

    print("wrote %s: %d icons, %d bytes (%.1f kB), %d bytes each"
          % (args.output, len(icons), total, total / 1024.0,
             total // len(icons)))
    print("verified %d icon(s) against ImageMagick" % verified)
    print("\nRun `idf.py reconfigure` (or a plain build) so CMake notices the header.")

    for name, error in sorted(failures):
        print("  %-24s FAILED (%s)" % (name, error), file=sys.stderr)

    if failures:
        sys.exit("%d icon(s) failed." % len(failures))


if __name__ == "__main__":
    main()
