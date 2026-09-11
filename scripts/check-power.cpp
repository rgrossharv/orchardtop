// SPDX-License-Identifier: Apache-2.0
// Read-only diagnostic using the exact battery parser used by OrchardTop.
#include "osx/battery_power.hpp"
#include <IOKit/IOKitLib.h>
#include <iostream>
#include <iomanip>
int main() {
    auto service = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("AppleSmartBattery"));
    if (!service) return 1;
    CFMutableDictionaryRef properties = nullptr;
    auto error = IORegistryEntryCreateCFProperties(service, &properties, nullptr, 0);
    IOObjectRelease(service);
    if (error || !properties) return 1;
    auto reading = Power::read_battery_power(properties);
    CFRelease(properties);
    if (!reading) { std::cerr << "Battery measurement unavailable\n"; return 1; }
    std::cout << std::fixed << std::setprecision(3)
        << (reading->instantaneous ? "InstantAmperage" : "Amperage") << "=" << reading->current_mA
        << " mA Voltage=" << reading->voltage_mV << " mV; BAT "
        << (reading->watts > 0 ? "OUT " : reading->watts < 0 ? "IN " : "IDLE ")
        << std::abs(reading->watts) << " W\n";
}
