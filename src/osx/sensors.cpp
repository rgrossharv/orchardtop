/* Copyright 2021 Aristocratos (jakob@qvantnet.com)

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

	   http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

indent = tab
tab-size = 4
*/

#include <Availability.h>
#if __MAC_OS_X_VERSION_MIN_REQUIRED > 101504
#include "sensors.hpp"

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hidsystem/IOHIDEventSystemClient.h>

#include <string>
#include <algorithm>
#include <cctype>
#include <numeric>
#include <unordered_map>
#include <vector>
#include <chrono>
#include <memory>

extern "C" {
typedef struct __IOHIDEvent *IOHIDEventRef;
typedef struct __IOHIDServiceClient *IOHIDServiceClientRef;
#ifdef __LP64__
typedef double IOHIDFloat;
#else
typedef float IOHIDFloat;
#endif

#define IOHIDEventFieldBase(type) (type << 16)
#define kIOHIDEventTypeTemperature 15

IOHIDEventSystemClientRef IOHIDEventSystemClientCreate(CFAllocatorRef allocator);
int IOHIDEventSystemClientSetMatching(IOHIDEventSystemClientRef client, CFDictionaryRef match);
int IOHIDEventSystemClientSetMatchingMultiple(IOHIDEventSystemClientRef client, CFArrayRef match);
IOHIDEventRef IOHIDServiceClientCopyEvent(IOHIDServiceClientRef, int64_t, int32_t, int64_t);
CFStringRef IOHIDServiceClientCopyProperty(IOHIDServiceClientRef service, CFStringRef property);
IOHIDFloat IOHIDEventGetFloatValue(IOHIDEventRef event, int32_t field);

// create a dict ref, like for temperature sensor {"PrimaryUsagePage":0xff00, "PrimaryUsage":0x5}
CFDictionaryRef matching(int page, int usage) {
	CFNumberRef nums[2];
	CFStringRef keys[2];

	keys[0] = CFStringCreateWithCString(0, "PrimaryUsagePage", 0);
	keys[1] = CFStringCreateWithCString(0, "PrimaryUsage", 0);
	nums[0] = CFNumberCreate(0, kCFNumberSInt32Type, &page);
	nums[1] = CFNumberCreate(0, kCFNumberSInt32Type, &usage);

	CFDictionaryRef dict = CFDictionaryCreate(0, (const void **)keys, (const void **)nums, 2, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
	CFRelease(keys[0]);
	CFRelease(keys[1]);
	CFRelease(nums[0]);
	CFRelease(nums[1]);
	return dict;
}

double getValue(IOHIDServiceClientRef sc) {
	IOHIDEventRef event = IOHIDServiceClientCopyEvent(sc, kIOHIDEventTypeTemperature, 0, 0);  // here we use ...CopyEvent
	IOHIDFloat temp = 0.0;
	if (event != 0) {
		temp = IOHIDEventGetFloatValue(event, IOHIDEventFieldBase(kIOHIDEventTypeTemperature));
		CFRelease(event);
	}
	return temp;
}

}  // extern C

namespace {
	int parse_sensor_index(const std::string& name, const std::string& prefix) {
		if (name.rfind(prefix, 0) != 0) return -1;
		size_t pos = prefix.size();
		if (pos >= name.size() or not std::isdigit(static_cast<unsigned char>(name.at(pos)))) return -1;
		int value = 0;
		while (pos < name.size() and std::isdigit(static_cast<unsigned char>(name.at(pos)))) {
			value = (value * 10) + (name.at(pos) - '0');
			pos++;
		}
		return value;
	}

	long long avg_to_long(const std::vector<double>& temps) {
		if (temps.empty()) return 0ll;
		return round(std::accumulate(temps.begin(), temps.end(), 0ll) / temps.size());
	}
}

long long Cpu::ThermalSensors::getSensors() {
	std::vector<long long> unused_core_temps;
	return getSensors(unused_core_temps);
}

long long Cpu::ThermalSensors::getSensors(std::vector<long long>& core_temps) {
	// One scan at a time; retain discovery across worker invocations, but
	// rediscover periodically so sleep/wake and service changes recover.
	struct Discovery {
		IOHIDEventSystemClientRef system = nullptr;
		CFArrayRef services = nullptr;
		std::vector<std::pair<IOHIDServiceClientRef, std::string>> sensors;
		std::chrono::steady_clock::time_point next{};
		~Discovery() { if (services) CFRelease(services); if (system) CFRelease(system); }
	};
	static auto shared_cache = std::make_shared<Discovery>();
	const auto cache_owner = shared_cache; // Keep services alive if exit joins an in-flight scan.
	auto& cache = *cache_owner;
	const auto now = std::chrono::steady_clock::now();
	if (now >= cache.next) {
		cache.next = now + std::chrono::seconds(60);
		if (cache.services) CFRelease(cache.services);
		if (cache.system) CFRelease(cache.system);
		cache.services = nullptr;
		cache.sensors.clear();
		cache.system = IOHIDEventSystemClientCreate(kCFAllocatorDefault);
		if (cache.system) {
			auto match = matching(0xff00, 5);
			IOHIDEventSystemClientSetMatching(cache.system, match);
			CFRelease(match);
			cache.services = IOHIDEventSystemClientCopyServices(cache.system);
			if (cache.services) {
				for (CFIndex i = 0; i < CFArrayGetCount(cache.services); ++i) {
					auto service = (IOHIDServiceClientRef)CFArrayGetValueAtIndex(cache.services, i);
					auto name = (CFStringRef)IOHIDServiceClientCopyProperty(service, CFSTR("Product"));
					if (not name) continue;
					char buf[200]{};
					const bool valid = CFStringGetCString(name, buf, sizeof(buf), kCFStringEncodingASCII);
					CFRelease(name);
					if (not valid) continue;
					std::string n(buf);
					if (n.rfind("eACC", 0) == 0 or n.rfind("pACC", 0) == 0
						or n.rfind("PMU tdie", 0) == 0 or n.rfind("SOC MTR Temp Sensor", 0) == 0)
						cache.sensors.emplace_back(service, std::move(n));
				}
			}
		}
	}
	std::vector<double> acc_temps;
	std::vector<double> tdie_temps;
	std::vector<double> soc_temps;
	std::unordered_map<std::string, std::vector<double> > acc_named_temps;
	std::unordered_map<int, std::vector<double> > tdie_indexed_temps;
	for (const auto& [sc, n] : cache.sensors) {
		const double temp = getValue(sc);
		if (not (temp > 0 and temp < 150)) continue;
		if (n.rfind("eACC", 0) == 0 or n.rfind("pACC", 0) == 0) {
			acc_temps.push_back(temp);
			acc_named_temps[n].push_back(temp);
		} else if (n.rfind("PMU tdie", 0) == 0) {
			tdie_temps.push_back(temp);
			const int index = parse_sensor_index(n, "PMU tdie");
			if (index >= 0) tdie_indexed_temps[index].push_back(temp);
		} else {
			soc_temps.push_back(temp);
		}
	}
	core_temps.clear();
	if (not tdie_indexed_temps.empty()) {
		std::vector<int> indexes;
		indexes.reserve(tdie_indexed_temps.size());
		for (const auto& indexed_temp : tdie_indexed_temps) {
			indexes.push_back(indexed_temp.first);
		}
		std::sort(indexes.begin(), indexes.end());
		for (const auto& index : indexes) {
			auto avg = avg_to_long(tdie_indexed_temps.at(index));
			if (avg > 0) core_temps.push_back(avg);
		}
	} else if (not acc_named_temps.empty()) {
		std::vector<std::string> names;
		names.reserve(acc_named_temps.size());
		for (const auto& named_temp : acc_named_temps) {
			names.push_back(named_temp.first);
		}
		std::sort(names.begin(), names.end());
		for (const auto& name : names) {
			auto avg = avg_to_long(acc_named_temps.at(name));
			if (avg > 0) core_temps.push_back(avg);
		}
	}

	const auto &temps = not acc_temps.empty() ? acc_temps : (not tdie_temps.empty() ? tdie_temps : soc_temps);
	return avg_to_long(temps);
}
#endif
