// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "modemfwd/file_decompressor.h"

#include <memory>
#include <string>

#include <base/check.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/stl_util.h>
#include <gtest/gtest.h>

#include "modemfwd/scoped_temp_file.h"

namespace modemfwd {
namespace {

class FileDecompressorTest : public testing::Test {
 public:
  FileDecompressorTest()
      : in_file_(ScopedTempFile::Create()),
        out_file_(ScopedTempFile::Create()) {
    CHECK(in_file_);
    CHECK(out_file_);
  }

 protected:
  std::unique_ptr<ScopedTempFile> in_file_;
  std::unique_ptr<ScopedTempFile> out_file_;
};

TEST_F(FileDecompressorTest, DecompressNonExistentInputFile) {
  EXPECT_FALSE(
      DecompressXzFile(base::FilePath("non_existent_file"), out_file_->path()));
}

TEST_F(FileDecompressorTest, DecompressEmptyFile) {
  EXPECT_FALSE(DecompressXzFile(in_file_->path(), out_file_->path()));
}

TEST_F(FileDecompressorTest, DecompressMalformedFile) {
  std::string content = "foo";
  ASSERT_EQ(base::WriteFile(in_file_->path(), content.data(), content.size()),
            content.size());
  EXPECT_FALSE(DecompressXzFile(in_file_->path(), out_file_->path()));
}

TEST_F(FileDecompressorTest, Decompress1MOfZeroes) {
  // Generated from `dd if=/dev/zero bs=1024 count=1024 | xz | xxd -i`
  static const uint8_t kCompressedContent[] = {
      0xfd, 0x37, 0x7a, 0x58, 0x5a, 0x00, 0x00, 0x04, 0xe6, 0xd6, 0xb4, 0x46,
      0x02, 0x00, 0x21, 0x01, 0x16, 0x00, 0x00, 0x00, 0x74, 0x2f, 0xe5, 0xa3,
      0xef, 0xff, 0xff, 0x00, 0xd9, 0x5d, 0x00, 0x00, 0x6f, 0xfd, 0xff, 0xff,
      0xa3, 0xb7, 0xff, 0x47, 0x3e, 0x48, 0x15, 0x72, 0x39, 0x61, 0x51, 0xb8,
      0x92, 0x28, 0xe6, 0xa3, 0x86, 0x07, 0xf9, 0xee, 0xe4, 0x1e, 0x82, 0xd3,
      0x2f, 0xc5, 0x3a, 0x3c, 0x01, 0x4b, 0xb1, 0x7e, 0xc9, 0x8a, 0x8a, 0x4d,
      0x2f, 0xa3, 0x0d, 0xd9, 0x7f, 0xa6, 0xe3, 0x8c, 0x23, 0x11, 0x53, 0xe0,
      0x59, 0x18, 0xc5, 0x75, 0x8a, 0xe2, 0x77, 0xf8, 0xb6, 0x94, 0x7f, 0x0c,
      0x6a, 0xc0, 0xde, 0x74, 0x49, 0x64, 0xe2, 0xe9, 0x5c, 0x53, 0xb2, 0x04,
      0xd8, 0xf7, 0x44, 0x0c, 0xab, 0x5f, 0x0d, 0x6d, 0x46, 0xe9, 0xe5, 0xc3,
      0x76, 0x88, 0xb7, 0x96, 0x57, 0xac, 0xb6, 0x4d, 0xe1, 0x69, 0x1d, 0x6f,
      0xfb, 0x4b, 0x88, 0x10, 0x6c, 0x42, 0xcb, 0x88, 0x3f, 0x5c, 0x00, 0x8f,
      0xd0, 0x4e, 0xaf, 0x26, 0x28, 0x94, 0x71, 0x1f, 0x3d, 0x8f, 0x24, 0xe1,
      0x70, 0x9e, 0xa7, 0x23, 0x5f, 0xec, 0x28, 0xcb, 0x85, 0xd1, 0x95, 0x98,
      0x8a, 0x7e, 0x2a, 0x91, 0xf2, 0x27, 0x75, 0xf7, 0x19, 0xc0, 0x06, 0x98,
      0x4d, 0x98, 0xfd, 0xd8, 0xaf, 0xd5, 0x90, 0x0f, 0xc4, 0x25, 0x53, 0xf8,
      0xf5, 0x91, 0x36, 0x31, 0x05, 0xa5, 0xb0, 0xee, 0x6f, 0xc1, 0x70, 0x4d,
      0x47, 0x0c, 0xd1, 0x91, 0x11, 0xaa, 0xad, 0x60, 0x1d, 0xba, 0xce, 0xb1,
      0x27, 0x18, 0x5c, 0x59, 0x86, 0xe9, 0x66, 0x52, 0x58, 0xbe, 0xe9, 0x76,
      0xac, 0x59, 0xe4, 0xe5, 0x5b, 0x05, 0x08, 0xf9, 0xc7, 0xda, 0xad, 0xfc,
      0xfb, 0x52, 0x2a, 0xf7, 0xe4, 0x95, 0x25, 0x00, 0x00, 0x00, 0x00, 0x00,
      0xc2, 0xf6, 0xba, 0x3e, 0xa2, 0x70, 0x6b, 0x60, 0x00, 0x01, 0xf5, 0x01,
      0x80, 0x80, 0x40, 0x00, 0x79, 0x97, 0x7f, 0x47, 0xb1, 0xc4, 0x67, 0xfb,
      0x02, 0x00, 0x00, 0x00, 0x00, 0x04, 0x59, 0x5a,
  };

  ASSERT_EQ(base::WriteFile(in_file_->path(),
                            reinterpret_cast<const char*>(kCompressedContent),
                            base::size(kCompressedContent)),
            base::size(kCompressedContent));
  EXPECT_TRUE(DecompressXzFile(in_file_->path(), out_file_->path()));

  std::string content;
  ASSERT_TRUE(base::ReadFileToString(out_file_->path(), &content));
  EXPECT_EQ(1024 * 1024, content.size());
  for (size_t i = 0; i < content.size(); ++i)
    EXPECT_EQ(0, content[i]);
}

}  // namespace
}  // namespace modemfwd
