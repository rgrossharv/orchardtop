// SPDX-License-Identifier: Apache-2.0
// Standalone macOS regression tests; no downloads or privileged sensors required.
#include "osx/battery_power.hpp"
#include "osx/smc_power.hpp"
#include <cassert>
#include <iostream>
#include <limits>

int main() {
    auto dict = CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    auto put = [&](CFStringRef key, int64_t value) {
        auto n = CFNumberCreate(nullptr, kCFNumberSInt64Type, &value);
        CFDictionarySetValue(dict, key, n);
        CFRelease(n);
    };
    auto check = [&](double watts) {
        auto result = Power::read_battery_power(dict);
        assert(result && std::abs(result->watts - watts) < 0.000001);
    };
    assert(!Power::read_battery_power(nullptr));
    assert(!Power::read_battery_power(dict));
    put(CFSTR("Voltage"), 12000);
    put(CFSTR("Amperage"), -2000);
    check(24); // mA * mV -> W, sign-extended 64-bit discharge
    assert(!Power::read_battery_power(dict)->instantaneous);
    put(CFSTR("InstantAmperage"), -1000);
    check(12); // Instantaneous sample takes precedence over averaged current
    put(CFSTR("InstantAmperage"), 2000);
    check(-24); // Charge transition: no smoothed discharge residue
    put(CFSTR("InstantAmperage"), 0);
    check(0); // Full/charging hold is a valid zero, not missing
    put(CFSTR("InstantAmperage"), 4294966296LL);
    check(12); // Unsigned 32-bit representation of -1000
    put(CFSTR("InstantAmperage"), 100001);
    check(24); // Invalid instant sample falls back to valid average
    CFDictionaryRemoveValue(dict, CFSTR("Amperage"));
    assert(!Power::read_battery_power(dict));
    CFDictionarySetValue(dict, CFSTR("InstantAmperage"), CFSTR("bad"));
    assert(!Power::read_battery_power(dict));
    put(CFSTR("InstantAmperage"), -1000);
    put(CFSTR("Voltage"), 0);
    assert(!Power::read_battery_power(dict));
    put(CFSTR("Voltage"), -12000);
    assert(!Power::read_battery_power(dict));
    CFRelease(dict);
    unsigned char fixed[] = {12, 128};
    assert(Power::decode_smc_power("sp78", fixed, 2) == 12.5);
    float sample = 150.25f;
    auto bytes = reinterpret_cast<unsigned char*>(&sample);
    assert(Power::decode_smc_power("flt ", bytes, 4) == 150.25);
    assert(Power::decode_smc_power("flt ", bytes, 2) == -1);
    assert(Power::decode_smc_power("????", bytes, 4) == -1);
    sample = std::numeric_limits<float>::quiet_NaN();
    assert(Power::decode_smc_power("flt ", bytes, 4) == -1);
    sample = -10;
    assert(Power::decode_smc_power("flt ", bytes, 4) == -1);
    {
        auto props = CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        auto tele = CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        auto adapter = CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        auto set = [](CFMutableDictionaryRef d, CFStringRef k, int64_t v) {
            auto n = CFNumberCreate(nullptr, kCFNumberSInt64Type, &v);
            CFDictionarySetValue(d, k, n);
            CFRelease(n);
        };
        assert(!Power::read_system_telemetry(props).system_in_watts);
        set(tele, CFSTR("SystemPowerIn"), 61200);
        set(tele, CFSTR("SystemLoad"), 18400);
        set(adapter, CFSTR("Watts"), 96);
        CFDictionarySetValue(props, CFSTR("PowerTelemetryData"), tele);
        CFDictionarySetValue(props, CFSTR("AdapterDetails"), adapter);
        CFDictionarySetValue(props, CFSTR("ExternalConnected"), kCFBooleanTrue);
        auto t = Power::read_system_telemetry(props);
        assert(t.system_in_watts && std::abs(*t.system_in_watts - 61.2) < 1e-9);
        assert(t.system_load_watts && std::abs(*t.system_load_watts - 18.4) < 1e-9);
        assert(t.adapter_rated_watts == 96 && t.external_connected == true);
        auto f = Power::resolve_flow({t.system_in_watts, t.system_load_watts, -40.0, t.external_connected});
        assert(f.net_available && std::abs(f.net - 42.8) < 1e-9);
        CFDictionarySetValue(props, CFSTR("ExternalConnected"), kCFBooleanFalse);
        t = Power::read_system_telemetry(props);
        assert(t.system_in_watts == 0.0); // Unplugged: nothing enters.
        CFRelease(adapter); CFRelease(tele); CFRelease(props);
    }
    {
        auto props = CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        auto set = [&](CFStringRef k, int64_t v) {
            auto n = CFNumberCreate(nullptr, kCFNumberSInt64Type, &v);
            CFDictionarySetValue(props, k, n);
            CFRelease(n);
        };
        assert(!Power::read_battery_snapshot(props));
        set(CFSTR("CurrentCapacity"), 55);
        set(CFSTR("MaxCapacity"), 100);
        set(CFSTR("AppleRawCurrentCapacity"), 2600);
        set(CFSTR("AppleRawMaxCapacity"), 4800);
        set(CFSTR("Voltage"), 12000);
        set(CFSTR("InstantAmperage"), 3000);   // charging at 36 W
        set(CFSTR("AvgTimeToFull"), 65535);    // no estimate
        CFDictionarySetValue(props, CFSTR("IsCharging"), kCFBooleanTrue);
        CFDictionarySetValue(props, CFSTR("ExternalConnected"), kCFBooleanTrue);
        auto snap = Power::read_battery_snapshot(props);
        assert(snap && std::abs(snap->percent - 55) < 1e-9 && snap->state == "Charging");
        assert(std::abs(snap->energy_full_wh - 57.6) < 1e-9 && std::abs(snap->energy_now_wh - 31.2) < 1e-9);
        assert(!snap->os_seconds_to_full);
        assert(snap->flow.battery_flow && std::abs(*snap->flow.battery_flow + 36.0) < 1e-9);
        auto est = Power::estimate_charge(*snap);
        assert(est.charging && est.targets[0].seconds && est.targets[1].seconds);
        set(CFSTR("AvgTimeToFull"), 42);
        assert(Power::read_battery_snapshot(props)->os_seconds_to_full == 42 * 60.0);
        CFDictionarySetValue(props, CFSTR("FullyCharged"), kCFBooleanTrue);
        assert(Power::read_battery_snapshot(props)->state == "Full");
        CFRelease(props);
    }
    std::cout << "Battery current, transitions, missing data, and SMC decoding passed; system telemetry, battery snapshot passed\n";
}
