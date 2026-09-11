# OrchardTop v1.4.9

- Show battery flow first: `BAT OUT` is power supplied by the battery, `BAT IN`
  is power entering it, and `BAT IDLE` is measured zero current.
- Prefer InstantAmperage × Voltage from one AppleSmartBattery snapshot; fall back
  to the driver's averaged Amperage. Decode signed current correctly and avoid
  extra smoothing across charging/discharging transitions.
- Keep SMC readings and IOReport component subtotals separate. Never replace
  battery draw with a component subtotal, charger rating, or board sensor.
- Clear failed sensor samples; support floating-point SMC power keys.
- Preserve macOS's discharge ETA when available; estimate it from battery
  capacity and battery draw only when the OS estimate is unavailable.
- Fix a startup crash when migrating an existing `Default` theme configuration.
- Embed apple-dark so the default palette works even without theme files.
- Add power regression tests, a live read-only diagnostic, and a resumable
  GitHub publish/build/download/upgrade script.

Battery watts are sensor-reported electrical flow, not a calibrated external
measurement. The battery controller controls sampling cadence. On AC, charging
watts do not represent total computer consumption or wall-plug power.
