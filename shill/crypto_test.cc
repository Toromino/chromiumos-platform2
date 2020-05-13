// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/crypto.h"

#include <string>

#include <gtest/gtest.h>

using testing::Test;

namespace shill {

namespace {
const char kEmptyText[] = "";
const char kPlainText[] = "This is a test!";
const char kROT47Text[] = "rot47:%9:D :D 2 E6DEP";
}  // namespace

TEST(CryptoTest, Encrypt) {
  EXPECT_EQ(kROT47Text, Crypto::Encrypt(kPlainText));
}

TEST(CryptoTest, DecryptNonROT47Fails) {
  EXPECT_FALSE(Crypto::Decrypt(kPlainText));
  EXPECT_FALSE(Crypto::Decrypt(kEmptyText));
}

TEST(CryptoTest, DecryptROT47Succeeds) {
  EXPECT_EQ(kPlainText, Crypto::Decrypt(kROT47Text));
}

}  // namespace shill
