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

The Apple power line labels the exposed component subtotal as `SUM` and the
AppleSMC board reading as `TOTAL`. On battery, OrchardTop uses `TOTAL` for the
live drain value and can derive a remaining-time estimate from the raw battery
capacity and voltage when macOS does not provide an ETA.
