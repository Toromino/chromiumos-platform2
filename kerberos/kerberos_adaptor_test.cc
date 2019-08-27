// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "kerberos/kerberos_adaptor.h"

#include <memory>
#include <utility>

#include <base/files/scoped_temp_dir.h>
#include <base/memory/ref_counted.h>
#include <base/run_loop.h>
#include <brillo/asan.h>
#include <dbus/login_manager/dbus-constants.h>
#include <dbus/mock_bus.h>
#include <dbus/mock_exported_object.h>
#include <dbus/mock_object_proxy.h>
#include <dbus/object_path.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "kerberos/account_manager.h"
#include "kerberos/fake_krb5_interface.h"
#include "kerberos/kerberos_metrics.h"
#include "kerberos/krb5_jail_wrapper.h"
#include "kerberos/platform_helper.h"
#include "kerberos/proto_bindings/kerberos_service.pb.h"

using brillo::dbus_utils::DBusObject;
using dbus::MockBus;
using dbus::MockExportedObject;
using dbus::MockObjectProxy;
using dbus::ObjectPath;
using testing::_;
using testing::AnyNumber;
using testing::Invoke;
using testing::Mock;
using testing::NiceMock;
using testing::Return;
using ByteArray = kerberos::KerberosAdaptor::ByteArray;

namespace kerberos {
namespace {

// Some arbitrary D-Bus message serial number. Required for mocking D-Bus calls.
const int kDBusSerial = 123;

// Stub user data.
constexpr char kUser[] = "user";
constexpr char kUserHash[] = "user-hash";
constexpr char kPrincipalName[] = "user@REALM.COM";
constexpr char kPassword[] = "hello123";

// Stub D-Bus object path for the mock daemon.
constexpr char kObjectPath[] = "/object/path";

// Real storage base dir.
constexpr char KDaemonStore[] = "/run/daemon-store/kerberosd";

// Empty Kerberos configuration.
constexpr char kEmptyConfig[] = "";

class MockMetrics : public KerberosMetrics {
 public:
  explicit MockMetrics(const base::FilePath& storage_dir)
      : KerberosMetrics(storage_dir) {}
  ~MockMetrics() override = default;

  MOCK_METHOD0(StartAcquireTgtTimer, void());
  MOCK_METHOD0(StopAcquireTgtTimerAndReport, void());
  MOCK_METHOD1(ReportValidateConfigErrorCode, void(ConfigErrorCode));
  MOCK_METHOD2(ReportDBusCallResult, void(const char*, ErrorType));
  MOCK_METHOD0(ShouldReportDailyUsageStats, bool());

 private:
  DISALLOW_COPY_AND_ASSIGN(MockMetrics);
};

// Stub completion callback for RegisterAsync().
void DoNothing(bool /* unused */) {}

// Serializes |message| as byte array.
ByteArray SerializeAsBlob(const google::protobuf::MessageLite& message) {
  ByteArray result;
  result.resize(message.ByteSize());
  CHECK(message.SerializeToArray(result.data(), result.size()));
  return result;
}

// Parses a response message from a byte array.
template <typename TResponse>
TResponse ParseResponse(const ByteArray& response_blob) {
  TResponse response;
  EXPECT_TRUE(
      response.ParseFromArray(response_blob.data(), response_blob.size()));
  return response;
}

// Stub RetrievePrimarySession Session Manager method.
dbus::Response* StubRetrievePrimarySession(dbus::MethodCall* method_call,
                                           int /* timeout_ms */,
                                           dbus::ScopedDBusError* /* error */) {
  // Respond with username = kUser and sanitized_username = kUserHash.
  method_call->SetSerial(kDBusSerial);
  auto response = dbus::Response::FromMethodCall(method_call);
  dbus::MessageWriter writer(response.get());
  writer.AppendString(kUser);
  writer.AppendString(kUserHash);

  // Note: The mock wraps this back into a std::unique_ptr.
  return response.release();
}

}  // namespace

class KerberosAdaptorTest : public ::testing::Test {
 public:
  KerberosAdaptorTest() = default;
  ~KerberosAdaptorTest() override = default;

  void SetUp() override {
    ::testing::Test::SetUp();

    mock_bus_ = make_scoped_refptr(new MockBus(dbus::Bus::Options()));

    // Mock out D-Bus initialization.
    const ObjectPath object_path(kObjectPath);
    mock_exported_object_ = make_scoped_refptr(
        new MockExportedObject(mock_bus_.get(), object_path));
    EXPECT_CALL(*mock_bus_, GetExportedObject(object_path))
        .WillRepeatedly(Return(mock_exported_object_.get()));
    EXPECT_CALL(*mock_exported_object_, Unregister()).Times(AnyNumber());
    EXPECT_CALL(*mock_exported_object_, ExportMethod(_, _, _, _))
        .Times(AnyNumber());
    EXPECT_CALL(*mock_exported_object_, SendSignal(_))
        .WillRepeatedly(
            Invoke(this, &KerberosAdaptorTest::OnKerberosFilesChanged));

    // Create temp directory for files written during tests.
    CHECK(storage_dir_.CreateUniqueTempDir());

    // Create mock metrics.
    auto metrics =
        std::make_unique<NiceMock<MockMetrics>>(storage_dir_.GetPath());
    metrics_ = metrics.get();
    ON_CALL(*metrics_, ShouldReportDailyUsageStats)
        .WillByDefault(Return(false));

    // Create KerberosAdaptor instance. Do this AFTER creating the proxy mocks
    // since they might be accessed during initialization.
    auto dbus_object =
        std::make_unique<DBusObject>(nullptr, mock_bus_, object_path);
    adaptor_ = std::make_unique<KerberosAdaptor>(std::move(dbus_object));
    adaptor_->set_storage_dir_for_testing(storage_dir_.GetPath());
    adaptor_->set_metrics_for_testing(std::move(metrics));
    adaptor_->set_krb5_for_testing(std::make_unique<FakeKrb5Interface>());
    adaptor_->RegisterAsync(base::BindRepeating(&DoNothing));
  }

  void TearDown() override { adaptor_.reset(); }

 protected:
  void OnKerberosFilesChanged(dbus::Signal* signal) {
    EXPECT_EQ(signal->GetInterface(), "org.chromium.Kerberos");
    EXPECT_EQ(signal->GetMember(), "KerberosFilesChanged");
    dbus::MessageReader reader(signal);
    std::string principal_name;
    EXPECT_TRUE(reader.PopString(&principal_name));
    EXPECT_EQ(kPrincipalName, principal_name);
  }

  // Adds a default account.
  ErrorType AddAccount() {
    AddAccountRequest request;
    request.set_principal_name(kPrincipalName);
    request.set_is_managed(false);
    ByteArray response_blob = adaptor_->AddAccount(SerializeAsBlob(request));
    return ParseResponse<AddAccountResponse>(response_blob).error();
  }

  // Removes the default account.
  ErrorType RemoveAccount() {
    RemoveAccountRequest request;
    request.set_principal_name(kPrincipalName);
    ByteArray response_blob = adaptor_->RemoveAccount(SerializeAsBlob(request));
    return ParseResponse<RemoveAccountResponse>(response_blob).error();
  }

  // Removes all accounts.
  ErrorType ClearAccounts() {
    ClearAccountsRequest request;
    ByteArray response_blob = adaptor_->ClearAccounts(SerializeAsBlob(request));
    return ParseResponse<ClearAccountsResponse>(response_blob).error();
  }

  // Lists accounts.
  ErrorType ListAccounts() {
    ListAccountsRequest request;
    ByteArray response_blob = adaptor_->ListAccounts(SerializeAsBlob(request));
    return ParseResponse<ListAccountsResponse>(response_blob).error();
  }

  // Sets a default config.
  ErrorType SetConfig() {
    SetConfigRequest request;
    request.set_principal_name(kPrincipalName);
    request.set_krb5conf(kEmptyConfig);
    ByteArray response_blob = adaptor_->SetConfig(SerializeAsBlob(request));
    return ParseResponse<SetConfigResponse>(response_blob).error();
  }

  // Validates a default config.
  ErrorType ValidateConfig() {
    ValidateConfigRequest request;
    request.set_krb5conf(kEmptyConfig);
    ByteArray response_blob =
        adaptor_->ValidateConfig(SerializeAsBlob(request));
    return ParseResponse<ValidateConfigResponse>(response_blob).error();
  }

  // Acquires a default Kerberos ticket.
  ErrorType AcquireKerberosTgt() {
    AcquireKerberosTgtRequest request;
    request.set_principal_name(kPrincipalName);
    ByteArray response_blob = adaptor_->AcquireKerberosTgt(
        SerializeAsBlob(request), WriteStringToPipe(kPassword));
    return ParseResponse<AcquireKerberosTgtResponse>(response_blob).error();
  }

  // Acquires a default Kerberos ticket.
  ErrorType GetKerberosFiles() {
    GetKerberosFilesRequest request;
    request.set_principal_name(kPrincipalName);
    ByteArray response_blob =
        adaptor_->GetKerberosFiles(SerializeAsBlob(request));
    return ParseResponse<GetKerberosFilesResponse>(response_blob).error();
  }

  // KEEP ORDER between these. It's important for destruction.
  scoped_refptr<MockBus> mock_bus_;
  scoped_refptr<MockExportedObject> mock_exported_object_;
  std::unique_ptr<KerberosAdaptor> adaptor_;
  base::MessageLoop loop_;

  base::ScopedTempDir storage_dir_;

  NiceMock<MockMetrics>* metrics_ = nullptr;

 private:
  DISALLOW_COPY_AND_ASSIGN(KerberosAdaptorTest);
};

// RetrievePrimarySession is called to figure out the proper storage dir if the
// dir is NOT overwritten by KerberosAdaptor::set_storage_dir_for_testing().
TEST_F(KerberosAdaptorTest, RetrievesPrimarySession) {
  // Stub out Session Manager's RetrievePrimarySession D-Bus method.
  auto mock_session_manager_proxy = make_scoped_refptr(new MockObjectProxy(
      mock_bus_.get(), login_manager::kSessionManagerServiceName,
      dbus::ObjectPath(login_manager::kSessionManagerServicePath)));
  EXPECT_CALL(*mock_bus_,
              GetObjectProxy(login_manager::kSessionManagerServiceName, _))
      .WillOnce(Return(mock_session_manager_proxy.get()));
  EXPECT_CALL(*mock_session_manager_proxy,
              MockCallMethodAndBlockWithErrorDetails(_, _, _))
      .WillOnce(Invoke(&StubRetrievePrimarySession));

  // Recreate an adaptor, but don't call set_storage_dir_for_testing().
  auto dbus_object =
      std::make_unique<DBusObject>(nullptr, mock_bus_, ObjectPath(kObjectPath));
  auto adaptor = std::make_unique<KerberosAdaptor>(std::move(dbus_object));
  adaptor->RegisterAsync(base::BindRepeating(&DoNothing));

  // Check if the right storage dir is set.
  EXPECT_EQ(base::FilePath(KDaemonStore).Append(kUserHash),
            adaptor->GetAccountManagerForTesting()->GetStorageDirForTesting());
}

// AddAccount and RemoveAccount succeed when a new account is added and removed.
TEST_F(KerberosAdaptorTest, AddRemoveAccountSucceess) {
  EXPECT_EQ(ERROR_NONE, AddAccount());
  EXPECT_EQ(ERROR_NONE, RemoveAccount());
}

#if defined(BRILLO_ASAN_BUILD)
// This test is failing on ASan bots: https://crbug.com/991316.
#define MAYBE_Metrics_ReportDBusCallResult DISABLED_Metrics_ReportDBusCallResult
#else
#define MAYBE_Metrics_ReportDBusCallResult Metrics_ReportDBusCallResult
#endif

// Checks that metrics are reported for all D-Bus calls.
TEST_F(KerberosAdaptorTest, MAYBE_Metrics_ReportDBusCallResult) {
  EXPECT_CALL(*metrics_, ReportDBusCallResult("AddAccount", ERROR_NONE));
  EXPECT_CALL(*metrics_, ReportDBusCallResult("ListAccounts", ERROR_NONE));
  EXPECT_CALL(*metrics_, ReportDBusCallResult("SetConfig", ERROR_NONE));
  EXPECT_CALL(*metrics_, ReportDBusCallResult("ValidateConfig", ERROR_NONE));
  EXPECT_CALL(*metrics_,
              ReportDBusCallResult("AcquireKerberosTgt", ERROR_NONE));
  EXPECT_CALL(*metrics_, ReportDBusCallResult("GetKerberosFiles", ERROR_NONE));
  EXPECT_CALL(*metrics_, ReportDBusCallResult("RemoveAccount", ERROR_NONE));
  EXPECT_CALL(*metrics_, ReportDBusCallResult("ClearAccounts", ERROR_NONE));

  EXPECT_EQ(ERROR_NONE, AddAccount());
  EXPECT_EQ(ERROR_NONE, ListAccounts());
  EXPECT_EQ(ERROR_NONE, SetConfig());
  EXPECT_EQ(ERROR_NONE, ValidateConfig());
  EXPECT_EQ(ERROR_NONE, AcquireKerberosTgt());
  EXPECT_EQ(ERROR_NONE, GetKerberosFiles());
  EXPECT_EQ(ERROR_NONE, RemoveAccount());
  EXPECT_EQ(ERROR_NONE, ClearAccounts());
}

// AcquireKerberosTgt should trigger timing events.
TEST_F(KerberosAdaptorTest, Metrics_AcquireTgtTimer) {
  EXPECT_CALL(*metrics_, StartAcquireTgtTimer());
  EXPECT_CALL(*metrics_, StopAcquireTgtTimerAndReport());
  EXPECT_EQ(ERROR_UNKNOWN_PRINCIPAL_NAME, AcquireKerberosTgt());
}

// AcquireKerberosTgt should trigger timing events.
TEST_F(KerberosAdaptorTest, Metrics_ValidateConfigErrorCode) {
  EXPECT_CALL(*metrics_, ReportValidateConfigErrorCode(CONFIG_ERROR_NONE));
  EXPECT_EQ(ERROR_NONE, AddAccount());
  EXPECT_EQ(ERROR_NONE, ValidateConfig());
}

// TODO(https://crbug.com/952247): Add more tests.

}  // namespace kerberos
