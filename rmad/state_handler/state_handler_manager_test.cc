// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <base/files/file_path.h>
#include <base/memory/scoped_refptr.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "rmad/state_handler/mock_state_handler.h"
#include "rmad/state_handler/state_handler_manager.h"
#include "rmad/utils/json_store.h"

namespace rmad {

using testing::_;
using testing::DoAll;
using testing::NiceMock;
using testing::Return;
using testing::SetArgPointee;

class StateHandlerManagerTest : public testing::Test {
 public:
  StateHandlerManagerTest() {
    json_store_ = base::MakeRefCounted<JsonStore>(base::FilePath(""));
    state_handler_manager_ = std::make_unique<StateHandlerManager>(json_store_);
  }

  scoped_refptr<BaseStateHandler> CreateMockStateHandler(RmadState state,
                                                         RmadState next_state) {
    auto handler =
        base::MakeRefCounted<NiceMock<MockStateHandler>>(json_store_);
    ON_CALL(*handler, GetState()).WillByDefault(Return(state));
    ON_CALL(*handler, GetNextState(_))
        .WillByDefault(DoAll(SetArgPointee<0>(next_state), Return(true)));
    return handler;
  }

 protected:
  scoped_refptr<JsonStore> json_store_;
  std::unique_ptr<StateHandlerManager> state_handler_manager_;
};

TEST_F(StateHandlerManagerTest, GetStateHandler) {
  auto handler1 =
      CreateMockStateHandler(RMAD_STATE_RMA_NOT_REQUIRED, RMAD_STATE_UNKNOWN);
  auto handler2 =
      CreateMockStateHandler(RMAD_STATE_WELCOME_SCREEN, RMAD_STATE_UNKNOWN);
  state_handler_manager_->RegisterStateHandler(handler1);
  state_handler_manager_->RegisterStateHandler(handler2);

  scoped_refptr<BaseStateHandler> nonexistent_handler =
      state_handler_manager_->GetStateHandler(RMAD_STATE_UNKNOWN);
  EXPECT_FALSE(nonexistent_handler.get());

  scoped_refptr<BaseStateHandler> retrieve_handler =
      state_handler_manager_->GetStateHandler(RMAD_STATE_WELCOME_SCREEN);
  EXPECT_TRUE(retrieve_handler.get());
  EXPECT_EQ(RMAD_STATE_WELCOME_SCREEN, retrieve_handler->GetState());
  RmadState next_state;
  EXPECT_EQ(true, retrieve_handler->GetNextState(&next_state));
  EXPECT_EQ(RMAD_STATE_UNKNOWN, next_state);
}

TEST_F(StateHandlerManagerTest, RegisterStateHandlerCollision) {
  auto handler1 =
      CreateMockStateHandler(RMAD_STATE_RMA_NOT_REQUIRED, RMAD_STATE_UNKNOWN);
  auto handler2 = CreateMockStateHandler(RMAD_STATE_RMA_NOT_REQUIRED,
                                         RMAD_STATE_WELCOME_SCREEN);
  state_handler_manager_->RegisterStateHandler(handler1);
  EXPECT_DEATH(state_handler_manager_->RegisterStateHandler(handler2),
               "Registered handlers should have unique RmadStates.");
}

}  // namespace rmad