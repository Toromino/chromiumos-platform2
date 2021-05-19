// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MINIOS_SCREEN_CONTROLLER_H_
#define MINIOS_SCREEN_CONTROLLER_H_

#include <memory>
#include <string>
#include <vector>

#include "minios/draw_utils.h"
#include "minios/key_reader.h"
#include "minios/process_manager.h"
#include "minios/screen_controller_interface.h"
#include "minios/screen_interface.h"

namespace minios {

class ScreenController : public ScreenControllerInterface,
                         public KeyReader::Delegate {
 public:
  explicit ScreenController(std::shared_ptr<DrawInterface> draw_utils);
  virtual ~ScreenController() = default;

  ScreenController(const ScreenController&) = delete;
  ScreenController& operator=(const ScreenController&) = delete;

  void Init();

  // ScreenControllerInterface overrides.
  // Called by screens when the user presses the next or continue button.
  void OnForward(ScreenInterface* screen) override;

  // Called by screens when the user presses the back or cancel button.
  void OnBackward(ScreenInterface* screen) override;

  // Changes to the `kLanguageDropDownScreen` class.
  void SwitchLocale(ScreenInterface* screen) override;

  // Returns to the original screen and updates UI elements based on locale
  // change.
  void UpdateLocale(ScreenInterface* screen,
                    int selected_locale_index) override;

  ScreenType GetCurrentScreenForTest() { return current_screen_->GetType(); }

  void SetCurrentScreenForTest(ScreenType current_screen) {
    current_screen_ = CreateScreen(current_screen);
  }

  std::unique_ptr<ScreenInterface> GetCurrentScreenPtrForTest(
      ScreenType current_screen) {
    return CreateScreen(current_screen);
  }

 private:
  // Creates each class ptr as needed.
  std::unique_ptr<ScreenInterface> CreateScreen(ScreenType screen);

  // This function overloads Delegate. It is only called when the key is
  // valid and updates the key state for the given fd and key. Calls
  // `SwitchState` to update the flow once key is recorded as being pressed
  // and released.
  void OnKeyPress(int fd_index, int key_changed, bool key_released) override;

  KeyReader key_reader_;

  ProcessManager process_manager_;

  std::shared_ptr<DrawInterface> draw_utils_;

  // Records the key press for each fd and key, where the index of the fd is the
  // row and the key code the column. Resets to false after key is released.
  // Only tracks the valid keys.
  std::vector<std::vector<bool>> key_states_;

  // Currently displayed screen. This class receives all the key events.
  std::unique_ptr<ScreenInterface> current_screen_;
  // Previous screen only used when changing the language so you know what
  // screen to return to after selection.
  std::unique_ptr<ScreenInterface> previous_screen_;
};

}  // namespace minios

#endif  // MINIOS_SCREEN_CONTROLLER_H_
