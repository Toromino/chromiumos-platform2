// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dlp/dlp_adaptor.h"

#include <memory>
#include <poll.h>
#include <string>
#include <utility>

#include <base/callback.h>
#include <base/files/file_util.h>
#include <base/memory/scoped_refptr.h>
#include <base/run_loop.h>
#include <brillo/dbus/mock_dbus_method_response.h>
#include <gtest/gtest.h>

#include "dlp/dlp_adaptor_test_helper.h"

using testing::_;
using testing::Invoke;
using testing::Return;

namespace dlp {
namespace {

// Some arbitrary D-Bus message serial number. Required for mocking D-Bus calls.
constexpr int kDBusSerial = 123;
constexpr int kPid = 1234;

class FileOpenRequestResultWaiter {
 public:
  FileOpenRequestResultWaiter() = default;
  ~FileOpenRequestResultWaiter() = default;
  FileOpenRequestResultWaiter(const FileOpenRequestResultWaiter&) = delete;
  FileOpenRequestResultWaiter& operator=(const FileOpenRequestResultWaiter&) =
      delete;

  // Waits until the result is available and returns it.
  bool GetResult() {
    run_loop_.Run();
    return result_;
  }

  // Returns the callback which should be passed to
  // DlpAdaptor::ProcessFileOpenRequest.
  base::OnceCallback<void(bool)> GetCallback() {
    return base::BindOnce(&FileOpenRequestResultWaiter::OnResult,
                          base::Unretained(this));
  }

 private:
  // Invoked when a result is available.
  void OnResult(bool result) {
    result_ = result;
    run_loop_.Quit();
  }

  base::RunLoop run_loop_;

  // Not initialized before run loop is quit.
  bool result_;
};

bool IsFdClosed(int fd) {
  struct pollfd pfd = {
      .fd = fd,
      .events = POLLERR,
  };
  if (poll(&pfd, 1, 1) < 0)
    return false;
  return pfd.revents & POLLERR;
}

}  // namespace

class DlpAdaptorTest : public ::testing::Test {
 public:
  DlpAdaptorTest() {
    // By passing true to SetFanotifyWatcherStartedForTesting,
    // DlpAdaptor won't try to start Fanotify. And given that these tests are
    // meant to test DlpAdaptor and don't depend on Fanotify, so Fanotify
    // initialisation isn't needed anyway.
    GetDlpAdaptor()->SetFanotifyWatcherStartedForTesting(true);
  }

  ~DlpAdaptorTest() override = default;

  DlpAdaptorTest(const DlpAdaptorTest&) = delete;
  DlpAdaptorTest& operator=(const DlpAdaptorTest&) = delete;

  DlpAdaptor* GetDlpAdaptor() { return helper_.adaptor(); }
  scoped_refptr<dbus::MockObjectProxy> GetMockDlpFilesPolicyServiceProxy() {
    return helper_.mock_dlp_files_policy_service_proxy();
  }

  std::vector<uint8_t> CreateSerializedAddFileRequest(
      const std::string& file,
      const std::string& source,
      const std::string& referrer) {
    AddFileRequest request;
    request.set_file_path(file);
    request.set_source_url(source);
    request.set_referrer_url(referrer);

    std::vector<uint8_t> proto_blob(request.ByteSizeLong());
    request.SerializeToArray(proto_blob.data(), proto_blob.size());
    return proto_blob;
  }

  std::vector<uint8_t> CreateSerializedRequestFileAccessRequest(
      ino_t inode, int pid, const std::string& destination) {
    RequestFileAccessRequest request;
    request.set_inode(inode);
    request.set_process_id(pid);
    request.set_destination_url(destination);

    std::vector<uint8_t> proto_blob(request.ByteSizeLong());
    request.SerializeToArray(proto_blob.data(), proto_blob.size());
    return proto_blob;
  }

  void StubIsDlpPolicyMatched(
      dbus::MethodCall* method_call,
      int /* timeout_ms */,
      dbus::MockObjectProxy::ResponseCallback* response_callback,
      dbus::MockObjectProxy::ErrorCallback* error_callback) {
    method_call->SetSerial(kDBusSerial);
    auto response = dbus::Response::FromMethodCall(method_call);
    dbus::MessageWriter writer(response.get());

    IsDlpPolicyMatchedResponse response_proto;
    response_proto.set_restricted(is_file_policy_restricted_);

    writer.AppendProtoAsArrayOfBytes(response_proto);
    std::move(*response_callback).Run(response.get());
  }

  void StubIsRestricted(
      dbus::MethodCall* method_call,
      int /* timeout_ms */,
      dbus::MockObjectProxy::ResponseCallback* response_callback,
      dbus::MockObjectProxy::ErrorCallback* error_callback) {
    method_call->SetSerial(kDBusSerial);
    auto response = dbus::Response::FromMethodCall(method_call);
    dbus::MessageWriter writer(response.get());

    IsRestrictedResponse response_proto;
    response_proto.set_restricted(is_file_policy_restricted_);

    writer.AppendProtoAsArrayOfBytes(response_proto);
    std::move(*response_callback).Run(response.get());
  }

 protected:
  bool is_file_policy_restricted_;

  DlpAdaptorTestHelper helper_;
};

TEST_F(DlpAdaptorTest, AllowedWithoutDatabase) {
  FileOpenRequestResultWaiter waiter;
  GetDlpAdaptor()->ProcessFileOpenRequest(
      /*inode=*/1, kPid, waiter.GetCallback());

  EXPECT_TRUE(waiter.GetResult());
}

TEST_F(DlpAdaptorTest, AllowedWithDatabase) {
  base::FilePath database_directory;
  base::CreateNewTempDirectory("dlpdatabase", &database_directory);
  GetDlpAdaptor()->InitDatabase(database_directory);

  FileOpenRequestResultWaiter waiter;
  GetDlpAdaptor()->ProcessFileOpenRequest(
      /*inode=*/1, kPid, waiter.GetCallback());

  EXPECT_TRUE(waiter.GetResult());
}

TEST_F(DlpAdaptorTest, NotRestrictedFileAddedAndAllowed) {
  base::FilePath database_directory;
  base::CreateNewTempDirectory("dlpdatabase", &database_directory);
  GetDlpAdaptor()->InitDatabase(database_directory);

  base::FilePath file_path;
  base::CreateTemporaryFile(&file_path);
  GetDlpAdaptor()->AddFile(
      CreateSerializedAddFileRequest(file_path.value(), "source", "referrer"));

  ino_t inode = GetDlpAdaptor()->GetInodeValue(file_path.value());

  is_file_policy_restricted_ = false;
  EXPECT_CALL(*GetMockDlpFilesPolicyServiceProxy(),
              DoCallMethodWithErrorCallback(_, _, _, _))
      .WillOnce(Invoke(this, &DlpAdaptorTest::StubIsDlpPolicyMatched));

  FileOpenRequestResultWaiter waiter;
  GetDlpAdaptor()->ProcessFileOpenRequest(inode, kPid, waiter.GetCallback());

  EXPECT_TRUE(waiter.GetResult());
}

TEST_F(DlpAdaptorTest, RestrictedFileAddedAndNotAllowed) {
  base::FilePath database_directory;
  base::CreateNewTempDirectory("dlpdatabase", &database_directory);
  GetDlpAdaptor()->InitDatabase(database_directory);

  base::FilePath file_path;
  base::CreateTemporaryFile(&file_path);
  GetDlpAdaptor()->AddFile(
      CreateSerializedAddFileRequest(file_path.value(), "source", "referrer"));

  ino_t inode = GetDlpAdaptor()->GetInodeValue(file_path.value());

  is_file_policy_restricted_ = true;
  EXPECT_CALL(*GetMockDlpFilesPolicyServiceProxy(),
              DoCallMethodWithErrorCallback(_, _, _, _))
      .WillOnce(Invoke(this, &DlpAdaptorTest::StubIsDlpPolicyMatched));

  FileOpenRequestResultWaiter waiter;
  GetDlpAdaptor()->ProcessFileOpenRequest(inode, kPid, waiter.GetCallback());

  EXPECT_FALSE(waiter.GetResult());
}
//
TEST_F(DlpAdaptorTest, RestrictedFileAddedAndRequestedAllowed) {
  // Create database.
  base::FilePath database_directory;
  base::CreateNewTempDirectory("dlpdatabase", &database_directory);
  GetDlpAdaptor()->InitDatabase(database_directory);

  // Create file to request access by inode.
  base::FilePath file_path;
  base::CreateTemporaryFile(&file_path);
  const ino_t inode = GetDlpAdaptor()->GetInodeValue(file_path.value());

  // Add the file to the database.
  GetDlpAdaptor()->AddFile(
      CreateSerializedAddFileRequest(file_path.value(), "source", "referrer"));

  // Setup callback for DlpFilesPolicyService::IsRestricted()
  is_file_policy_restricted_ = false;
  EXPECT_CALL(*GetMockDlpFilesPolicyServiceProxy(),
              DoCallMethodWithErrorCallback(_, _, _, _))
      .WillOnce(Invoke(this, &DlpAdaptorTest::StubIsRestricted));

  // Request access to the file.
  std::unique_ptr<brillo::dbus_utils::MockDBusMethodResponse<
      std::vector<uint8_t>, brillo::dbus_utils::FileDescriptor>>
      response = std::make_unique<brillo::dbus_utils::MockDBusMethodResponse<
          std::vector<uint8_t>, brillo::dbus_utils::FileDescriptor>>(nullptr);
  bool allowed;
  brillo::dbus_utils::FileDescriptor lifeline_fd;
  response->set_return_callback(base::BindRepeating(
      [](bool* allowed, brillo::dbus_utils::FileDescriptor* lifeline_fd,
         const std::vector<uint8_t>& proto_blob,
         const brillo::dbus_utils::FileDescriptor& fd) {
        RequestFileAccessResponse response;
        response.ParseFromArray(proto_blob.data(), proto_blob.size());
        *allowed = response.allowed();
        *lifeline_fd = brillo::dbus_utils::FileDescriptor(fd.get());
      },
      &allowed, &lifeline_fd));
  GetDlpAdaptor()->RequestFileAccess(
      std::move(response),
      CreateSerializedRequestFileAccessRequest(inode, kPid, "destination"));

  EXPECT_TRUE(allowed);
  EXPECT_FALSE(IsFdClosed(lifeline_fd.get()));

  // Access the file.
  FileOpenRequestResultWaiter waiter;
  GetDlpAdaptor()->ProcessFileOpenRequest(inode, kPid, waiter.GetCallback());

  EXPECT_TRUE(waiter.GetResult());

  // Second request still allowed.
  FileOpenRequestResultWaiter waiter2;
  GetDlpAdaptor()->ProcessFileOpenRequest(inode, kPid, waiter2.GetCallback());

  EXPECT_TRUE(waiter2.GetResult());
}
//
TEST_F(DlpAdaptorTest, RestrictedFileAddedAndRequestedNotAllowed) {
  // Create database.
  base::FilePath database_directory;
  base::CreateNewTempDirectory("dlpdatabase", &database_directory);
  GetDlpAdaptor()->InitDatabase(database_directory);

  // Create file to request access by inode.
  base::FilePath file_path;
  base::CreateTemporaryFile(&file_path);
  const ino_t inode = GetDlpAdaptor()->GetInodeValue(file_path.value());

  // Add the file to the database.
  GetDlpAdaptor()->AddFile(
      CreateSerializedAddFileRequest(file_path.value(), "source", "referrer"));

  // Setup callback for DlpFilesPolicyService::IsRestricted()
  is_file_policy_restricted_ = true;
  EXPECT_CALL(*GetMockDlpFilesPolicyServiceProxy(),
              DoCallMethodWithErrorCallback(_, _, _, _))
      .WillOnce(Invoke(this, &DlpAdaptorTest::StubIsRestricted));

  // Request access to the file.
  std::unique_ptr<brillo::dbus_utils::MockDBusMethodResponse<
      std::vector<uint8_t>, brillo::dbus_utils::FileDescriptor>>
      response = std::make_unique<brillo::dbus_utils::MockDBusMethodResponse<
          std::vector<uint8_t>, brillo::dbus_utils::FileDescriptor>>(nullptr);
  bool allowed;
  brillo::dbus_utils::FileDescriptor lifeline_fd;
  response->set_return_callback(base::BindRepeating(
      [](bool* allowed, brillo::dbus_utils::FileDescriptor* lifeline_fd,
         const std::vector<uint8_t>& proto_blob,
         const brillo::dbus_utils::FileDescriptor& fd) {
        RequestFileAccessResponse response;
        response.ParseFromArray(proto_blob.data(), proto_blob.size());
        *allowed = response.allowed();
        *lifeline_fd = brillo::dbus_utils::FileDescriptor(fd.get());
      },
      &allowed, &lifeline_fd));
  GetDlpAdaptor()->RequestFileAccess(
      std::move(response),
      CreateSerializedRequestFileAccessRequest(inode, kPid, "destination"));

  EXPECT_FALSE(allowed);
  EXPECT_TRUE(IsFdClosed(lifeline_fd.get()));

  // Setup callback for DlpFilesPolicyService::IsDlpPolicyMatched()
  is_file_policy_restricted_ = true;
  EXPECT_CALL(*GetMockDlpFilesPolicyServiceProxy(),
              DoCallMethodWithErrorCallback(_, _, _, _))
      .WillOnce(Invoke(this, &DlpAdaptorTest::StubIsDlpPolicyMatched));

  // Request access to the file.
  FileOpenRequestResultWaiter waiter;
  GetDlpAdaptor()->ProcessFileOpenRequest(inode, kPid, waiter.GetCallback());

  EXPECT_FALSE(waiter.GetResult());
}

TEST_F(DlpAdaptorTest, RestrictedFileAddedRequestedAndCancelledNotAllowed) {
  // Create database.
  base::FilePath database_directory;
  base::CreateNewTempDirectory("dlpdatabase", &database_directory);
  GetDlpAdaptor()->InitDatabase(database_directory);

  // Create file to request access by inode.
  base::FilePath file_path;
  base::CreateTemporaryFile(&file_path);
  const ino_t inode = GetDlpAdaptor()->GetInodeValue(file_path.value());

  // Add the file to the database.
  GetDlpAdaptor()->AddFile(
      CreateSerializedAddFileRequest(file_path.value(), "source", "referrer"));

  // Setup callback for DlpFilesPolicyService::IsRestricted()
  is_file_policy_restricted_ = false;
  EXPECT_CALL(*GetMockDlpFilesPolicyServiceProxy(),
              DoCallMethodWithErrorCallback(_, _, _, _))
      .WillOnce(Invoke(this, &DlpAdaptorTest::StubIsRestricted));

  // Request access to the file.
  std::unique_ptr<brillo::dbus_utils::MockDBusMethodResponse<
      std::vector<uint8_t>, brillo::dbus_utils::FileDescriptor>>
      response = std::make_unique<brillo::dbus_utils::MockDBusMethodResponse<
          std::vector<uint8_t>, brillo::dbus_utils::FileDescriptor>>(nullptr);
  bool allowed;
  brillo::dbus_utils::FileDescriptor lifeline_fd;
  response->set_return_callback(base::BindRepeating(
      [](bool* allowed, brillo::dbus_utils::FileDescriptor* lifeline_fd,
         const std::vector<uint8_t>& proto_blob,
         const brillo::dbus_utils::FileDescriptor& fd) {
        RequestFileAccessResponse response;
        response.ParseFromArray(proto_blob.data(), proto_blob.size());
        *allowed = response.allowed();
        *lifeline_fd = brillo::dbus_utils::FileDescriptor(fd.get());
      },
      &allowed, &lifeline_fd));
  GetDlpAdaptor()->RequestFileAccess(
      std::move(response),
      CreateSerializedRequestFileAccessRequest(inode, kPid, "destination"));

  EXPECT_TRUE(allowed);
  EXPECT_FALSE(IsFdClosed(lifeline_fd.get()));

  // Cancel access to the file.
  IGNORE_EINTR(close(lifeline_fd.release()));

  // Let DlpAdaptor process that lifeline_fd is closed.
  base::RunLoop().RunUntilIdle();

  // Setup callback for DlpFilesPolicyService::IsDlpPolicyMatched()
  is_file_policy_restricted_ = true;
  EXPECT_CALL(*GetMockDlpFilesPolicyServiceProxy(),
              DoCallMethodWithErrorCallback(_, _, _, _))
      .WillOnce(Invoke(this, &DlpAdaptorTest::StubIsDlpPolicyMatched));

  // Request access to the file.
  FileOpenRequestResultWaiter waiter;
  GetDlpAdaptor()->ProcessFileOpenRequest(inode, kPid, waiter.GetCallback());

  EXPECT_FALSE(waiter.GetResult());
}

// DlpAdaptor::RequestFileAccess crashes if file access is requested while the
// database isn't created yet. This test makes sure this doesn't happen anymore.
// https://crbug.com/1267295.
TEST_F(DlpAdaptorTest, RequestAllowedWithoutDatabase) {
  // Create file to request access by inode.
  base::FilePath file_path;
  base::CreateTemporaryFile(&file_path);
  const ino_t inode = GetDlpAdaptor()->GetInodeValue(file_path.value());

  // Request access to the file.
  std::unique_ptr<brillo::dbus_utils::MockDBusMethodResponse<
      std::vector<uint8_t>, brillo::dbus_utils::FileDescriptor>>
      response = std::make_unique<brillo::dbus_utils::MockDBusMethodResponse<
          std::vector<uint8_t>, brillo::dbus_utils::FileDescriptor>>(nullptr);
  bool allowed;
  brillo::dbus_utils::FileDescriptor lifeline_fd;
  response->set_return_callback(base::BindRepeating(
      [](bool* allowed, brillo::dbus_utils::FileDescriptor* lifeline_fd,
         const std::vector<uint8_t>& proto_blob,
         const brillo::dbus_utils::FileDescriptor& fd) {
        RequestFileAccessResponse response;
        response.ParseFromArray(proto_blob.data(), proto_blob.size());
        *allowed = response.allowed();
      },
      &allowed, &lifeline_fd));
  GetDlpAdaptor()->RequestFileAccess(
      std::move(response),
      CreateSerializedRequestFileAccessRequest(inode, kPid, "destination"));

  EXPECT_TRUE(allowed);
}

TEST_F(DlpAdaptorTest, GetFilesSources) {
  // Create database.
  base::FilePath database_directory;
  ASSERT_TRUE(base::CreateNewTempDirectory("dlpdatabase", &database_directory));
  GetDlpAdaptor()->InitDatabase(database_directory);

  // Create files to request sources by inodes.
  base::FilePath file_path1;
  ASSERT_TRUE(base::CreateTemporaryFile(&file_path1));
  const ino_t inode1 = GetDlpAdaptor()->GetInodeValue(file_path1.value());
  base::FilePath file_path2;
  ASSERT_TRUE(base::CreateTemporaryFile(&file_path2));
  const ino_t inode2 = GetDlpAdaptor()->GetInodeValue(file_path2.value());

  GetFilesSourcesRequest request;
  request.add_files_inodes(inode1);
  request.add_files_inodes(inode2);
  request.add_files_inodes(123456);

  const std::string source1 = "source1";
  const std::string source2 = "source2";

  // Add the files to the database.
  GetDlpAdaptor()->AddFile(
      CreateSerializedAddFileRequest(file_path1.value(), source1, "referrer1"));
  GetDlpAdaptor()->AddFile(
      CreateSerializedAddFileRequest(file_path2.value(), source2, "referrer2"));

  std::vector<uint8_t> request_proto_blob(request.ByteSizeLong());
  request.SerializeToArray(request_proto_blob.data(),
                           request_proto_blob.size());
  std::vector<uint8_t> response_proto_blob =
      GetDlpAdaptor()->GetFilesSources(std::move(request_proto_blob));
  GetFilesSourcesResponse response;
  response.ParseFromArray(response_proto_blob.data(),
                          response_proto_blob.size());

  ASSERT_EQ(response.files_metadata_size(), 2u);

  FileMetadata file_metadata1 = response.files_metadata()[0];
  EXPECT_EQ(file_metadata1.inode(), inode1);
  EXPECT_EQ(file_metadata1.source_url(), source1);

  FileMetadata file_metadata2 = response.files_metadata()[1];
  EXPECT_EQ(file_metadata2.inode(), inode2);
  EXPECT_EQ(file_metadata2.source_url(), source2);
}

TEST_F(DlpAdaptorTest, GetFilesSourcesWithoutDatabase) {
  // Create files to request sources by inodes.
  base::FilePath file_path1;
  ASSERT_TRUE(base::CreateTemporaryFile(&file_path1));
  const ino_t inode1 = GetDlpAdaptor()->GetInodeValue(file_path1.value());
  base::FilePath file_path2;
  ASSERT_TRUE(base::CreateTemporaryFile(&file_path2));
  const ino_t inode2 = GetDlpAdaptor()->GetInodeValue(file_path2.value());

  GetFilesSourcesRequest request;
  request.add_files_inodes(inode1);
  request.add_files_inodes(inode2);

  const std::string source1 = "source1";
  const std::string source2 = "source2";

  // Add the files to the database.
  GetDlpAdaptor()->AddFile(
      CreateSerializedAddFileRequest(file_path1.value(), source1, "referrer1"));
  GetDlpAdaptor()->AddFile(
      CreateSerializedAddFileRequest(file_path2.value(), source2, "referrer2"));

  std::vector<uint8_t> request_proto_blob(request.ByteSizeLong());
  request.SerializeToArray(request_proto_blob.data(),
                           request_proto_blob.size());
  std::vector<uint8_t> response_proto_blob =
      GetDlpAdaptor()->GetFilesSources(std::move(request_proto_blob));
  GetFilesSourcesResponse response;
  response.ParseFromArray(response_proto_blob.data(),
                          response_proto_blob.size());

  EXPECT_EQ(response.files_metadata_size(), 0u);
}

TEST_F(DlpAdaptorTest, SetDlpFilesPolicy) {
  SetDlpFilesPolicyRequest request;
  request.add_rules();
  std::vector<uint8_t> proto_blob(request.ByteSizeLong());
  request.SerializeToArray(proto_blob.data(), proto_blob.size());

  std::vector<uint8_t> response_blob =
      GetDlpAdaptor()->SetDlpFilesPolicy(proto_blob);

  RequestFileAccessResponse response;
  response.ParseFromArray(response_blob.data(), response_blob.size());

  EXPECT_FALSE(response.has_error_message());
}

}  // namespace dlp
