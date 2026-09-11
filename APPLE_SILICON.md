# Apple Silicon notes

OrchardTop is made for M1, M2, M3, and M4 Macs.

## The simple rule

Apple Silicon has one memory pool. The CPU and GPU share it.

That is why the GPU box has no memory section. A GPU memory number can look
like separate VRAM even though it is part of the same system memory. The
Memory box shows the whole truth in one place.

## Where the numbers come from

- CPU use comes from macOS Mach counters.
- Memory comes from macOS virtual-memory counters.
- Swap comes from the macOS `VM_SWAPUSAGE` value.
- GPU use comes from IOReport when it works.
- If IOReport does not work, GPU use comes from the Apple graphics driver.
- GPU power and temperature only appear when macOS gives useful data.
- The CPU panel includes a power line for CPU, GPU, ANE, DRAM, display, media,
  residual system draw, their exposed subtotal, and the board total.

OrchardTop does not claim to know how much memory belongs only to the GPU.
macOS does not give normal apps a simple and stable answer for every Mac and
every macOS version.

## Build and run

```bash
make
./otop
```

GPU support turns on by itself on an Apple Silicon build.

Preset `1` is the large Memory view. It shows swap inside the Memory box.
Other presets show the GPU alone or next to CPU, memory, or processes.

## A note about power

Apple does not publish one fixed GPU power limit for this tool to use. The
power bar compares the current reading with the largest reading seen since
OrchardTop started. The watts number is still the live reading.

The power row starts with `BAT OUT`, `BAT IN`, or `BAT IDLE`, computed from
AppleSmartBattery current and voltage. This is net battery flow, including all
loads powered by the battery during discharge. Charging power alone cannot tell
you the computer's total consumption while plugged in.

`SMC` labels PSTR separately because its coverage varies with hardware. `SUM`
is only the exposed IOReport channel subtotal; channels can overlap or omit
loads, so it must not be treated as battery or whole-system draw. Unavailable
sources show `N/A` or `-`, never a previous successful reading.

Battery readings have no additional smoothing. They prefer InstantAmperage over
Amperage, preserve the sign, and convert mA × mV to W. The sensor's own cadence,
precision, and averaging still apply; these are not calibrated meter readings.
The discharge ETA uses battery-side power only when macOS has no usable ETA.

Implementation references:
- [Apple power-source implementation](https://github.com/apple-oss-distributions/PowerManagement/blob/main/AppleSmartBatteryManager/AppleSmartBattery.cpp)
- [Chromium SMC power decoding](https://chromium.googlesource.com/chromium/src.git/+/72.0.3626.80/chrome/browser/metrics/power_metrics_provider_mac.mm)

Validation commands: `./scripts/test-power.sh` and `./scripts/check-power.sh`.
