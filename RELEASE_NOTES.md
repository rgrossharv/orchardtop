# OrchardTop v1.4.12

- Refresh Apple Silicon battery watts in both locations about once per second
  without accelerating process, disk, network, GPU, or graph sampling.
- Redraw only the two power readouts on battery ticks and avoid copying
  collector data and graph histories during rendering.
- Cache thermal service discovery, skip unrelated CPU sensors, fix Core
  Foundation leaks, and avoid overlapping asynchronous temperature scans.
- Keep native btop collectors, presets, and swap defaults on Linux and Intel
  Macs, with the `otop` launcher and apple-dark default theme on all platforms.
- Remove the Homebrew formula's ARM-only restriction during release updates.
- Gate publication on an x86_64 Linux build and terminal/theme smoke test.

The hardware controls battery sample cadence; repeated readings are expected.
Linux battery and total-power enhancements remain future work. Existing custom
configuration is preserved. The standalone download remains macOS ARM64;
Homebrew builds the source for the target platform.
