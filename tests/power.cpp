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
    std::cout << "Battery current, transitions, missing data, and SMC decoding passed\n";
}
