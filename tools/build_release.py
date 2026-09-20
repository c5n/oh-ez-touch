#!/usr/bin/env python3
"""A release build: clean builds of every hardware target, collected and
version-labelled.

For each hardware target (the TARGETS table shared with batchupdate.py) the
build directory is cleaned, the sdkconfig is regenerated from the defaults
files, and the firmware is built. The images land in release/<version>/:

    oh-ez-touch-<version>-<target>.bin   the OTA image batchupdate.py posts
    <target>/bootloader.bin              |
    <target>/partition-table.bin         |  the set an initial esptool flash
    <target>/ota_data_initial.bin        |  needs
    SHA256SUMS                           checksums of all of the above

    tools/build_release.py                     # all hardware targets
    tools/build_release.py -t arduitouch lanbon  # a subset
    tools/build_release.py --dirty             # incremental, no cleaning

Needs the ESP-IDF environment (. ~/esp/esp-idf/export.sh) and nothing but
Python 3.
"""

import argparse
import hashlib
import shutil
import subprocess
import sys
from pathlib import Path

# tools/ is sys.path[0] when this runs as a script, so this import is also
# what a run from any other directory resolves. TARGETS is the list of what
# counts as a hardware target; expected_version_default names the release.
from batchupdate import REPO_ROOT, TARGETS, expected_version_default

# What an initial esptool flash needs besides the application image, as
# paths inside a build directory.
FLASH_SET = {
    "bootloader.bin": Path("bootloader") / "bootloader.bin",
    "partition-table.bin": Path("partition_table") / "partition-table.bin",
    "ota_data_initial.bin": Path("ota_data_initial.bin"),
}


def idf(build_dir, *argv):
    """One idf.py invocation; its output goes straight through."""
    return subprocess.run(
        ["idf.py", "-B", str(build_dir)] + list(argv),
        cwd=REPO_ROOT).returncode


def build_target(target, dirty):
    """Configure (from scratch unless dirty) and build one target.

    Returns the build directory's application image, None on failure."""
    build_dir = REPO_ROOT / "build" / target
    sdkconfig = build_dir / "sdkconfig"

    if not dirty and build_dir.exists():
        print("== %s: fullclean" % target, flush=True)
        if idf(build_dir, "fullclean") != 0:
            return None

    if dirty and sdkconfig.exists():
        configured = True
    else:
        # A cleaned tree has no sdkconfig: regenerate it from the defaults
        # files, so a release always builds the current configuration.
        print("== %s: set-target esp32" % target, flush=True)
        configured = idf(
            build_dir,
            "-DSDKCONFIG=%s" % sdkconfig,
            "-DSDKCONFIG_DEFAULTS=sdkconfig.defaults;"
            "sdkconfig.defaults.esp32;sdkconfig.defaults.%s" % target,
            "set-target", "esp32") == 0

    if not configured:
        return None

    print("== %s: build" % target, flush=True)
    if idf(build_dir, "build") != 0:
        return None

    image = build_dir / "oh-ez-touch.bin"
    if not image.is_file():
        print("error: %s: build succeeded but %s is missing"
              % (target, image), file=sys.stderr)
        return None

    return image


def collect(target, image, outdir, version, sums):
    """One target's images into the release directory."""
    ota_name = "oh-ez-touch-%s-%s.bin" % (version, target)
    copied = {ota_name: image}
    for name, source in FLASH_SET.items():
        path = image.parent / source
        if path.is_file():
            copied["%s/%s" % (target, name)] = path

    for name, source in copied.items():
        dest = outdir / name
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, dest)
        sums[name] = hashlib.sha256(dest.read_bytes()).hexdigest()

    return outdir / ota_name


def main():
    parser = argparse.ArgumentParser(
        description="Clean release builds of all hardware targets, collected "
                    "in release/<version>/.")
    parser.add_argument("-t", "--targets", nargs="+", choices=sorted(TARGETS),
                        metavar="TARGET",
                        help="build only these (default: all hardware "
                             "targets: %s)" % ", ".join(sorted(TARGETS)))
    parser.add_argument("--dirty", action="store_true",
                        help="incremental builds: keep the build directories "
                             "and their sdkconfigs")
    parser.add_argument("--outdir", metavar="DIR",
                        help="release directory (default release/<version>)")
    args = parser.parse_args()

    if shutil.which("idf.py") is None:
        print("error: idf.py is not on the PATH -- run "
              "'. ~/esp/esp-idf/export.sh' first", file=sys.stderr)
        return 2

    version = expected_version_default()
    outdir = Path(args.outdir) if args.outdir else REPO_ROOT / "release" / version
    targets = args.targets or sorted(TARGETS)

    print("release build %s: %s" % (version, ", ".join(targets)))

    sums = {}
    rows = []
    for target in targets:
        image = build_target(target, args.dirty)
        if image is None:
            print("error: %s failed -- stopping; the release directory %s "
                  "is incomplete" % (target, outdir), file=sys.stderr)
            return 1
        ota_image = collect(target, image, outdir, version, sums)
        rows.append((target, ota_image))

    (outdir / "SHA256SUMS").write_text(
        "".join("%s  %s\n" % (sums[name], name) for name in sorted(sums)))

    print("\n%-16s %10s  %s" % ("TARGET", "BYTES", "IMAGE"))
    for target, ota_image in rows:
        print("%-16s %10d  %s" % (target, ota_image.stat().st_size, ota_image))
    print("\nrelease %s complete in %s" % (version, outdir))
    return 0


if __name__ == "__main__":
    sys.exit(main())
