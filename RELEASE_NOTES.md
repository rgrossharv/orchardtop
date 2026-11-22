# OrchardTop v1.4.14

- New power-flow readout: `CHG` (watts the charger is delivering), `SYS`
  (watts the computer is consuming), and `NET` (in minus out, green ▲ when
  the battery is gaining, red ▼ when it is draining, even on AC).
- Apple Silicon reads `SystemPowerIn`/`SystemLoad` from the battery
  controller's power telemetry, plus the charger's rated watts.
- Linux gains the power row: charger, battery and system flow from
  `/sys/class/power_supply`, and RAPL CPU/DRAM power when readable.
  Peripheral (mouse/headset) batteries are ignored.
- The power row drops trailing detail on narrow boxes instead of cutting the
  charger, system and net figures, and only lists components that exist.
- Unit tests cover the flow arithmetic and sysfs parsing.
- Release and macOS CI builds move to the `macos-15` runner; GitHub retired
  `macos-14`, which stopped the `v1.4.13` tag from publishing.
- Development moves to a self-hosted Forgejo; releases and Homebrew still
  publish through GitHub.

Existing configuration is preserved. The standalone download remains macOS
ARM64; Homebrew builds the source for the target platform.
