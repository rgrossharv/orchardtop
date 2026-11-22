// SPDX-License-Identifier: Apache-2.0
// Battery time-to-80%/100% estimates.
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <random>

#include "charge_estimate.hpp"

namespace fs = std::filesystem;
using Power::BatterySnapshot;
using Power::estimate_charge;

static BatterySnapshot charging(double percent, double full_wh, double into_cell_w) {
	BatterySnapshot s;
	s.percent = percent;
	s.energy_full_wh = full_wh;
	s.energy_now_wh = full_wh * percent / 100.0;
	s.state = "Charging";
	s.flow.battery_flow = -into_cell_w;
	s.flow.adapter_connected = true;
	return s;
}

TEST(ChargeEstimate, LinearBelowEighty) {
	// 50 Wh battery at 50%: 15 Wh to 80% at 30 W = 30 minutes.
	const auto e = estimate_charge(charging(50, 50, 30));
	ASSERT_EQ(e.targets.size(), 2u);
	EXPECT_EQ(e.basis, "battery");
	ASSERT_TRUE(e.targets[0].seconds);
	EXPECT_NEAR(*e.targets[0].seconds, 1800.0, 1e-6);
	EXPECT_FALSE(e.targets[0].tapered);
	EXPECT_NEAR(e.targets[0].wh_needed, 15.0, 1e-9);
}

TEST(ChargeEstimate, LastTwentyPercentIsSlower) {
	const auto e = estimate_charge(charging(50, 50, 30));
	ASSERT_TRUE(e.targets[1].seconds);
	EXPECT_TRUE(e.targets[1].tapered);
	const double last20 = *e.targets[1].seconds - *e.targets[0].seconds;
	const double constant = 0.2 * 50 / 30 * 3600; // 80->100 at full power
	EXPECT_GT(last20, 2.0 * constant);
	EXPECT_LT(last20, 2.5 * constant);
}

TEST(ChargeEstimate, AboveEightyUsesTaperedPower) {
	// At 90% the measured 10 W is already tapered; the 80% power is higher.
	const auto e = estimate_charge(charging(90, 50, 10));
	EXPECT_TRUE(e.targets[0].reached);
	ASSERT_TRUE(e.targets[1].seconds);
	EXPECT_GT(*e.targets[1].seconds, 0.1 * 50 / 10 * 3600); // slower than constant 10 W
}

TEST(ChargeEstimate, ContinuityAtEighty) {
	// Charging 79.9 -> 80.1 must not jump.
	const double a = Power::charge_seconds(79.9, 80.0, 50, 30);
	const double b = Power::charge_seconds(80.0, 80.1, 50, 30);
	EXPECT_NEAR(a, b, a * 0.02);
}

TEST(ChargeEstimate, FallsBackToNetFlow) {
	BatterySnapshot s = charging(40, 60, 0);
	s.flow.battery_flow.reset();
	s.flow.adapter_in = 65.0;
	s.flow.system_load = 20.0;
	const auto e = estimate_charge(s);
	EXPECT_EQ(e.basis, "net");
	EXPECT_DOUBLE_EQ(e.charge_watts, 45.0);
	EXPECT_TRUE(e.targets[0].seconds);
}

TEST(ChargeEstimate, DischargingGivesTimeToEmpty) {
	BatterySnapshot s = charging(50, 50, 0);
	s.state = "Discharging";
	s.flow.battery_flow = 12.5;
	const auto e = estimate_charge(s);
	EXPECT_FALSE(e.charging);
	ASSERT_TRUE(e.seconds_to_empty);
	EXPECT_NEAR(*e.seconds_to_empty, 25.0 / 12.5 * 3600, 1e-6);
	EXPECT_FALSE(e.targets[0].seconds);
}

TEST(ChargeEstimate, ChargeLimitBlocksFull) {
	BatterySnapshot s = charging(60, 50, 20);
	s.charge_limit_percent = 80;
	const auto e = estimate_charge(s);
	EXPECT_TRUE(e.targets[0].seconds);
	EXPECT_TRUE(e.targets[1].beyond_limit);
	EXPECT_FALSE(e.targets[1].seconds);
	EXPECT_NE(Power::format_charge_report(s, e, 3, false).find("charging stops at 80%"), std::string::npos);
}

TEST(ChargeEstimate, ReportText) {
	auto s = charging(50, 50, 30);
	s.os_seconds_to_full = 3300;
	const auto text = Power::format_charge_report(s, estimate_charge(s), 3, false);
	EXPECT_NE(text.find("50%  (25.0 of 50.0 Wh)  Charging"), std::string::npos) << text;
	EXPECT_NE(text.find("To  80%    in 0h 30m"), std::string::npos) << text;
	EXPECT_NE(text.find("To 100%    in ~"), std::string::npos) << text;
	EXPECT_NE(text.find("OS estimate to full: 0h 55m"), std::string::npos) << text;
	EXPECT_EQ(text.find('\033'), std::string::npos); // No colour codes when not a terminal.
}

TEST(ChargeEstimate, Durations) {
	EXPECT_EQ(Power::format_duration(30), "<1m");
	EXPECT_EQ(Power::format_duration(3900), "1h 05m");
}

class SysfsBattery : public ::testing::Test {
protected:
	fs::path root;
	void SetUp() override {
		root = fs::temp_directory_path() / ("otop-bat-" + std::to_string(std::random_device{}()));
		fs::create_directories(root);
	}
	void TearDown() override { fs::remove_all(root); }
	void put(const std::string& dev, const std::string& file, const std::string& value) {
		fs::create_directories(root / dev);
		std::ofstream(root / dev / file) << value << "\n";
	}
};

TEST_F(SysfsBattery, EnergyBattery) {
	put("hidpp_battery_0", "type", "Battery");
	put("hidpp_battery_0", "scope", "Device");
	put("hidpp_battery_0", "capacity", "20");
	put("BAT0", "type", "Battery");
	put("BAT0", "status", "Charging");
	put("BAT0", "capacity", "55");
	put("BAT0", "energy_now", "30100000");
	put("BAT0", "energy_full", "54700000");
	put("BAT0", "power_now", "40000000");
	put("BAT0", "charge_control_end_threshold", "80");
	const auto s = Power::read_sysfs_battery(root);
	ASSERT_TRUE(s);
	EXPECT_DOUBLE_EQ(s->percent, 55);
	EXPECT_NEAR(s->energy_full_wh, 54.7, 1e-9);
	ASSERT_TRUE(s->flow.battery_flow);
	EXPECT_DOUBLE_EQ(*s->flow.battery_flow, -40.0);
	ASSERT_TRUE(s->charge_limit_percent);
	EXPECT_DOUBLE_EQ(*s->charge_limit_percent, 80);
}

TEST_F(SysfsBattery, ChargeBatteryUsesDesignVoltage) {
	put("BAT1", "type", "Battery");
	put("BAT1", "status", "Discharging");
	put("BAT1", "charge_now", "2000000");
	put("BAT1", "charge_full", "4000000");
	put("BAT1", "voltage_min_design", "11400000");
	const auto s = Power::read_sysfs_battery(root);
	ASSERT_TRUE(s);
	EXPECT_NEAR(s->energy_full_wh, 45.6, 1e-9);
	EXPECT_NEAR(s->percent, 50.0, 1e-9);
}

TEST_F(SysfsBattery, NoBattery) {
	EXPECT_FALSE(Power::read_sysfs_battery(root));
	EXPECT_FALSE(Power::read_sysfs_battery(root / "missing"));
}
