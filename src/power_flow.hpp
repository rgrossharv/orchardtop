// SPDX-License-Identifier: Apache-2.0
// OrchardTop: platform-neutral power-flow accounting (charger in, system out, net).
#pragma once
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>

namespace Power {

//* Raw measurements gathered by a platform collector. All values in watts.
struct FlowInputs {
	std::optional<double> adapter_in{};  // Measured power entering from the charger.
	std::optional<double> system_load{};  // Measured power consumed by the computer.
	std::optional<double> battery_flow{}; // Positive = discharging, negative = charging.
	std::optional<bool> adapter_connected{}; // Unknown when the platform cannot tell.
};

//* Resolved flow. A negative watt value means unavailable.
struct Flow {
	double adapter_in = -1.0;
	double system_load = -1.0;
	double net = 0.0;            // Positive = more energy entering than leaving.
	bool net_available = false;
	bool adapter_derived = false; // adapter_in was inferred (e.g. 0 W on battery).
	bool system_derived = false;  // system_load was inferred from other readings.
};

inline bool plausible_watts(const std::optional<double>& w) {
	return w and std::isfinite(*w) and std::abs(*w) < 1000.0;
}

//* Combine whatever measurements exist into in/out/net figures without
//* inventing data: every derived value follows from conservation of energy
//* (charger in = system load + battery charge).
inline Flow resolve_flow(const FlowInputs& in) {
	Flow f;
	const bool have_adapter = plausible_watts(in.adapter_in) and *in.adapter_in >= 0.0;
	const bool have_system = plausible_watts(in.system_load) and *in.system_load >= 0.0;
	const bool have_battery = plausible_watts(in.battery_flow);

	if (have_adapter) f.adapter_in = *in.adapter_in;
	if (have_system) f.system_load = *in.system_load;

	// Unplugged: nothing enters, so the battery feeds the whole system.
	if (not have_adapter and in.adapter_connected == false) {
		f.adapter_in = 0.0;
		f.adapter_derived = true;
		if (not have_system and have_battery and *in.battery_flow >= 0.0) {
			f.system_load = *in.battery_flow;
			f.system_derived = true;
		}
	}
	// Plugged in with a measured charger: load = charger in + battery discharge.
	if (have_adapter and not have_system and have_battery) {
		const double load = *in.adapter_in + *in.battery_flow;
		if (load >= 0.0) {
			f.system_load = load;
			f.system_derived = true;
		}
	}

	if (f.adapter_in >= 0.0 and f.system_load >= 0.0) {
		f.net = f.adapter_in - f.system_load;
		f.net_available = true;
	} else if (have_battery) {
		// The battery's own flow is the net energy stored or drained.
		f.net = -*in.battery_flow;
		f.net_available = true;
	}
	return f;
}

//* ---------------------------- sysfs (Linux) helpers ----------------------------

inline std::optional<std::string> read_sysfs(const std::filesystem::path& p) {
	std::ifstream file(p);
	if (not file.good()) return std::nullopt;
	std::string value;
	std::getline(file, value);
	while (not value.empty() and (value.back() == '\n' or value.back() == ' ' or value.back() == '\r')) value.pop_back();
	if (file.bad()) return std::nullopt;
	return value;
}

inline std::optional<double> read_sysfs_number(const std::filesystem::path& p) {
	const auto s = read_sysfs(p);
	if (not s or s->empty()) return std::nullopt;
	try {
		size_t used = 0;
		const double v = std::stod(*s, &used);
		if (used == 0 or not std::isfinite(v)) return std::nullopt;
		return v;
	} catch (...) { return std::nullopt; }
}

struct SysfsPower {
	FlowInputs flow;
	double adapter_rated = -1.0; // Negotiated/nameplate capacity, not a measurement.
	bool any_supply = false;
};

//* Read every supply under a power_supply class directory (normally
//* /sys/class/power_supply). Batteries report µW/µA/µV; adapters that expose
//* power_now are treated as measured input.
inline SysfsPower read_sysfs_power(const std::filesystem::path& root) {
	namespace fs = std::filesystem;
	SysfsPower out;
	std::error_code ec;
	if (not fs::is_directory(root, ec)) return out;

	bool saw_adapter = false, adapter_online = false;
	double adapter_sum = 0.0, battery_sum = 0.0;
	bool adapter_measured = false, battery_measured = false;

	for (const auto& entry : fs::directory_iterator(root, ec)) {
		const auto dir = entry.path();
		const auto type = read_sysfs(dir / "type");
		if (not type) continue;
		// Peripheral batteries (mice, headsets) report scope=Device.
		if (const auto scope = read_sysfs(dir / "scope"); scope and *scope == "Device") continue;
		out.any_supply = true;

		if (*type == "Battery") {
			if (const auto present = read_sysfs_number(dir / "present"); present and *present == 0) continue;
			const auto status = read_sysfs(dir / "status").value_or("Unknown");
			std::optional<double> watts;
			if (auto p = read_sysfs_number(dir / "power_now")) watts = std::abs(*p) / 1e6;
			else if (auto i = read_sysfs_number(dir / "current_now"), v = read_sysfs_number(dir / "voltage_now"); i and v)
				watts = std::abs(*i) / 1e6 * std::abs(*v) / 1e6;
			if (not watts) continue;
			// sysfs magnitudes are unsigned on most drivers; status gives direction.
			if (status == "Discharging") battery_sum += *watts;
			else if (status == "Charging") battery_sum -= *watts;
			else if (status == "Full" or status == "Not charging") {} // ~0 W flow
			else continue;
			battery_measured = true;
		} else if (*type == "Mains" or type->starts_with("USB") or *type == "Wireless") {
			saw_adapter = true;
			const auto online = read_sysfs_number(dir / "online");
			if (not online or *online == 0) continue;
			adapter_online = true;
			if (auto p = read_sysfs_number(dir / "power_now"); p and *p > 0) {
				adapter_sum += *p / 1e6;
				adapter_measured = true;
			}
			auto v = read_sysfs_number(dir / "voltage_max");
			auto i = read_sysfs_number(dir / "current_max");
			if (v and i and *v > 0 and *i > 0) out.adapter_rated = std::max(out.adapter_rated, *v / 1e6 * *i / 1e6);
		}
	}

	if (saw_adapter) out.flow.adapter_connected = adapter_online;
	if (adapter_measured) out.flow.adapter_in = adapter_sum;
	if (battery_measured) {
		out.flow.battery_flow = battery_sum;
		// A discharging battery with no online adapter entry implies unplugged.
		if (not saw_adapter and battery_sum > 0.0) out.flow.adapter_connected = false;
	}
	return out;
}

} // namespace Power
