// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "minios/key_reader.h"
#include "minios/mock_draw_interface.h"
#include "minios/mock_network_manager.h"
#include "minios/mock_screen_controller.h"
#include "minios/screen_network.h"

using ::testing::NiceMock;

namespace minios {

class ScreenNetworkTest : public ::testing::Test {
 protected:
  std::shared_ptr<NetworkManagerInterface> mock_network_manager_ =
      std::make_shared<NiceMock<MockNetworkManager>>();
  std::shared_ptr<DrawInterface> mock_draw_interface_ =
      std::make_shared<NiceMock<MockDrawInterface>>();
  NiceMock<MockScreenControllerInterface> mock_screen_controller_;
  ScreenNetwork screen_network_{mock_draw_interface_, mock_network_manager_,
                                &mock_screen_controller_};
};

TEST_F(ScreenNetworkTest, InvalidNetwork) {
  // Get to dropdown screen and set list of available networks.
  screen_network_.OnKeyPress(kKeyEnter);
  EXPECT_EQ(ScreenType::kExpandedNetworkDropDownScreen,
            screen_network_.GetType());
  screen_network_.OnGetNetworks({"network"}, nullptr);
  screen_network_.SetIndexForTest(2);

  // Resets index because the network chosen was invalid.
  screen_network_.OnKeyPress(kKeyEnter);
  EXPECT_EQ(0, screen_network_.GetIndexForTest());
}

TEST_F(ScreenNetworkTest, GetNetworks) {
  screen_network_.OnGetNetworks({"test1", "test2", "test3"}, nullptr);

  // Network error.
  brillo::ErrorPtr error_ptr =
      brillo::Error::Create(FROM_HERE, "HTTP", "404", "Not found", nullptr);

  // Reset and show error screen.
  EXPECT_CALL(mock_screen_controller_, OnError(ScreenType::kNetworkError));
  screen_network_.OnGetNetworks({}, error_ptr.get());
}

TEST_F(ScreenNetworkTest, GetNetworksRefresh) {
  screen_network_.OnKeyPress(kKeyEnter);
  EXPECT_EQ(ScreenType::kExpandedNetworkDropDownScreen,
            screen_network_.GetType());
  // Menu count is updated amd drop down screen is refreshed.
  screen_network_.OnGetNetworks({"test1", "test2", "test3"}, nullptr);
  // Update button when "refreshing" to the expanded dropdown screen.
  EXPECT_EQ(screen_network_.GetButtonCountForTest(), 4);
}

TEST_F(ScreenNetworkTest, EnterOnDropDown) {
  // If dropdown has not been selected yet, the focus is on the normal buttons.
  screen_network_.OnKeyPress(kKeyDown);
  EXPECT_CALL(mock_screen_controller_, OnBackward(testing::_));
  screen_network_.OnKeyPress(kKeyEnter);

  // Set networks.
  screen_network_.OnGetNetworks({"test1", "test2", "test3"}, nullptr);

  // Select dropdown.
  screen_network_.OnKeyPress(kKeyUp);
  screen_network_.OnKeyPress(kKeyEnter);
  EXPECT_EQ(ScreenType::kExpandedNetworkDropDownScreen,
            screen_network_.GetType());

  // Pick second network.
  screen_network_.OnKeyPress(kKeyDown);
  screen_network_.OnKeyPress(kKeyEnter);

  EXPECT_EQ(screen_network_.GetIndexForTest(), 1);
}

}  // namespace minios