#!/usr/bin/env python3
"""Generate the simulator's icon data from the openHAB classic icon set.

The simulator has no HTTP client, so it cannot fetch widget icons from an
openHAB server the way the firmware does. This script downloads the icons it
needs, rasterizes them to PNG and writes them out as C arrays that
main/sim/icon_fixture.cpp picks up.

The generated header is NOT committed: the openHAB classic icon set is licensed
under the EPL-2.0, which is incompatible with this project's GPL-3.0, so the
artwork must not become part of this repository. Run this script locally to get
the icons; without it the simulator simply shows no icons.

    tools/fetch_sim_icons.py

Requires network access and one of inkscape, rsvg-convert or ImageMagick.
"""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
import urllib.request

ICON_BASE_URL = (
    "https://raw.githubusercontent.com/openhab/openhab-webui/main/bundles/"
    "org.openhab.ui.iconset.classic/src/main/resources/icons/"
)

# The icons referenced by main/sim/sitemap_fixture.cpp, plus the on/off variants
# openHAB would serve for a switchable item. Numeric states (a dimmer at 40, a
# rollershutter at 30) fall back to the base icon; openHAB itself picks the
# nearest available step, which is not reproduced here.
ICON_NAMES = [
    "bedroom",
    "colorpicker",
    "heating",
    "humidity",
    "light",
    "light-off",
    "light-on",
    "receiver",
    "receiver-off",
    "receiver-on",
    "rollershutter",
    "settings",
    "slider",
    "sofa",
    "temperature",
    "text",
]

OUTPUT_PATH = os.path.join("main", "sim", "icon_fixture_data.h")


def find_rasterizer():
    """Return a function converting an SVG file to a PNG file of the given size."""
    if shutil.which("inkscape"):
        def inkscape(svg, png, size):
            subprocess.run(
                ["inkscape", svg, "-w", str(size), "-h", str(size), "-o", png],
                check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            )
        return "inkscape", inkscape

    if shutil.which("rsvg-convert"):
        def rsvg(svg, png, size):
            subprocess.run(
                ["rsvg-convert", svg, "-w", str(size), "-h", str(size), "-o", png],
                check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            )
        return "rsvg-convert", rsvg

    magick = shutil.which("magick") or shutil.which("convert")
    if magick:
        # ImageMagick's own SVG renderer handles the gradients in these icons
        # poorly; it is the last resort.
        def im(svg, png, size):
            subprocess.run(
                [magick, "-background", "none", svg, "-resize", "%dx%d" % (size, size), png],
                check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            )
        return magick, im

    sys.exit(
        "No SVG rasterizer found. Install one of:\n"
        "  Debian/Ubuntu: sudo apt install inkscape\n"
        "                 sudo apt install librsvg2-bin   (rsvg-convert)\n"
    )


def c_array(data):
    """Format bytes as the body of a C array initializer."""
    lines = []
    for offset in range(0, len(data), 12):
        chunk = data[offset:offset + 12]
        lines.append("    " + " ".join("0x%02x," % byte for byte in chunk))
    return "\n".join(lines)


def c_identifier(name):
    return "sim_icon_" + name.replace("-", "_")


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--size", type=int, default=32,
                        help="icon edge length in pixels (default: 32)")
    parser.add_argument("--max-bytes", type=int, default=5000,
                        help="reject icons larger than this; must stay within "
                             "ICON_PNG_BUFFER_SIZE in main/openhab_ui.cpp "
                             "(default: 5000)")
    parser.add_argument("--output", default=OUTPUT_PATH,
                        help="generated header (default: %s)" % OUTPUT_PATH)
    args = parser.parse_args()

    rasterizer_name, rasterize = find_rasterizer()
    print("rasterizing with %s at %dx%d" % (rasterizer_name, args.size, args.size))

    icons = []

    with tempfile.TemporaryDirectory() as tmpdir:
        for name in ICON_NAMES:
            svg_path = os.path.join(tmpdir, name + ".svg")
            png_path = os.path.join(tmpdir, name + ".png")

            url = ICON_BASE_URL + name + ".svg"
            try:
                with urllib.request.urlopen(url, timeout=30) as response:
                    svg_data = response.read()
            except Exception as exc:                 # noqa: BLE001 - report and continue
                print("  %-16s SKIPPED (%s)" % (name, exc))
                continue

            with open(svg_path, "wb") as svg_file:
                svg_file.write(svg_data)

            try:
                rasterize(svg_path, png_path, args.size)
            except subprocess.CalledProcessError as exc:
                print("  %-16s SKIPPED (rasterizing failed: %s)" % (name, exc))
                continue

            with open(png_path, "rb") as png_file:
                png_data = png_file.read()

            if len(png_data) > args.max_bytes:
                print("  %-16s SKIPPED (%d bytes exceeds --max-bytes=%d)"
                      % (name, len(png_data), args.max_bytes))
                continue

            print("  %-16s %d bytes" % (name, len(png_data)))
            icons.append((name, png_data))

    if not icons:
        sys.exit("No icons were generated.")

    os.makedirs(os.path.dirname(args.output), exist_ok=True)

    with open(args.output, "w") as out:
        out.write("/*\n")
        out.write(" * Generated by tools/fetch_sim_icons.py -- do not edit, do not commit.\n")
        out.write(" *\n")
        out.write(" * %dx%d PNG renderings of the openHAB classic icon set, which is\n"
                  % (args.size, args.size))
        out.write(" * copyright the openHAB project and licensed under the EPL-2.0:\n")
        out.write(" * https://github.com/openhab/openhab-webui\n")
        out.write(" *\n")
        out.write(" * That license is incompatible with this project's GPL-3.0, which is why\n")
        out.write(" * this file is generated locally and excluded from the repository.\n")
        out.write(" */\n\n")
        out.write("#ifndef SIM_ICON_FIXTURE_DATA_H\n")
        out.write("#define SIM_ICON_FIXTURE_DATA_H\n\n")
        out.write("#define SIM_ICON_FIXTURE_GENERATED 1\n\n")

        for name, png_data in icons:
            out.write("static const unsigned char %s[] = {\n%s\n};\n\n"
                      % (c_identifier(name), c_array(png_data)))

        out.write("static const struct\n{\n")
        out.write("    const char *name;\n")
        out.write("    const unsigned char *data;\n")
        out.write("    unsigned int size;\n")
        out.write("} sim_icon_table[] = {\n")
        for name, _ in icons:
            identifier = c_identifier(name)
            out.write("    { \"%s\", %s, sizeof(%s) },\n" % (name, identifier, identifier))
        out.write("};\n\n")
        out.write("#endif /* SIM_ICON_FIXTURE_DATA_H */\n")

    total = sum(len(data) for _, data in icons)
    print("wrote %s: %d icons, %d bytes of PNG data" % (args.output, len(icons), total))


if __name__ == "__main__":
    main()
