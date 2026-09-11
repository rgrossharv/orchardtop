# OrchardTop

OrchardTop is a system monitor for Apple Silicon Macs.

It runs in Terminal. It shows CPU use, GPU use, power, memory, swap, disks,
network use, and running apps.

OrchardTop is based on [btop](https://github.com/aristocratos/btop). It keeps
the fast C++ code and the good-looking screen from btop. It changes the Mac
parts so they make more sense on Apple Silicon.

## Why this exists

Apple Silicon has one pool of memory. The CPU and GPU share it. There is no
separate VRAM pool. Regular btop makes this look a little weird, so it may look like the GPU has
its own memory. It does not. OrchardTop shows all memory in one place: the Memory box. The GPU box shows
GPU load and any power, clock, or temperature data that macOS gives us. It
does not show a fake VRAM total or a confusing GPU memory number.

This is a VIBECODED fork. It grew out of frustration with btop on Apple
Silicon and with [ASiTop](https://github.com/tlkh/asitop). ASiTop can be useful,
but its Python setup, screen layout, and scaling were not what this project
wanted. That is a matter of taste, not an attack on either project.

## What you need

- An M series Mac.
- macOS.
- Apple command line tools.

If you do not have the command line tools, run this once:

```bash
xcode-select --install
```

## Install with Homebrew (recommended)

OrchardTop has a Homebrew tap. The easiest way to install it is:

```bash
brew install rgrossharv/orchardtop/orchardtop
```

You can also add the tap first. Then the shorter command works:

```bash
brew tap rgrossharv/orchardtop
brew install orchardtop
```

## Install with curl

After a release is published, run:

```bash
curl -fsSL https://raw.githubusercontent.com/rgrossharv/orchardtop/main/install.sh | sh
```

The installer checks that the Mac is Apple Silicon. It checks the downloaded
file before it installs it. By default, it installs OrchardTop in
`~/.local/bin`.

## Memory and swap

The Memory box is the one place to look for memory.

- `UMA Total` is all physical memory in the Mac.
- `Used` is memory used by apps, macOS, drivers, and compressed pages.
- `Cached` is file data that macOS can reuse.
- `Available` is memory that can be used without adding more pressure.
- `Swap` is disk space macOS is using as extra memory.

Swap is shown inside the Memory box by default. Preset `1` is a large Memory
view, so it is the easiest place to see swap.

## GPU numbers

The GPU box does not show memory. This is on purpose.

GPU load comes from macOS driver data. Some Macs and macOS versions also give
OrchardTop GPU watts, clock speed, or temperature. If macOS does not give a
number, OrchardTop hides it instead of guessing.

## Settings

OrchardTop saves settings here:

```text
~/.config/orchardtop/orchardtop.conf
```

The `apple-dark` palette is built into the executable and is also shipped as a
theme file. It works on first launch even if the theme files are missing.

On Apple Silicon, `apple-dark` is the default launch theme. Existing configs
that still use the untouched `Default` theme are migrated to it; a different
theme chosen in the menu is respected.

## Power and battery estimates

The power row puts battery flow first:

- `BAT OUT 12.0W`: the battery is supplying 12 watts to the computer.
- `BAT IN 24.0W`: 24 watts are entering the battery while charging.
- `BAT IDLE 0.0W`: the sensor reports no net battery flow.
- `BAT N/A`: the battery measurement is unavailable.
- `SMC`: the separate PSTR sensor reading, when available. Its coverage depends
  on the Mac; it is not substituted for battery power.
- `SUM`: the subtotal of exposed IOReport energy channels, not whole-system power.

Battery watts come from signed `InstantAmperage` (mA) multiplied by `Voltage`
(mV) from one AppleSmartBattery snapshot, divided by 1,000,000. If the instant
sample is unavailable or invalid, the driver's averaged `Amperage` is used.
OrchardTop adds no smoothing. The battery controller still controls update cadence
and measurement accuracy; polling faster does not produce a newer hardware sample.

On battery, `BAT OUT` answers how much electrical power the computer is pulling
from the battery, including the loads fed by it. On AC, `BAT IN` is charging
power, not the computer's consumption. Charger nameplate watts are capacity,
not measured draw. Exact wall-plug consumption requires an external meter.

The battery header shows the same flow magnitude, with ▲ charging or ▼ discharging.
The macOS time-to-empty estimate is preserved when available; otherwise a battery
capacity/voltage/draw estimate is used. That ETA changes with workload and battery
condition. Missing measurements are never replaced with a component subtotal.

To check the raw values with the same parser used by the monitor:

```bash
./scripts/check-power.sh
./scripts/test-power.sh
```

## Publish and upgrade v1.4.10

The release is prepared locally. From a regular Terminal, run:

```bash
cd /path/to/orchardtop
./scripts/release-brew.sh --dry-run
./scripts/release-brew.sh
```

The version and release notes are already prepared. The script authenticates
through GitHub CLI if necessary, tests and builds with GCC 15, publishes the
tag, waits for GitHub Actions, and checks the downloaded release checksum. It
then updates the source formula in `rgrossharv/homebrew-orchardtop`, runs the
Homebrew upgrade, and verifies the formula test, executable version, and theme.

It requires a clean commit on `main`. A rerun reuses a matching tag and tap
update. It refuses to overwrite a different commit's tag or downgrade the tap.
The failed `v1.4.9` tag is preserved. If the build itself fails, inspect GitHub
Actions before retrying; changes to source require a new version/tag.

Once the script has published the formula, other installations can upgrade with:

```bash
brew update
brew upgrade rgrossharv/orchardtop/orchardtop
```

The script prints the exact Homebrew executable path. Use that path if an older
`~/.local/bin/orchardtop` copy takes precedence in your shell. The older
`scripts/release.sh` installs a standalone copy and does not update Homebrew.

After building, the monitor can be started either way:

```bash
./bin/orchardtop
./otop
```

`make install` also installs `otop` alongside `orchardtop` as a compatibility
command.

## License and credit

OrchardTop uses the Apache License 2.0 because btop uses that license. The full
license is in [LICENSE](LICENSE). The fork notice is in [NOTICE](NOTICE).

The original btop work is by Aristocratos and the btop contributors. Their
copyright and license notes are kept in the source files. Changed source files
say that they were changed for OrchardTop.

The included `fmt` code has its own license at
[include/fmt/LICENSE.rst](include/fmt/LICENSE.rst).

OrchardTop is not an official btop project. It is also not connected to Apple
or ASiTop. All code was written by 5.6 Luna XHigh and 5.6 Sol Medium

## More detail

If you want the longer technical notes, read
[APPLE_SILICON.md](APPLE_SILICON.md).
