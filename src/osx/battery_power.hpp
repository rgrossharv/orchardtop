// SPDX-License-Identifier: Apache-2.0
// OrchardTop battery electrical measurement. No component/adapter substitution.
#pragma once
#include <CoreFoundation/CoreFoundation.h>
#include <cmath>
#include <cstdint>
#include <optional>
#include <initializer_list>

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
}
