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

    tools/build_release.py                       # the default hardware targets
    tools/build_release.py --full                # plus the optional ones
    tools/build_release.py -t arduitouch lanbon  # a subset
    tools/build_release.py --dirty               # incremental, no cleaning

On a terminal the run keeps a display on screen: a progress bar, and a
table of files, durations and sizes that fills in as targets finish.
idf.py's output would scroll that away, so it goes to a log file instead
-- kept, and its tail printed, when a target fails. --verbose (or no
terminal, or one too short for the display) gives the display up and
lets idf.py's output through, as releases used to be.

Needs the ESP-IDF environment (. ~/esp/esp-idf/export.sh) and nothing but
Python 3.
"""

import argparse
import hashlib
import shutil
import subprocess
import sys
import tempfile
import threading
import time
from collections import deque
from pathlib import Path

# tools/ is sys.path[0] when this runs as a script, so this import is also
# what a run from any other directory resolves. TARGETS is the list of what
# counts as a hardware target; expected_version_default names the release.
from batchupdate import REPO_ROOT, TARGETS, expected_version_default

# batchupdate.py keeps REPO_ROOT as a string; every use below joins paths on
# it, so here it is a Path.
REPO_ROOT = Path(REPO_ROOT)

# What an initial esptool flash needs besides the application image, as
# paths inside a build directory.
FLASH_SET = {
    "bootloader.bin": Path("bootloader") / "bootloader.bin",
    "partition-table.bin": Path("partition_table") / "partition-table.bin",
    "ota_data_initial.bin": Path("ota_data_initial.bin"),
}


def idf(build_dir, log, *argv):
    """One idf.py invocation; its output goes to log, or -- with log
    None, the default -- straight through."""
    return subprocess.run(
        ["idf.py", "-B", str(build_dir)] + list(argv),
        cwd=REPO_ROOT,
        stdout=log,
        stderr=subprocess.STDOUT if log else None).returncode


# The recovery firmware is built like a target but is not one: it is the
# same image for every board, and its defaults file replaces the board's
# rather than adding to it.
MINIMAL = "minimal"

# What -t accepts and a full run builds: the hardware targets plus the
# recovery image, which batchupdate.py --minimal then looks for as
# oh-ez-touch-<version>-minimal.bin.
ALL_TARGETS = sorted(TARGETS) + [MINIMAL]

# What a full run (--full) adds; without it these need -t to be built.
OPTIONAL_TARGETS = {"arduitouch_jtag", "cyd", MINIMAL}
DEFAULT_TARGETS = [t for t in sorted(TARGETS) if t not in OPTIONAL_TARGETS]


def format_duration(seconds):
    """Seconds as m:ss (or h:mm:ss for the long-winded)."""
    seconds = int(seconds)
    hours, seconds = divmod(seconds, 3600)
    minutes, seconds = divmod(seconds, 60)
    if hours:
        return "%d:%02d:%02d" % (hours, minutes, seconds)
    return "%d:%02d" % (minutes, seconds)


def progress_bar(done, total, started, width=24):
    """One line of build progress: filled bar, count, and if there is
    anything done yet a naive ETA from the average per-target time."""
    filled = width * done // total
    bar = "%s%s" % ("#" * filled, "." * (width - filled))
    if done:
        per_target = (time.monotonic() - started) / done
        remaining = per_target * (total - done)
        return "[%s] %d/%d  (ETA %s)" % (bar, done, total,
                                         format_duration(remaining))
    return "[%s] %d/%d" % (bar, done, total)


def show_log_tail(log_path, target):
    """A failed target's build log: its tail on stderr, and where the
    rest of it is -- the log file is kept."""
    with open(log_path, "rb") as log:
        tail = deque(log, maxlen=60)
    print("---- %s failed; the last 60 lines of %s ----"
          % (target, log_path), file=sys.stderr)
    for line in tail:
        sys.stderr.buffer.write(line)
    print("---- everything else: %s ----" % log_path, file=sys.stderr)


class Display:
    """The screen for the whole run: a progress bar and a table that
    fills in as targets finish, redrawn in place with ANSI escapes.
    A ticker thread repaints once a second, so elapsed times and the
    ETA move while idf.py grinds away with its output going to a log
    file rather than the terminal.

    The display is only used when it fits the terminal (see height()),
    which is what keeps the cursor-up of a redraw from running past the
    top of the screen."""

    BAR_WIDTH = 24

    def __init__(self, targets, version, outdir):
        self.targets = list(targets)
        self.version = version
        self.outdir = outdir
        # What render() works from, one entry per target: build duration
        # (None until it has one), the (name, size) files collected, and
        # a failed flag for the one that stopped the run.
        self.rows = {t: {"duration": None, "files": [], "failed": False}
                     for t in self.targets}
        self.current = None          # the target being built right now
        self.current_started = None  # when it started (monotonic)
        self.activity = None         # "<target>: <step>" for the bar line
        self.message = None          # a note line under the table
        self.started = time.monotonic()
        self.lines = 0               # lines the last redraw printed
        self.lock = threading.Lock()
        self.stopped = threading.Event()
        self.ticker = None

    def start(self):
        self.ticker = threading.Thread(target=self._tick, daemon=True)
        self.ticker.start()
        self.redraw()

    def _tick(self):
        while not self.stopped.wait(1.0):
            self.redraw()

    def stop(self):
        """Freeze the display as it is; the cursor is left below it, so
        whatever gets printed next appends instead of overwriting."""
        self.stopped.set()
        if self.ticker:
            self.ticker.join()
        self.redraw()
        self.lines = 0

    def building(self, target):
        with self.lock:
            self.current = target
            self.current_started = time.monotonic()
            self.activity = None
            self.message = None
        self.redraw()

    def step(self, label):
        with self.lock:
            self.activity = label
        self.redraw()

    def note(self, message):
        with self.lock:
            self.message = message
        self.redraw()

    def done(self, target, duration, files):
        with self.lock:
            self.current = None
            self.activity = None
            self.rows[target]["duration"] = duration
            self.rows[target]["files"] = files
        self.redraw()

    def fail(self, target):
        with self.lock:
            self.current = None
            self.activity = None
            self.rows[target]["failed"] = True
        self.stop()

    def height(self):
        """Lines the display needs once every target has filled in its
        files (the OTA image plus FLASH_SET), and the message line has
        appeared: what a terminal must hold for the in-place redraws to
        stay on the screen."""
        return 8 + len(self.targets) * (1 + len(FLASH_SET))

    def render(self):
        """The lines of the current display; called with the lock held."""
        done = sum(1 for r in self.rows.values() if r["duration"] is not None)
        total = len(self.targets)

        lines = ["release build %s -> %s" % (self.version, self.outdir), ""]

        filled = self.BAR_WIDTH * done // total
        bar = "[%s%s] %d/%d" % ("#" * filled, "." * (self.BAR_WIDTH - filled),
                                done, total)
        # The ETA is naive: the wall clock so far over what is done,
        # scaled by what is not. It cannot know the current target is
        # halfway through, so it settles as targets finish.
        if 0 < done < total:
            per_target = (time.monotonic() - self.started) / done
            bar += "  (ETA %s)" % format_duration(
                per_target * (total - done))
        if self.current:
            bar += "  %s %s" % (
                self.activity or "building %s" % self.current,
                format_duration(time.monotonic() - self.current_started))
        lines += [bar, ""]

        total_duration = 0.0
        total_size = 0
        total_files = 0
        lines.append("%-16s %10s  %9s  %s"
                     % ("TARGET", "DURATION", "BYTES", "FILE"))
        for target in self.targets:
            row = self.rows[target]
            duration, files = row["duration"], row["files"]
            if duration is not None:
                lines.append("%-16s %10s  %9d  %s"
                             % (target, format_duration(duration),
                                files[0][1], files[0][0]))
                for name, size in files[1:]:
                    lines.append("%-16s %10s  %9d  %s"
                                 % ("", "", size, name))
                total_duration += duration
                total_size += sum(size for _, size in files)
                total_files += len(files)
            elif row["failed"]:
                lines.append("%-16s %10s  %9s  %s"
                             % (target, "-", "-", "FAILED"))
            elif target == self.current:
                lines.append("%-16s %10s  %9s  %s"
                             % (target, format_duration(
                                   time.monotonic() - self.current_started)
                               + "...", "-", "building..."))
            else:
                lines.append("%-16s %10s  %9s  %s"
                             % (target, "-", "-", ""))
        lines.append("%-16s %10s  %9d  (%d files)"
                     % ("TOTAL", format_duration(total_duration),
                        total_size, total_files))

        if self.message:
            lines += ["", self.message]
        return lines

    def redraw(self):
        """Repaint the screen: cursor up to the top of the previous
        paint, erase from there down, print the new one."""
        with self.lock:
            lines = self.render()
            if self.lines:
                sys.stdout.write("\x1b[%dA" % self.lines)
            sys.stdout.write("\x1b[J")
            sys.stdout.write("\n".join(lines) + "\n")
            sys.stdout.flush()
            self.lines = len(lines)


class PlainUI:
    """What replaces Display when the terminal cannot host one (piped
    output, --verbose, or too few lines): step and note messages as
    plain prints, which is what a release build used to look like."""

    def step(self, label):
        print("== %s" % label, flush=True)

    def note(self, message):
        print(message, file=sys.stderr, flush=True)


def build_target(target, dirty, ui, log):
    """Configure (from scratch unless dirty) and build one target.

    ui gets the step messages; log (None to pass idf.py's output
    through) is where idf.py's output goes. Returns the build
    directory's application image, None on failure."""
    build_dir = REPO_ROOT / "build" / target
    sdkconfig = build_dir / "sdkconfig"

    if not dirty and build_dir.exists():
        ui.step("%s: fullclean" % target)
        if idf(build_dir, log, "fullclean") != 0:
            return None

    if dirty and sdkconfig.exists():
        configured = True
    else:
        # A cleaned tree has no sdkconfig: regenerate it from the defaults
        # files, so a release always builds the current configuration.
        ui.step("%s: set-target esp32" % target)
        configured = idf(
            build_dir,
            log,
            "-DSDKCONFIG=%s" % sdkconfig,
            "-DSDKCONFIG_DEFAULTS=sdkconfig.defaults;"
            "sdkconfig.defaults.esp32;sdkconfig.defaults.%s" % target,
            "set-target", "esp32") == 0

    if not configured:
        return None

    ui.step("%s: build" % target)
    if idf(build_dir, log, "build") != 0:
        return None

    image = build_dir / "oh-ez-touch.bin"
    if not image.is_file():
        ui.note("error: %s: build succeeded but %s is missing"
                % (target, image))
        return None

    return image


def collect(target, image, outdir, version, sums):
    """One target's images into the release directory.

    Returns the OTA image path and the (name, size) pairs copied."""
    ota_name = "oh-ez-touch-%s-%s.bin" % (version, target)
    copied = {ota_name: image}
    for name, source in FLASH_SET.items():
        path = image.parent / source
        if path.is_file():
            copied["%s/%s" % (target, name)] = path

    files = []
    for name, source in copied.items():
        dest = outdir / name
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, dest)
        sums[name] = hashlib.sha256(dest.read_bytes()).hexdigest()
        files.append((name, dest.stat().st_size))

    return outdir / ota_name, files


def print_table(rows):
    """The final table for a plain run: a line per file (target and
    duration on the first of each), and the totals."""
    total_duration = sum(duration for _, duration, _ in rows)
    total_size = sum(size for _, _, files in rows for _, size in files)
    print("\n%-16s %10s  %9s  %s" % ("TARGET", "DURATION", "BYTES", "FILE"))
    for target, duration, files in rows:
        print("%-16s %10s  %9d  %s"
              % (target, format_duration(duration), files[0][1], files[0][0]))
        for name, size in files[1:]:
            print("%-16s %10s  %9d  %s" % ("", "", size, name))
    print("%-16s %10s  %9d  (%d files)"
          % ("TOTAL", format_duration(total_duration), total_size,
             sum(len(files) for _, _, files in rows)))


def main():
    parser = argparse.ArgumentParser(
        description="Clean release builds of all hardware targets, collected "
                    "in release/<version>/.")
    parser.add_argument("-t", "--targets", nargs="+", choices=ALL_TARGETS,
                        metavar="TARGET",
                        help="build only these (default: %s, or all of them "
                             "with --full: %s)"
                             % (", ".join(DEFAULT_TARGETS),
                                ", ".join(ALL_TARGETS)))
    parser.add_argument("--full", action="store_true",
                        help="build the optional targets too: %s"
                             % ", ".join(sorted(OPTIONAL_TARGETS)))
    parser.add_argument("--dirty", action="store_true",
                        help="incremental builds: keep the build directories "
                             "and their sdkconfigs")
    parser.add_argument("--verbose", action="store_true",
                        help="stream idf.py's output instead of the live "
                             "progress display")
    parser.add_argument("--outdir", metavar="DIR",
                        help="release directory (default release/<version>)")
    args = parser.parse_args()

    if shutil.which("idf.py") is None:
        print("error: idf.py is not on the PATH -- run "
              "'. ~/esp/esp-idf/export.sh' first", file=sys.stderr)
        return 2

    version = expected_version_default()
    outdir = (Path(args.outdir) if args.outdir
              else REPO_ROOT / "release" / version)
    targets = args.targets or (ALL_TARGETS if args.full
                               else DEFAULT_TARGETS)

    # The display needs a terminal that can hold it; anything else (a
    # pipe, --verbose, a short terminal) gets the plain old prints.
    live = sys.stdout.isatty() and not args.verbose
    display = Display(targets, version, outdir) if live else None
    if live and display.height() > shutil.get_terminal_size().lines:
        print("note: the terminal is too short for the live display -- "
              "falling back to plain output", file=sys.stderr)
        live = False
        display = None
    ui = display or PlainUI()

    if display:
        display.start()
    else:
        print("release build %s: %s" % (version, ", ".join(targets)))

    sums = {}
    rows = []
    started = time.monotonic()
    for target in targets:
        log = log_path = None
        if display:
            log = tempfile.NamedTemporaryFile(
                mode="w+b", prefix="build_release-%s-" % target,
                suffix=".log", delete=False)
            log_path = Path(log.name)
            display.building(target)
        else:
            print("%s  building %s"
                  % (progress_bar(len(rows), len(targets), started), target),
                  flush=True)
        target_started = time.monotonic()
        image = build_target(target, args.dirty, ui, log)
        duration = time.monotonic() - target_started
        if log:
            log.close()
        if image is None:
            if display:
                display.fail(target)
                show_log_tail(log_path, target)
            print("error: %s failed -- stopping; the release directory %s "
                  "is incomplete" % (target, outdir), file=sys.stderr)
            return 1
        if log_path:
            log_path.unlink()
        _, files = collect(target, image, outdir, version, sums)
        rows.append((target, duration, files))
        if display:
            display.done(target, duration, files)
        else:
            print("%s  %s done in %s"
                  % (progress_bar(len(rows), len(targets), started), target,
                     format_duration(duration)), flush=True)

    (outdir / "SHA256SUMS").write_text(
        "".join("%s  %s\n" % (sums[name], name) for name in sorted(sums)))

    if display:
        display.stop()
    else:
        print_table(rows)
    print("\nrelease %s complete in %s (wall clock %s)"
          % (version, outdir, format_duration(time.monotonic() - started)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
