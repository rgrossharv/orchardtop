// SPDX-License-Identifier: Apache-2.0
// OrchardTop: battery time-to-80%/100% estimates from measured charge power.
#pragma once
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <fmt/format.h>

#include "power_flow.hpp"

namespace Power {

//* One reading of the battery's state of charge plus the power flow.
struct BatterySnapshot {
	double percent = -1.0;        // State of charge, 0-100.
	double energy_now_wh = -1.0;  // Stored energy.
	double energy_full_wh = -1.0; // Capacity at 100% (current health, not design).
	std::string state;            // "Charging", "Discharging", "Full", "Not charging", "Unknown".
	FlowInputs flow;              // Charger in, system load, battery flow (+ = discharge).
	std::optional<double> os_seconds_to_full;   // The OS/firmware's own estimate, if published.
	std::optional<double> charge_limit_percent; // Charging stops here (e.g. 80% threshold).
};

//* Read the current battery state. Implemented per platform; nullopt without a battery.
std::optional<BatterySnapshot> battery_snapshot();

struct ChargeTarget {
	double percent = 0.0;
	double wh_needed = 0.0;
	std::optional<double> seconds; // Unset when unreachable or not charging.
	bool reached = false;          // Already at or above the target.
	bool tapered = false;          // Part of the span is in the slow 80-100% phase.
	bool beyond_limit = false;     // A charge limit stops charging before the target.
};

struct ChargeEstimate {
	double charge_watts = 0.0;        // Power going into the battery (+) or out (-).
	std::string basis;                // Which measurement charge_watts came from.
	bool charging = false;
	std::vector<ChargeTarget> targets;
	std::optional<double> seconds_to_empty; // When discharging.
};

//* Lithium-ion cells charge at roughly constant power up to ~80%, then the
//* charger holds voltage and power falls off. Model the 80-100% span as power
//* tapering linearly to TAPER_END of its 80% value; this lands near the
//* 30-45 minutes laptops typically take for the last 20%.
inline constexpr double TAPER_START = 80.0;
inline constexpr double TAPER_END = 0.15;

inline double taper_factor(double percent) {
	if (percent <= TAPER_START) return 1.0;
	const double x = std::min(percent, 100.0) - TAPER_START;
	return 1.0 - (1.0 - TAPER_END) * x / (100.0 - TAPER_START);
}

//* Seconds to charge from percent a to b (a < b) given power p80 at 80%.
inline double charge_seconds(double a, double b, double full_wh, double p80) {
	const double wh_per_percent = full_wh / 100.0;
	double seconds = 0.0;
	// Constant-power span below the taper.
	if (a < TAPER_START) {
		const double end = std::min(b, TAPER_START);
		seconds += (end - a) * wh_per_percent / p80 * 3600.0;
		a = end;
	}
	// Tapered span: integral of dE / (p80 * f(s)) with f linear in s.
	if (b > a) {
		const double slope = (1.0 - TAPER_END) / (100.0 - TAPER_START);
		seconds += wh_per_percent / (p80 * slope) * std::log(taper_factor(a) / taper_factor(b)) * 3600.0;
	}
	return seconds;
}

inline ChargeEstimate estimate_charge(const BatterySnapshot& s, const std::vector<double>& targets = {80.0, 100.0}) {
	ChargeEstimate e;
	// Prefer the battery's own current: it is the energy actually being stored.
	// Fall back to charger-in minus system-load when the battery is not measured.
	if (plausible_watts(s.flow.battery_flow)) {
		e.charge_watts = -*s.flow.battery_flow;
		e.basis = "battery";
	} else if (const auto f = resolve_flow(s.flow); f.net_available) {
		e.charge_watts = f.net;
		e.basis = "net";
	} else {
		e.basis = "none";
	}
	e.charging = e.charge_watts > 0.05;

	const bool have_capacity = s.energy_full_wh > 0.0 and s.percent >= 0.0;
	if (not e.charging and e.charge_watts < -0.05 and s.energy_now_wh > 0.0)
		e.seconds_to_empty = s.energy_now_wh / -e.charge_watts * 3600.0;

	// Power the cell would take at 80%, inferred from the present reading.
	const double p80 = e.charge_watts / taper_factor(s.percent);
	for (const double target : targets) {
		ChargeTarget t;
		t.percent = target;
		t.reached = s.percent >= target - 0.5;
		t.beyond_limit = s.charge_limit_percent and *s.charge_limit_percent < target - 0.5;
		t.tapered = target > TAPER_START and not t.reached;
		if (have_capacity and not t.reached)
			t.wh_needed = (target - s.percent) / 100.0 * s.energy_full_wh;
		if (have_capacity and e.charging and not t.reached and not t.beyond_limit)
			t.seconds = charge_seconds(s.percent, target, s.energy_full_wh, p80);
		e.targets.push_back(t);
	}
	return e;
}

//* ------------------------------ report text --------------------------------

inline std::string format_duration(double seconds) {
	if (seconds < 60.0) return "<1m";
	const long minutes = std::lround(seconds / 60.0);
	return fmt::format("{}h {:02}m", minutes / 60, minutes % 60);
}

//* Human-readable report for `otop --timetofull`.
inline std::string format_charge_report(const BatterySnapshot& s, const ChargeEstimate& e, int samples, bool color) {
	const std::string bold = color ? "\033[1m" : "", reset = color ? "\033[0m" : "";
	const std::string green = color ? "\033[1;32m" : "", red = color ? "\033[1;31m" : "", dim = color ? "\033[2m" : "";
	const auto signed_w = [&](double w) {
		const std::string c = w > 0.05 ? green : w < -0.05 ? red : "";
		return fmt::format("{}{}{:.1f} W{}", c, w > 0.05 ? "+" : w < -0.05 ? "-" : "", std::abs(w), c.empty() ? "" : reset);
	};
	std::string out = fmt::format("{}OrchardTop battery charge estimate{}\n", bold, reset);

	out += fmt::format("  {}Battery{}    {:.0f}%", bold, reset, s.percent);
	if (s.energy_full_wh > 0) out += fmt::format("  ({:.1f} of {:.1f} Wh)", s.energy_now_wh, s.energy_full_wh);
	out += fmt::format("  {}\n", s.state.empty() ? "Unknown" : s.state);

	const auto flow = resolve_flow(s.flow);
	std::string power;
	if (flow.adapter_in >= 0) power += fmt::format("CHG {:.1f} W   ", flow.adapter_in);
	if (flow.system_load >= 0) power += fmt::format("SYS {}{:.1f} W   ", flow.system_derived ? "~" : "", flow.system_load);
	if (flow.net_available) power += "NET " + signed_w(flow.net);
	if (not power.empty()) out += fmt::format("  {}Power{}      {}\n", bold, reset, power);
	if (e.basis != "none") {
		out += fmt::format("  {}Into cell{}  {}  {}({} sensor, {}-sample average){}\n", bold, reset, signed_w(e.charge_watts), dim,
			e.basis == "battery" ? "battery" : "charger minus system", samples, reset);
	}
	out += "\n";

	if (s.state == "Full" or s.percent >= 99.5) {
		out += "  Battery is full.\n";
		return out;
	}
	if (not e.charging) {
		if (e.basis == "none") out += "  Charge power is not measurable on this machine, so no estimate is possible.\n";
		else if (e.seconds_to_empty)
			out += fmt::format("  Not charging: the battery is supplying {:.1f} W. About {} until empty at this rate.\n",
				-e.charge_watts, format_duration(*e.seconds_to_empty));
		else if (s.charge_limit_percent)
			out += fmt::format("  Plugged in but not charging (charge limit {:.0f}%).\n", *s.charge_limit_percent);
		else out += "  Not charging right now.\n";
		return out;
	}
	if (s.energy_full_wh <= 0) {
		out += "  Battery capacity is not reported, so no time estimate is possible.\n";
		return out;
	}
	for (const auto& t : e.targets) {
		out += fmt::format("  {}To {:>3.0f}%{}    ", bold, t.percent, reset);
		if (t.reached) out += "reached\n";
		else if (t.beyond_limit)
			out += fmt::format("not expected: charging stops at {:.0f}%\n", *s.charge_limit_percent);
		else if (t.seconds)
			out += fmt::format("in {:<9}  {:.1f} Wh{}\n", (t.tapered ? "~" : "") + format_duration(*t.seconds), t.wh_needed,
				t.tapered ? "   (charging slows after 80%)" : "");
	}
	if (s.os_seconds_to_full) out += fmt::format("             {}OS estimate to full: {}{}\n", dim, format_duration(*s.os_seconds_to_full), reset);
	return out;
}

//* ------------------------------ sysfs (Linux) ------------------------------

//* Read the main system battery (skipping peripheral batteries) under a
//* power_supply class directory. Energy in µWh, charge in µAh, voltage in µV.
inline std::optional<BatterySnapshot> read_sysfs_battery(const std::filesystem::path& root) {
	namespace fs = std::filesystem;
	std::error_code ec;
	if (not fs::is_directory(root, ec)) return std::nullopt;
	for (const auto& entry : fs::directory_iterator(root, ec)) {
		const auto dir = entry.path();
		if (read_sysfs(dir / "type").value_or("") != "Battery") continue;
		if (const auto scope = read_sysfs(dir / "scope"); scope and *scope == "Device") continue;
		if (const auto present = read_sysfs_number(dir / "present"); present and *present == 0) continue;

		BatterySnapshot s;
		s.state = read_sysfs(dir / "status").value_or("Unknown");
		auto energy_now = read_sysfs_number(dir / "energy_now");
		auto energy_full = read_sysfs_number(dir / "energy_full");
		if (energy_now and energy_full) {
			s.energy_now_wh = *energy_now / 1e6;
			s.energy_full_wh = *energy_full / 1e6;
		} else {
			auto charge_now = read_sysfs_number(dir / "charge_now");
			auto charge_full = read_sysfs_number(dir / "charge_full");
			auto volts = read_sysfs_number(dir / "voltage_min_design");
			if (not volts) volts = read_sysfs_number(dir / "voltage_now");
			if (charge_now and charge_full and volts) {
				s.energy_now_wh = *charge_now / 1e6 * *volts / 1e6;
				s.energy_full_wh = *charge_full / 1e6 * *volts / 1e6;
			}
		}
		if (auto capacity = read_sysfs_number(dir / "capacity")) s.percent = *capacity;
		else if (s.energy_full_wh > 0) s.percent = 100.0 * s.energy_now_wh / s.energy_full_wh;
		if (s.percent < 0) continue;

		if (auto ttf = read_sysfs_number(dir / "time_to_full_now"); ttf and *ttf > 0) s.os_seconds_to_full = *ttf;
		if (auto limit = read_sysfs_number(dir / "charge_control_end_threshold"); limit and *limit > 0 and *limit < 100)
			s.charge_limit_percent = *limit;
		s.flow = read_sysfs_power(root).flow;
		return s;
	}
	return std::nullopt;
}

} // namespace Power
