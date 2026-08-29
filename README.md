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

It includes an `apple-dark` color theme. Pick it in the menu.

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
