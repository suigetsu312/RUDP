#include <gtest/gtest.h>
#include <array>
#include <cstdint>

#include "Rudp/Utils.hpp"

using namespace Rudp;

TEST(UtilsEndian, WriteReadU16BE) {
    std::array<uint8_t, 2> b{};
    ASSERT_TRUE(Utils::WriteU16BE(b, 0, 0x1234));
    EXPECT_EQ(b[0], 0x12);
    EXPECT_EQ(b[1], 0x34);

    uint16_t v = 0;
    ASSERT_TRUE(Utils::ReadU16BE(b, 0, v));
    EXPECT_EQ(v, 0x1234);
}

TEST(UtilsEndian, WriteReadU32BE) {
    std::array<uint8_t, 4> b{};
    ASSERT_TRUE(Utils::WriteU32BE(b, 0, 0x01020304u));
    EXPECT_EQ(b[0], 0x01);
    EXPECT_EQ(b[1], 0x02);
    EXPECT_EQ(b[2], 0x03);
    EXPECT_EQ(b[3], 0x04);

    uint32_t v = 0;
    ASSERT_TRUE(Utils::ReadU32BE(b, 0, v));
    EXPECT_EQ(v, 0x01020304u);
}

TEST(UtilsEndian, WriteReadU64BE) {
    std::array<uint8_t, 8> b{};
    const uint64_t x = 0x0102030405060708ull;
    ASSERT_TRUE(Utils::WriteU64BE(b, 0, x));
    EXPECT_EQ(b[0], 0x01);
    EXPECT_EQ(b[1], 0x02);
    EXPECT_EQ(b[2], 0x03);
    EXPECT_EQ(b[3], 0x04);
    EXPECT_EQ(b[4], 0x05);
    EXPECT_EQ(b[5], 0x06);
    EXPECT_EQ(b[6], 0x07);
    EXPECT_EQ(b[7], 0x08);

    uint64_t v = 0;
    ASSERT_TRUE(Utils::ReadU64BE(b, 0, v));
    EXPECT_EQ(v, x);
}

TEST(UtilsEndian, BoundsChecks) {
    uint16_t u16 = 0;
    uint32_t u32 = 0;
    uint64_t u64 = 0;

    std::array<uint8_t, 1> b1{};
    EXPECT_FALSE(Utils::WriteU16BE(b1, 0, 0x1234));
    EXPECT_FALSE(Utils::ReadU16BE(b1, 0, u16));

    std::array<uint8_t, 3> b3{};
    EXPECT_FALSE(Utils::WriteU32BE(b3, 0, 0x01020304u));
    EXPECT_FALSE(Utils::ReadU32BE(b3, 0, u32));

    std::array<uint8_t, 7> b7{};
    EXPECT_FALSE(Utils::WriteU64BE(b7, 0, 0x0102030405060708ull));
    EXPECT_FALSE(Utils::ReadU64BE(b7, 0, u64));

    std::array<uint8_t, 4> b4{};
    EXPECT_FALSE(Utils::WriteU32BE(b4, 1, 0x01020304u)); // 1+4 > 4
    EXPECT_FALSE(Utils::ReadU32BE(b4, 1, u32));
}
