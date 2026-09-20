#!/usr/bin/env python3
"""Download the openHAB classic icon set and rasterize every icon to PNG.

openHAB ships its built-in icons as SVG (see
https://www.openhab.org/docs/configuration/items.html#icons); the firmware asks
the server for PNG instead, because that is what LVGL can decode. This script
produces the same PNGs locally -- for looking at what an icon name resolves to,
for filling an /icon/ mock, or for feeding a sprite sheet -- without needing a
running openHAB.

The set follows openHAB's naming rules: lowercase letters, digits, hyphens and
underscores, with state variants named "<icon>-<state>.svg" next to the
mandatory default "<icon>.svg". Both land in the output directory under their
own names, so "light.png", "light-on.png" and "light-off.png" sit side by side
exactly as openHAB would serve them.

    tools/fetch_openhab_icons.py                    # all icons, 32x32
    tools/fetch_openhab_icons.py --size 64          # bigger
    tools/fetch_openhab_icons.py light heating      # just these, with variants

The output is NOT committed: the classic icon set is copyright the openHAB
project and licensed under the EPL-2.0, which is incompatible with this
project's GPL-3.0, so the artwork must not become part of this repository. The
default output directory is listed in .gitignore for that reason.

Requires network access and one of inkscape, rsvg-convert or ImageMagick.

See also tools/fetch_sim_icons.py, which fetches the handful of icons the
simulator's fixture needs and writes them out as C arrays instead.
"""

import argparse
import concurrent.futures
import json
import os
import shutil
import subprocess
import sys
import urllib.error
import urllib.request

REPO_API_URL = (
    "https://api.github.com/repos/openhab/openhab-webui/contents/bundles/"
    "org.openhab.ui.iconset.classic/src/main/resources/icons"
)

RAW_BASE_URL = (
    "https://raw.githubusercontent.com/openhab/openhab-webui/%s/bundles/"
    "org.openhab.ui.iconset.classic/src/main/resources/icons/"
)

DEFAULT_OUTPUT_DIR = "openhab-icons"


def fetch(url, timeout=30):
    request = urllib.request.Request(url, headers={"User-Agent": "oh-ez-touch-icon-fetch"})
    with urllib.request.urlopen(request, timeout=timeout) as response:
        return response.read()


def list_icon_names(ref):
    """Every icon in the set, without the .svg extension.

    Read from the GitHub contents API rather than hardcoded: the set grows
    between openHAB releases, and a list in here would quietly go stale. The
    directory also holds a couple of maintenance shell scripts, which the
    extension filter drops.
    """
    try:
        entries = json.loads(fetch(REPO_API_URL + "?ref=" + ref).decode("utf-8"))
    except urllib.error.HTTPError as exc:
        # 403 here is almost always the unauthenticated rate limit, which means
        # "try again in an hour", not "the icons moved".
        sys.exit("Cannot list the icon set: %s\n%s" % (exc, REPO_API_URL))
    except Exception as exc:                        # noqa: BLE001 - report and stop
        sys.exit("Cannot list the icon set: %s" % exc)

    names = sorted(entry["name"][:-len(".svg")] for entry in entries
                   if entry["type"] == "file" and entry["name"].endswith(".svg"))

    if not names:
        sys.exit("The icon set listing came back empty; has the path moved?")

    return names


def select(names, wanted):
    """Keep the named icons plus their state variants ("light" -> "light-on").

    An unknown name is worth stopping for: it is nearly always a typo, and
    silently producing nothing for it is the unhelpful answer.
    """
    selected = []
    for name in wanted:
        matches = [candidate for candidate in names
                   if candidate == name or candidate.startswith(name + "-")]
        if not matches:
            sys.exit("No icon named %r in the classic icon set." % name)
        selected.extend(matches)

    return sorted(set(selected))


def download(names, base_url, out_dir, jobs):
    """Fetch every named SVG into out_dir. Returns (downloaded, failures)."""
    def one(name):
        try:
            data = fetch(base_url + name + ".svg")
        except Exception as exc:                    # noqa: BLE001 - reported per icon
            return name, "download failed: %s" % exc

        with open(os.path.join(out_dir, name + ".svg"), "wb") as svg_file:
            svg_file.write(data)

        return name, None

    downloaded = []
    failures = []

    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
        for name, error in pool.map(one, names):
            if error is None:
                downloaded.append(name)
            else:
                failures.append((name, error))

    return downloaded, failures


def rasterize_inkscape(names, out_dir, size, jobs):
    """Convert through one inkscape --shell process.

    Not a process per icon, and `jobs` is deliberately ignored: inkscape 1.4
    aborts when several copies start at once (separate profile directories and
    --app-id-tag do not help), and its startup alone is most of the cost of a
    single conversion. One process fed every file avoids both problems.

    The commands are run with cwd=out_dir and bare file names, because the
    shell splits actions on ';' and ':' -- an output path containing either
    would be torn apart. Icon names cannot contain them.
    """
    script = "".join(
        "file-open:%s.svg; export-width:%d; export-height:%d; "
        "export-filename:%s.png; export-do; file-close\n" % (name, size, size, name)
        for name in names)

    subprocess.run(["inkscape", "--shell"], input=script.encode(), cwd=out_dir,
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def rasterize_with(command_for):
    """Build a rasterizer that runs one process per icon, `jobs` at a time."""
    def rasterize(names, out_dir, size, jobs):
        def one(name):
            svg = os.path.join(out_dir, name + ".svg")
            png = os.path.join(out_dir, name + ".png")
            # A bad exit status is not inspected: collect() decides what
            # worked by looking for the PNG, which is the only answer the
            # batched inkscape path can give, and one rule beats two.
            subprocess.run(command_for(svg, png, size), check=False,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

        with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
            list(pool.map(one, names))

    return rasterize


def find_rasterizer():
    """Return (name, fn) where fn converts a list of downloaded SVGs to PNG.

    Preference order is quality first: inkscape and rsvg-convert both render
    the gradients in these icons correctly, ImageMagick's built-in SVG renderer
    does so poorly and is only the last resort.
    """
    if shutil.which("inkscape"):
        return "inkscape", rasterize_inkscape

    if shutil.which("rsvg-convert"):
        return "rsvg-convert", rasterize_with(
            lambda svg, png, size: ["rsvg-convert", svg, "-w", str(size),
                                    "-h", str(size), "-o", png])

    magick = shutil.which("magick") or shutil.which("convert")
    if magick:
        return magick, rasterize_with(
            lambda svg, png, size: [magick, "-background", "none", svg,
                                    "-resize", "%dx%d" % (size, size), png])

    sys.exit(
        "No SVG rasterizer found. Install one of:\n"
        "  Debian/Ubuntu: sudo apt install inkscape\n"
        "                 sudo apt install librsvg2-bin   (rsvg-convert)\n"
        "                 sudo apt install imagemagick\n"
    )


def collect(names, out_dir, keep_svg):
    """Sort the converted icons from the ones that produced nothing.

    Checked by looking at the files rather than at exit statuses: the batch
    rasterizer has only one status for the whole run, so the PNG being there
    and non-empty is the only per-icon answer available.
    """
    converted = []
    failures = []

    for name in names:
        png_path = os.path.join(out_dir, name + ".png")

        if os.path.exists(png_path) and os.path.getsize(png_path) > 0:
            converted.append((name, os.path.getsize(png_path)))
        else:
            failures.append((name, "rasterizing produced no PNG"))
            if os.path.exists(png_path):
                os.remove(png_path)

        if not keep_svg:
            svg_path = os.path.join(out_dir, name + ".svg")
            if os.path.exists(svg_path):
                os.remove(svg_path)

    return converted, failures


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("names", nargs="*", metavar="ICON",
                        help="icons to fetch, state variants included "
                             "(default: the whole set)")
    parser.add_argument("--size", type=int, default=32,
                        help="icon edge length in pixels (default: 32, the size "
                             "the firmware draws tile icons at)")
    parser.add_argument("--output-dir", default=DEFAULT_OUTPUT_DIR,
                        help="where the PNGs go (default: %s/)" % DEFAULT_OUTPUT_DIR)
    parser.add_argument("--ref", default="main",
                        help="openhab-webui git ref to fetch from (default: main)")
    parser.add_argument("--jobs", type=int, default=8,
                        help="parallel downloads (default: 8)")
    parser.add_argument("--force", action="store_true",
                        help="re-fetch icons whose PNG is already there")
    parser.add_argument("--keep-svg", action="store_true",
                        help="keep the downloaded SVG next to each PNG")
    args = parser.parse_args()

    if args.size < 1:
        sys.exit("--size must be at least 1.")
    if args.jobs < 1:
        sys.exit("--jobs must be at least 1.")

    rasterizer_name, rasterize = find_rasterizer()
    base_url = RAW_BASE_URL % args.ref

    names = list_icon_names(args.ref)
    if args.names:
        names = select(names, args.names)

    os.makedirs(args.output_dir, exist_ok=True)

    if not args.force:
        pending = [name for name in names
                   if not os.path.exists(os.path.join(args.output_dir, name + ".png"))]
        skipped = len(names) - len(pending)
        if skipped:
            print("%d icon(s) already in %s/; --force to redo them"
                  % (skipped, args.output_dir))
        names = pending

    if not names:
        print("Nothing to do.")
        return

    print("fetching %d icon(s) from openhab-webui@%s, rasterizing with %s at %dx%d"
          % (len(names), args.ref, rasterizer_name, args.size, args.size))

    downloaded, failures = download(names, base_url, args.output_dir, args.jobs)

    if downloaded:
        rasterize(downloaded, args.output_dir, args.size, args.jobs)

    converted, rasterizer_failures = collect(downloaded, args.output_dir, args.keep_svg)
    failures.extend(rasterizer_failures)

    total_bytes = sum(size for _, size in converted)
    print("wrote %d PNG(s) to %s/, %d bytes total"
          % (len(converted), args.output_dir, total_bytes))

    for name, error in sorted(failures):
        print("  %-24s FAILED (%s)" % (name, error), file=sys.stderr)

    # A partial run is still useful output, but the exit status has to say that
    # not everything came through, or a caller in a script will not notice.
    if failures:
        sys.exit("%d icon(s) failed." % len(failures))


if __name__ == "__main__":
    main()
