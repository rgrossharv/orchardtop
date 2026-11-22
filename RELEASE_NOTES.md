# OrchardTop v1.4.15

- New `otop --timetofull`: estimates how long until the battery reaches 80%
  and 100% from the measured power going into it (three-reading average).
  Below 80% the estimate is linear; the slower 80-100% phase uses a taper
  model and is marked `~`. The OS's own time-to-full is shown when available,
  Linux charge limits are respected, and time to empty is shown on battery.
- Updated with upstream btop through November 2026: fixes for process-filter
  resource use, number shortening, Linux disk usage on monolithic kernels and
  hidden-box redraws, plus eight new Zacess themes and btop's consolidated CI.
- The terminal smoke test no longer flakes on slower machines.

The power row from v1.4.14 (`CHG` / `SYS` / `NET`) is unchanged. Existing
configuration is preserved. The standalone download remains macOS ARM64;
Homebrew builds the source for the target platform.
