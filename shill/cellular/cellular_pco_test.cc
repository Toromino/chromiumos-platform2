// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/cellular/cellular_pco.h"

#include <tuple>

#include <gtest/gtest.h>

namespace shill {

class CellularPcoInvalidRawDataTest
    : public testing::TestWithParam<std::vector<uint8_t>> {};

TEST_P(CellularPcoInvalidRawDataTest, CreateFromRawData) {
  const auto& raw_data = GetParam();
  EXPECT_EQ(nullptr, CellularPco::CreateFromRawData(raw_data));
}

INSTANTIATE_TEST_SUITE_P(
    CellularPcoInvalidRawDataTest,
    CellularPcoInvalidRawDataTest,
    testing::Values(
        // Less than 3 octets:
        std::vector<uint8_t>({}),
        std::vector<uint8_t>({0x27}),
        std::vector<uint8_t>({0x27, 0x00}),
        // Invalid PCO content length:
        std::vector<uint8_t>({0x27, 0x00, 0x00}),
        std::vector<uint8_t>({0x27, 0x02, 0x00}),
        // Invalid PCO IEI:
        std::vector<uint8_t>({0x26, 0x01, 0x00}),
        // More than 253 octets:
        std::vector<uint8_t>({
            // clang-format off
            0x27, 0xFC, 0x00, 0xFF, 0x00, 0xF8, 0x00, 0x01,
            0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09,
            0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11,
            0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19,
            0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20, 0x21,
            0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29,
            0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F, 0x30, 0x31,
            0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39,
            0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 0x40, 0x41,
            0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49,
            0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F, 0x50, 0x51,
            0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59,
            0x5A, 0x5B, 0x5C, 0x5D, 0x5E, 0x5F, 0x60, 0x61,
            0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
            0x6A, 0x6B, 0x6C, 0x6D, 0x6E, 0x6F, 0x70, 0x71,
            0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79,
            0x7A, 0x7B, 0x7C, 0x7D, 0x7E, 0x7F, 0x80, 0x81,
            0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
            0x8A, 0x8B, 0x8C, 0x8D, 0x8E, 0x8F, 0x90, 0x91,
            0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99,
            0x9A, 0x9B, 0x9C, 0x9D, 0x9E, 0x9F, 0xA0, 0xA1,
            0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9,
            0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF, 0xB0, 0xB1,
            0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9,
            0xBA, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF, 0xC0, 0xC1,
            0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9,
            0xCA, 0xCB, 0xCC, 0xCD, 0xCE, 0xCF, 0xD0, 0xD1,
            0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9,
            0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF, 0xE0, 0xE1,
            0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9,
            0xEA, 0xEB, 0xEC, 0xED, 0xEE, 0xEF, 0xF0, 0xF1,
            0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7
            // clang-format on
        }),
        // Incomplete element
        std::vector<uint8_t>({0x26, 0x01, 0x00, 0xFF}),
        std::vector<uint8_t>({0x26, 0x01, 0x00, 0xFF, 0x00}),
        std::vector<uint8_t>({0x26, 0x01, 0x00, 0xFF, 0x00, 0x01}),
        std::vector<uint8_t>({0x26, 0x01, 0x00, 0xFF, 0x00, 0x02, 0x00})));

using CellularPcoValidRawDataTestParams =
    std::tuple<std::vector<uint8_t>,                // raw data
               std::vector<CellularPco::Element>>;  // expected elements

class CellularPcoValidRawDataTest
    : public testing::TestWithParam<CellularPcoValidRawDataTestParams> {};

TEST_P(CellularPcoValidRawDataTest, FindElement) {
  const auto& raw_data = std::get<0>(GetParam());
  const auto& expected_elements = std::get<1>(GetParam());
  auto pco = CellularPco::CreateFromRawData(raw_data);
  ASSERT_NE(nullptr, pco);
  for (const auto& expected_element : expected_elements) {
    const CellularPco::Element* element = pco->FindElement(expected_element.id);
    ASSERT_NE(nullptr, element);
    EXPECT_EQ(expected_element.id, element->id);
    EXPECT_EQ(expected_element.data, element->data);
  }
}

INSTANTIATE_TEST_SUITE_P(
    CellularPcoValidRawDataTest,
    CellularPcoValidRawDataTest,
    testing::Values(
        // No element
        make_tuple(std::vector<uint8_t>({0x27, 0x01, 0x00}),
                   std::vector<CellularPco::Element>()),
        // Element with no content
        make_tuple(std::vector<uint8_t>({0x27, 0x04, 0x00, 0xAA, 0xBB, 0x00}),
                   std::vector<CellularPco::Element>(
                       {CellularPco::Element(0xAABB, {})})),
        // Element with content of 1 octet
        make_tuple(
            std::vector<uint8_t>({0x27, 0x05, 0x00, 0xAA, 0xBB, 0x01, 0x22}),
            std::vector<CellularPco::Element>({CellularPco::Element(0xAABB,
                                                                    {0x22})})),
        // Multiple elements
        make_tuple(std::vector<uint8_t>({
                       // clang-format off
                       0x27, 0x0D, 0x00, 0xAA, 0xBB, 0x01, 0x22, 0xCC,
                       0xDD, 0x00, 0xEE, 0xFF, 0x02, 0x33, 0x44
                       // clang-format on
                   }),
                   std::vector<CellularPco::Element>({
                       CellularPco::Element(0xAABB, {0x22}),
                       CellularPco::Element(0xCCDD, {}),
                       CellularPco::Element(0xEEFF, {0x33, 0x44}),
                   })),
        // Element with content of the maximum length
        make_tuple(
            std::vector<uint8_t>({
                // clang-format off
                0x27, 0xFB, 0x00, 0xFF, 0x00, 0xF7, 0x00, 0x01,
                0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09,
                0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11,
                0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19,
                0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20, 0x21,
                0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29,
                0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F, 0x30, 0x31,
                0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39,
                0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 0x40, 0x41,
                0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49,
                0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F, 0x50, 0x51,
                0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59,
                0x5A, 0x5B, 0x5C, 0x5D, 0x5E, 0x5F, 0x60, 0x61,
                0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
                0x6A, 0x6B, 0x6C, 0x6D, 0x6E, 0x6F, 0x70, 0x71,
                0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79,
                0x7A, 0x7B, 0x7C, 0x7D, 0x7E, 0x7F, 0x80, 0x81,
                0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
                0x8A, 0x8B, 0x8C, 0x8D, 0x8E, 0x8F, 0x90, 0x91,
                0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99,
                0x9A, 0x9B, 0x9C, 0x9D, 0x9E, 0x9F, 0xA0, 0xA1,
                0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9,
                0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF, 0xB0, 0xB1,
                0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9,
                0xBA, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF, 0xC0, 0xC1,
                0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9,
                0xCA, 0xCB, 0xCC, 0xCD, 0xCE, 0xCF, 0xD0, 0xD1,
                0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9,
                0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF, 0xE0, 0xE1,
                0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9,
                0xEA, 0xEB, 0xEC, 0xED, 0xEE, 0xEF, 0xF0, 0xF1,
                0xF2, 0xF3, 0xF4, 0xF5, 0xF6
                // clang-format on
            }),
            std::vector<CellularPco::Element>({CellularPco::Element(
                0xFF00,
                {
                    // clang-format off
                    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                    0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
                    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
                    0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,
                    0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
                    0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F,
                    0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
                    0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F,
                    0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47,
                    0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F,
                    0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57,
                    0x58, 0x59, 0x5A, 0x5B, 0x5C, 0x5D, 0x5E, 0x5F,
                    0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67,
                    0x68, 0x69, 0x6A, 0x6B, 0x6C, 0x6D, 0x6E, 0x6F,
                    0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77,
                    0x78, 0x79, 0x7A, 0x7B, 0x7C, 0x7D, 0x7E, 0x7F,
                    0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
                    0x88, 0x89, 0x8A, 0x8B, 0x8C, 0x8D, 0x8E, 0x8F,
                    0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
                    0x98, 0x99, 0x9A, 0x9B, 0x9C, 0x9D, 0x9E, 0x9F,
                    0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7,
                    0xA8, 0xA9, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF,
                    0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7,
                    0xB8, 0xB9, 0xBA, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF,
                    0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7,
                    0xC8, 0xC9, 0xCA, 0xCB, 0xCC, 0xCD, 0xCE, 0xCF,
                    0xD0, 0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7,
                    0xD8, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF,
                    0xE0, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7,
                    0xE8, 0xE9, 0xEA, 0xEB, 0xEC, 0xED, 0xEE, 0xEF,
                    0xF0, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6,
                    // clang-format on
                })}))));

TEST(CellularPcoTest, FindNotExistentElement) {
  std::vector<uint8_t> raw_data = {
      // clang-format off
      0x27, 0x0D, 0x00, 0xAA, 0xBB, 0x01, 0x22, 0xCC,
      0xDD, 0x00, 0xEE, 0xFF, 0x02, 0x33, 0x44
      // clang-format on
  };
  auto pco = CellularPco::CreateFromRawData(raw_data);
  ASSERT_NE(nullptr, pco);
  EXPECT_EQ(nullptr, pco->FindElement(0xFF00));
}

}  // namespace shill
