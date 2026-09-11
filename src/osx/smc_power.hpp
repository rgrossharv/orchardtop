// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace Power {
inline double decode_smc_power(std::string_view type, const unsigned char* bytes, unsigned size) {
    double watts = -1;
    if (type == "flt " && size == 4) {
        float value;
        std::memcpy(&value, bytes, sizeof(value));
        watts = value;
    } else if (type == "sp78" && size == 2) {
        const auto raw = static_cast<uint16_t>((bytes[0] << 8) | bytes[1]);
        watts = static_cast<int16_t>(raw) / 256.0;
    }
    return std::isfinite(watts) && watts >= 0 && watts < 1000 ? watts : -1;
}
}
