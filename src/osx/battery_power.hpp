// SPDX-License-Identifier: Apache-2.0
// OrchardTop battery electrical measurement. No component/adapter substitution.
#pragma once
#include <CoreFoundation/CoreFoundation.h>
#include <cmath>
#include <cstdint>
#include <optional>
#include <initializer_list>
#include "../power_flow.hpp"

namespace Power {
struct BatteryReading {
    double current_mA;
    double voltage_mV;
    double watts; // Positive = discharge; negative = charge.
    bool instantaneous;
};

inline std::optional<BatteryReading> read_battery_power(CFDictionaryRef properties) {
    if (!properties) return std::nullopt;
    auto number = [&](CFStringRef key) -> CFNumberRef {
        auto value = CFDictionaryGetValue(properties, key);
        return value && CFGetTypeID(value) == CFNumberGetTypeID()
            ? static_cast<CFNumberRef>(value) : nullptr;
    };
    double voltage = 0;
    auto voltage_number = number(CFSTR("Voltage"));
    if (!voltage_number || !CFNumberGetValue(voltage_number, kCFNumberDoubleType, &voltage)
        || !std::isfinite(voltage) || voltage <= 0 || voltage > 100000) return std::nullopt;
    // Try the freshest sample first. Amperage is the driver's averaged current.
    for (auto key : {CFSTR("InstantAmperage"), CFSTR("Amperage")}) {
        auto current_number = number(key);
        int64_t current = 0;
        if (!current_number || !CFNumberGetValue(current_number, kCFNumberSInt64Type, &current)) continue;
        // Some drivers publish signed 32-bit current in an unsigned OSNumber.
        // Signed 64-bit extraction already handles the 64-bit representation.
        if (current > INT32_MAX && current <= UINT32_MAX) current -= (int64_t{1} << 32);
        if (current < -100000 || current > 100000) continue;
        const double watts = -static_cast<double>(current) * voltage / 1000000.0;
        if (!std::isfinite(watts) || std::abs(watts) >= 1000) continue;
        return BatteryReading{static_cast<double>(current), voltage, watts, static_cast<bool>(CFEqual(key, CFSTR("InstantAmperage")))};
    }
    return std::nullopt;
}

//* Whole-system flow published by the battery controller on Apple Silicon
//* (AppleSmartBattery -> PowerTelemetryData, milliwatts).
struct SystemTelemetry {
    std::optional<double> system_in_watts;   // SystemPowerIn: charger power delivered to the system.
    std::optional<double> system_load_watts; // SystemLoad: power the computer is consuming.
    std::optional<bool> external_connected;  // ExternalConnected.
    double adapter_rated_watts = -1;         // AdapterDetails.Watts (capacity, not draw).
};

inline SystemTelemetry read_system_telemetry(CFDictionaryRef properties) {
    SystemTelemetry out;
    if (!properties) return out;
    auto milliwatts = [](CFDictionaryRef dict, CFStringRef key) -> std::optional<double> {
        auto value = CFDictionaryGetValue(dict, key);
        if (!value || CFGetTypeID(value) != CFNumberGetTypeID()) return std::nullopt;
        int64_t raw = 0;
        if (!CFNumberGetValue(static_cast<CFNumberRef>(value), kCFNumberSInt64Type, &raw)) return std::nullopt;
        if (raw > INT32_MAX && raw <= UINT32_MAX) raw -= (int64_t{1} << 32);
        if (raw < 0 || raw >= 1000000) return std::nullopt; // >= 1 kW is not plausible.
        return static_cast<double>(raw) / 1000.0;
    };
    auto dict_value = [](CFDictionaryRef dict, CFStringRef key) -> CFDictionaryRef {
        auto value = CFDictionaryGetValue(dict, key);
        return value && CFGetTypeID(value) == CFDictionaryGetTypeID() ? static_cast<CFDictionaryRef>(value) : nullptr;
    };
    if (auto telemetry = dict_value(properties, CFSTR("PowerTelemetryData"))) {
        out.system_in_watts = milliwatts(telemetry, CFSTR("SystemPowerIn"));
        out.system_load_watts = milliwatts(telemetry, CFSTR("SystemLoad"));
    }
    if (auto ext = CFDictionaryGetValue(properties, CFSTR("ExternalConnected")); ext && CFGetTypeID(ext) == CFBooleanGetTypeID())
        out.external_connected = CFBooleanGetValue(static_cast<CFBooleanRef>(ext));
    if (auto adapter = dict_value(properties, CFSTR("AdapterDetails"))) {
        auto value = CFDictionaryGetValue(adapter, CFSTR("Watts"));
        int64_t watts = 0;
        if (value && CFGetTypeID(value) == CFNumberGetTypeID()
            && CFNumberGetValue(static_cast<CFNumberRef>(value), kCFNumberSInt64Type, &watts) && watts > 0 && watts < 1000)
            out.adapter_rated_watts = static_cast<double>(watts);
    }
    // Unplugged: SystemPowerIn is meaningless noise if present; force 0 W.
    if (out.external_connected == false) out.system_in_watts = 0.0;
    return out;
}
}
