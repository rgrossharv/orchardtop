// SPDX-License-Identifier: Apache-2.0
// Charger-in / system-out / net accounting and sysfs parsing.
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <random>

#include "power_flow.hpp"

namespace fs = std::filesystem;
using Power::FlowInputs;
using Power::resolve_flow;

TEST(PowerFlow, MeasuredInAndOutGiveNet) {
	const auto f = resolve_flow({.adapter_in = 61.0, .system_load = 18.5, .battery_flow = -40.0, .adapter_connected = true});
	EXPECT_DOUBLE_EQ(f.adapter_in, 61.0);
	EXPECT_DOUBLE_EQ(f.system_load, 18.5);
	EXPECT_DOUBLE_EQ(f.net, 42.5);
	EXPECT_TRUE(f.net_available);
	EXPECT_FALSE(f.system_derived);
}

TEST(PowerFlow, HeavyLoadOnChargerIsNetNegative) {
	const auto f = resolve_flow({.adapter_in = 30.0, .system_load = 45.0, .battery_flow = 15.0, .adapter_connected = true});
	EXPECT_DOUBLE_EQ(f.net, -15.0);
}

TEST(PowerFlow, UnpluggedBatteryFeedsSystem) {
	const auto f = resolve_flow({.battery_flow = 12.0, .adapter_connected = false});
	EXPECT_DOUBLE_EQ(f.adapter_in, 0.0);
	EXPECT_TRUE(f.adapter_derived);
	EXPECT_DOUBLE_EQ(f.system_load, 12.0);
	EXPECT_TRUE(f.system_derived);
	EXPECT_DOUBLE_EQ(f.net, -12.0);
}

TEST(PowerFlow, MeasuredChargerDerivesLoad) {
	const auto f = resolve_flow({.adapter_in = 60.0, .battery_flow = -45.0, .adapter_connected = true});
	EXPECT_DOUBLE_EQ(f.system_load, 15.0);
	EXPECT_TRUE(f.system_derived);
	EXPECT_DOUBLE_EQ(f.net, 45.0);
}

TEST(PowerFlow, UnmeasuredChargerFallsBackToBatteryNet) {
	const auto f = resolve_flow({.battery_flow = -20.0, .adapter_connected = true});
	EXPECT_LT(f.adapter_in, 0.0);
	EXPECT_LT(f.system_load, 0.0);
	EXPECT_TRUE(f.net_available);
	EXPECT_DOUBLE_EQ(f.net, 20.0);
}

TEST(PowerFlow, NothingKnown) {
	const auto f = resolve_flow({});
	EXPECT_FALSE(f.net_available);
	EXPECT_LT(f.adapter_in, 0.0);
	EXPECT_LT(f.system_load, 0.0);
}

TEST(PowerFlow, RejectsImplausibleValues) {
	const auto f = resolve_flow({.adapter_in = 5000.0, .system_load = -3.0, .adapter_connected = true});
	EXPECT_LT(f.adapter_in, 0.0);
	EXPECT_LT(f.system_load, 0.0);
	EXPECT_FALSE(f.net_available);
}

class SysfsPower : public ::testing::Test {
protected:
	fs::path root;
	void SetUp() override {
		root = fs::temp_directory_path() / ("otop-psu-" + std::to_string(std::random_device{}()));
		fs::create_directories(root);
	}
	void TearDown() override { fs::remove_all(root); }
	void put(const std::string& dev, const std::string& file, const std::string& value) {
		fs::create_directories(root / dev);
		std::ofstream(root / dev / file) << value << "\n";
	}
};

TEST_F(SysfsPower, DischargingLaptop) {
	put("AC", "type", "Mains");
	put("AC", "online", "0");
	put("BAT0", "type", "Battery");
	put("BAT0", "present", "1");
	put("BAT0", "status", "Discharging");
	put("BAT0", "power_now", "11500000");
	const auto s = Power::read_sysfs_power(root);
	ASSERT_TRUE(s.flow.battery_flow);
	EXPECT_DOUBLE_EQ(*s.flow.battery_flow, 11.5);
	EXPECT_EQ(s.flow.adapter_connected, false);
	const auto f = resolve_flow(s.flow);
	EXPECT_DOUBLE_EQ(f.system_load, 11.5);
	EXPECT_DOUBLE_EQ(f.net, -11.5);
}

TEST_F(SysfsPower, ChargingFromUsbPdWithCurrentVoltage) {
	put("ucsi-source-psy-USBC000:001", "type", "USB");
	put("ucsi-source-psy-USBC000:001", "online", "1");
	put("ucsi-source-psy-USBC000:001", "voltage_max", "20000000");
	put("ucsi-source-psy-USBC000:001", "current_max", "3250000");
	put("BAT0", "type", "Battery");
	put("BAT0", "status", "Charging");
	put("BAT0", "current_now", "2000000");
	put("BAT0", "voltage_now", "12000000");
	put("hidpp_battery_0", "type", "Battery"); // Mouse: must be ignored.
	put("hidpp_battery_0", "scope", "Device");
	put("hidpp_battery_0", "status", "Discharging");
	put("hidpp_battery_0", "power_now", "1000000");
	const auto s = Power::read_sysfs_power(root);
	EXPECT_EQ(s.flow.adapter_connected, true);
	EXPECT_FALSE(s.flow.adapter_in); // Rated capacity is not a measurement.
	EXPECT_NEAR(s.adapter_rated, 65.0, 1e-9);
	ASSERT_TRUE(s.flow.battery_flow);
	EXPECT_DOUBLE_EQ(*s.flow.battery_flow, -24.0);
	const auto f = resolve_flow(s.flow);
	EXPECT_DOUBLE_EQ(f.net, 24.0);
}

TEST_F(SysfsPower, MeasuredMainsAdapter) {
	put("ADP1", "type", "Mains");
	put("ADP1", "online", "1");
	put("ADP1", "power_now", "40000000");
	put("BAT1", "type", "Battery");
	put("BAT1", "status", "Charging");
	put("BAT1", "power_now", "25000000");
	const auto f = resolve_flow(Power::read_sysfs_power(root).flow);
	EXPECT_DOUBLE_EQ(f.adapter_in, 40.0);
	EXPECT_DOUBLE_EQ(f.system_load, 15.0);
	EXPECT_DOUBLE_EQ(f.net, 25.0);
}

TEST_F(SysfsPower, DesktopHasNothing) {
	const auto s = Power::read_sysfs_power(root);
	EXPECT_FALSE(s.any_supply);
	EXPECT_FALSE(resolve_flow(s.flow).net_available);
	EXPECT_FALSE(Power::read_sysfs_power(root / "missing").any_supply);
}
