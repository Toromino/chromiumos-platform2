// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/provision_device_state_handler.h"

#include <base/memory/scoped_refptr.h>
#include <base/test/task_environment.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "rmad/constants.h"
#include "rmad/proto_bindings/rmad.pb.h"
#include "rmad/state_handler/state_handler_test_common.h"
#include "rmad/utils/json_store.h"
#include "rmad/utils/mock_cbi_utils.h"
#include "rmad/utils/mock_cros_config_utils.h"
#include "rmad/utils/mock_ssfc_utils.h"
#include "rmad/utils/mock_vpd_utils.h"

using testing::_;
using testing::DoAll;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::SetArgPointee;
using testing::WithArg;

namespace {

constexpr char kTestModelName[] = "TestModelName";
constexpr uint32_t kTestSSFC = 0x1234;

}  // namespace

namespace rmad {

class ProvisionDeviceStateHandlerTest : public StateHandlerTest {
 public:
  // Helper class to mock the callback function to send signal.
  class SignalSender {
   public:
    MOCK_METHOD(bool,
                SendProvisionProgressSignal,
                (const ProvisionStatus&),
                (const));
  };

  void QueueStatus(const ProvisionStatus& status) {
    status_hitory_.push_back(status);
  }

  scoped_refptr<ProvisionDeviceStateHandler> CreateStateHandler(
      bool get_model_name = true,
      bool need_update_ssfc = true,
      bool set_ssfc = true,
      bool set_stable_dev_secret = true,
      bool flush_vpd = true) {
    // Expect signal is always sent.
    ON_CALL(signal_sender_, SendProvisionProgressSignal(_))
        .WillByDefault(
            DoAll(WithArg<0>(Invoke(
                      this, &ProvisionDeviceStateHandlerTest::QueueStatus)),
                  Return(true)));

    auto cros_config_utils = std::make_unique<NiceMock<MockCrosConfigUtils>>();
    if (get_model_name) {
      ON_CALL(*cros_config_utils, GetModelName(_))
          .WillByDefault(DoAll(SetArgPointee<0>(std::string(kTestModelName)),
                               Return(true)));
    } else {
      ON_CALL(*cros_config_utils, GetModelName(_)).WillByDefault(Return(false));
    }

    auto ssfc_utils = std::make_unique<NiceMock<MockSsfcUtils>>();
    auto cbi_utils = std::make_unique<NiceMock<MockCbiUtils>>();

    if (need_update_ssfc) {
      ON_CALL(*ssfc_utils, GetSSFC(_, _, _))
          .WillByDefault(DoAll(SetArgPointee<1>(true),
                               SetArgPointee<2>(kTestSSFC), Return(true)));
      ON_CALL(*cbi_utils, SetSSFC(_)).WillByDefault(Return(set_ssfc));
    } else {
      ON_CALL(*ssfc_utils, GetSSFC(_, _, _))
          .WillByDefault(DoAll(SetArgPointee<1>(false), Return(true)));
    }

    auto vpd_utils = std::make_unique<NiceMock<MockVpdUtils>>();
    ON_CALL(*vpd_utils, SetStableDeviceSecret(_))
        .WillByDefault(Return(set_stable_dev_secret));
    ON_CALL(*vpd_utils, FlushOutRoVpdCache()).WillByDefault(Return(flush_vpd));

    // Fake |HardwareVerifierUtils|.
    auto handler = base::MakeRefCounted<ProvisionDeviceStateHandler>(
        json_store_, std::move(cbi_utils), std::move(cros_config_utils),
        std::move(ssfc_utils), std::move(vpd_utils));
    auto callback =
        std::make_unique<base::RepeatingCallback<bool(const ProvisionStatus&)>>(
            base::BindRepeating(&SignalSender::SendProvisionProgressSignal,
                                base::Unretained(&signal_sender_)));
    handler->RegisterSignalSender(std::move(callback));
    return handler;
  }

  void RunHandlerTaskRunner(
      scoped_refptr<ProvisionDeviceStateHandler> handler) {
    handler->GetTaskRunner()->PostTask(FROM_HERE, run_loop_.QuitClosure());
    run_loop_.Run();
  }

 protected:
  NiceMock<SignalSender> signal_sender_;
  std::vector<ProvisionStatus> status_hitory_;

  // Variables for TaskRunner.
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::ThreadPoolExecutionMode::ASYNC,
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  base::RunLoop run_loop_;
};

TEST_F(ProvisionDeviceStateHandlerTest, InitializeState_Success) {
  auto handler = CreateStateHandler();
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  RunHandlerTaskRunner(handler);
}

TEST_F(ProvisionDeviceStateHandlerTest, Clenaup_Success) {
  auto handler = CreateStateHandler();
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  handler->CleanUpState();
  RunHandlerTaskRunner(handler);
}

TEST_F(ProvisionDeviceStateHandlerTest, InitializeState_NoSignalSenderFailed) {
  auto handler = CreateStateHandler();
  handler->RegisterSignalSender(nullptr);
  EXPECT_EQ(handler->InitializeState(),
            RMAD_ERROR_STATE_HANDLER_INITIALIZATION_FAILED);
  RunHandlerTaskRunner(handler);
}

TEST_F(ProvisionDeviceStateHandlerTest, GetNextStateCase_Success) {
  auto handler = CreateStateHandler();
  json_store_->SetValue(kSameOwner, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  task_environment_.FastForwardBy(
      ProvisionDeviceStateHandler::kReportStatusInterval);
  EXPECT_GE(status_hitory_.size(), 1);
  EXPECT_EQ(status_hitory_.back().status(),
            ProvisionStatus::RMAD_PROVISION_STATUS_COMPLETE);

  auto provision = std::make_unique<ProvisionDeviceState>();
  provision->set_choice(ProvisionDeviceState::RMAD_PROVISION_CHOICE_CONTINUE);
  RmadState state;
  state.set_allocated_provision_device(provision.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kFinalize);

  RunHandlerTaskRunner(handler);
}

TEST_F(ProvisionDeviceStateHandlerTest,
       GetNextStateCase_KeepDeviceOpenSuccess) {
  auto handler = CreateStateHandler();
  json_store_->SetValue(kSameOwner, false);
  json_store_->SetValue(kKeepDeviceOpen, true);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  task_environment_.FastForwardBy(
      ProvisionDeviceStateHandler::kReportStatusInterval);
  EXPECT_GE(status_hitory_.size(), 1);
  EXPECT_EQ(status_hitory_.back().status(),
            ProvisionStatus::RMAD_PROVISION_STATUS_COMPLETE);

  auto provision = std::make_unique<ProvisionDeviceState>();
  provision->set_choice(ProvisionDeviceState::RMAD_PROVISION_CHOICE_CONTINUE);
  RmadState state;
  state.set_allocated_provision_device(provision.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kWpEnablePhysical);

  RunHandlerTaskRunner(handler);
}

TEST_F(ProvisionDeviceStateHandlerTest, GetNextStateCase_Wait) {
  auto handler = CreateStateHandler();
  json_store_->SetValue(kSameOwner, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  EXPECT_EQ(status_hitory_.size(), 0);

  auto provision = std::make_unique<ProvisionDeviceState>();
  provision->set_choice(ProvisionDeviceState::RMAD_PROVISION_CHOICE_CONTINUE);
  RmadState state;
  state.set_allocated_provision_device(provision.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_WAIT);
  EXPECT_EQ(state_case, RmadState::StateCase::kProvisionDevice);

  RunHandlerTaskRunner(handler);
}

TEST_F(ProvisionDeviceStateHandlerTest,
       GetNextStateCase_UnknownDestinationFailedBlocking) {
  auto handler = CreateStateHandler();
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  task_environment_.FastForwardBy(
      ProvisionDeviceStateHandler::kReportStatusInterval);
  EXPECT_GE(status_hitory_.size(), 1);
  EXPECT_EQ(status_hitory_.back().status(),
            ProvisionStatus::RMAD_PROVISION_STATUS_FAILED_BLOCKING);

  auto provision = std::make_unique<ProvisionDeviceState>();
  provision->set_choice(ProvisionDeviceState::RMAD_PROVISION_CHOICE_CONTINUE);
  RmadState state;
  state.set_allocated_provision_device(provision.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_PROVISIONING_FAILED);
  EXPECT_EQ(state_case, RmadState::StateCase::kProvisionDevice);

  RunHandlerTaskRunner(handler);
}

TEST_F(ProvisionDeviceStateHandlerTest, GetNextStateCase_Retry) {
  auto handler = CreateStateHandler();
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  task_environment_.FastForwardBy(
      ProvisionDeviceStateHandler::kReportStatusInterval);
  EXPECT_GE(status_hitory_.size(), 1);
  EXPECT_EQ(status_hitory_.back().status(),
            ProvisionStatus::RMAD_PROVISION_STATUS_FAILED_BLOCKING);

  auto provision = std::make_unique<ProvisionDeviceState>();
  provision->set_choice(ProvisionDeviceState::RMAD_PROVISION_CHOICE_RETRY);
  RmadState state;
  state.set_allocated_provision_device(provision.release());

  json_store_->SetValue(kSameOwner, false);

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_WAIT);
  EXPECT_EQ(state_case, RmadState::StateCase::kProvisionDevice);

  task_environment_.FastForwardBy(
      ProvisionDeviceStateHandler::kReportStatusInterval);
  EXPECT_GE(status_hitory_.size(), 2);
  EXPECT_EQ(status_hitory_.back().status(),
            ProvisionStatus::RMAD_PROVISION_STATUS_COMPLETE);

  RunHandlerTaskRunner(handler);
}

TEST_F(ProvisionDeviceStateHandlerTest,
       GetNextStateCase_SetStableDeviceSecretFailedBlocking) {
  auto handler = CreateStateHandler(true, true, true, false, true);
  json_store_->SetValue(kSameOwner, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  task_environment_.FastForwardBy(
      ProvisionDeviceStateHandler::kReportStatusInterval);
  EXPECT_GE(status_hitory_.size(), 1);
  EXPECT_EQ(status_hitory_.back().status(),
            ProvisionStatus::RMAD_PROVISION_STATUS_FAILED_BLOCKING);

  RunHandlerTaskRunner(handler);
}

TEST_F(ProvisionDeviceStateHandlerTest,
       GetNextStateCase_GetModelNameFailedBlocking) {
  auto handler = CreateStateHandler(false, true, true, true, true);
  json_store_->SetValue(kSameOwner, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  task_environment_.FastForwardBy(
      ProvisionDeviceStateHandler::kReportStatusInterval);
  EXPECT_GE(status_hitory_.size(), 1);
  EXPECT_EQ(status_hitory_.back().status(),
            ProvisionStatus::RMAD_PROVISION_STATUS_FAILED_BLOCKING);

  RunHandlerTaskRunner(handler);
}

TEST_F(ProvisionDeviceStateHandlerTest,
       GetNextStateCase_SsfcNotRequiredSuccess) {
  auto handler = CreateStateHandler(true, false, true, true, true);
  json_store_->SetValue(kSameOwner, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  task_environment_.FastForwardBy(
      ProvisionDeviceStateHandler::kReportStatusInterval);
  EXPECT_GE(status_hitory_.size(), 1);
  EXPECT_EQ(status_hitory_.back().status(),
            ProvisionStatus::RMAD_PROVISION_STATUS_COMPLETE);

  auto provision = std::make_unique<ProvisionDeviceState>();
  provision->set_choice(ProvisionDeviceState::RMAD_PROVISION_CHOICE_CONTINUE);
  RmadState state;
  state.set_allocated_provision_device(provision.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kFinalize);

  RunHandlerTaskRunner(handler);
}

TEST_F(ProvisionDeviceStateHandlerTest,
       GetNextStateCase_SetSsfcFailedBlocking) {
  auto handler = CreateStateHandler(true, true, false, true, true);
  json_store_->SetValue(kSameOwner, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  task_environment_.FastForwardBy(
      ProvisionDeviceStateHandler::kReportStatusInterval);
  EXPECT_GE(status_hitory_.size(), 1);
  EXPECT_EQ(status_hitory_.back().status(),
            ProvisionStatus::RMAD_PROVISION_STATUS_FAILED_BLOCKING);

  RunHandlerTaskRunner(handler);
}

TEST_F(ProvisionDeviceStateHandlerTest,
       GetNextStateCase_VpdFlushFailedBlocking) {
  auto handler = CreateStateHandler(true, true, true, true, false);
  json_store_->SetValue(kSameOwner, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
  task_environment_.FastForwardBy(
      ProvisionDeviceStateHandler::kReportStatusInterval);
  EXPECT_GE(status_hitory_.size(), 1);
  EXPECT_EQ(status_hitory_.back().status(),
            ProvisionStatus::RMAD_PROVISION_STATUS_FAILED_BLOCKING);

  RunHandlerTaskRunner(handler);
}

TEST_F(ProvisionDeviceStateHandlerTest, GetNextStateCase_MissingState) {
  auto handler = CreateStateHandler();
  json_store_->SetValue(kSameOwner, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  // No WelcomeScreenState.
  RmadState state;

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_REQUEST_INVALID);
  EXPECT_EQ(state_case, RmadState::StateCase::kProvisionDevice);

  RunHandlerTaskRunner(handler);
}

TEST_F(ProvisionDeviceStateHandlerTest, GetNextStateCase_MissingArgs) {
  auto handler = CreateStateHandler();
  json_store_->SetValue(kSameOwner, false);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  auto provision = std::make_unique<ProvisionDeviceState>();
  provision->set_choice(ProvisionDeviceState::RMAD_PROVISION_CHOICE_UNKNOWN);
  RmadState state;
  state.set_allocated_provision_device(provision.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_REQUEST_ARGS_MISSING);
  EXPECT_EQ(state_case, RmadState::StateCase::kProvisionDevice);

  RunHandlerTaskRunner(handler);
}

}  // namespace rmad
