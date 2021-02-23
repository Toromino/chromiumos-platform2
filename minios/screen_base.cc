// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "minios/screen_base.h"

#include <algorithm>
#include <utility>

#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <base/threading/platform_thread.h>
#include <base/time/time.h>

namespace screens {

// Colors
const char kMenuBlack[] = "0x202124";
const char kMenuBlue[] = "0x8AB4F8";
const char kMenuGrey[] = "0x3F4042";
const char kMenuButtonFrameGrey[] = "0x9AA0A6";

// Dimension Constants
const int kMonospaceGlyphWidth = 10;
const int kDefaultMessageWidth = 720;

namespace {
constexpr char kConsole0[] = "dev/pts/0";
// Frecon constants
// TODO(vyshu): Get this from frecon.
constexpr int kFreconScalingFactor = 1;
constexpr int kCanvasSize = 1080;

// Dimensions and spacing.
constexpr int kDefaultButtonWidth = 80;
constexpr int kButtonHeight = 32;
constexpr int kMonospaceGlyphHeight = 20;
constexpr int kNewLineChar = 10;

constexpr char kButtonWidthToken[] = "DEBUG_OPTIONS_BTN_WIDTH";
}  // namespace

bool ScreenBase::ShowText(const std::string& text,
                          int glyph_offset_h,
                          int glyph_offset_v,
                          const std::string& color) {
  base::FilePath glyph_dir = screens_path_.Append("glyphs").Append(color);
  const int kTextStart = glyph_offset_h;

  for (const auto& chr : text) {
    int char_num = static_cast<int>(chr);
    base::FilePath chr_file_path =
        glyph_dir.Append(base::NumberToString(char_num) + ".png");
    if (char_num == kNewLineChar) {
      glyph_offset_v += kMonospaceGlyphHeight;
      glyph_offset_h = kTextStart;
    } else {
      int offset_rtl = right_to_left_ ? -glyph_offset_h : glyph_offset_h;
      if (!ShowImage(chr_file_path, offset_rtl, glyph_offset_v)) {
        LOG(ERROR) << "Failed to show image " << chr_file_path << " for text "
                   << text;
        return false;
      }
      glyph_offset_h += kMonospaceGlyphWidth;
    }
  }
  return true;
}

bool ScreenBase::ShowImage(const base::FilePath& image_name,
                           int offset_x,
                           int offset_y) {
  if (right_to_left_)
    offset_x = -offset_x;
  std::string command = base::StringPrintf(
      "\033]image:file=%s;offset=%d,%d;scale=%d\a", image_name.value().c_str(),
      offset_x, offset_y, kFreconScalingFactor);
  if (!base::AppendToFile(base::FilePath(root_).Append(kConsole0),
                          command.c_str(), command.size())) {
    LOG(ERROR) << "Could not write " << image_name << "  to console.";
    return false;
  }

  return true;
}

bool ScreenBase::ShowBox(int offset_x,
                         int offset_y,
                         int size_x,
                         int size_y,
                         const std::string& color) {
  size_x = std::max(size_x, 1);
  size_y = std::max(size_y, 1);
  if (right_to_left_)
    offset_x = -offset_x;

  std::string command = base::StringPrintf(
      "\033]box:color=%s;size=%d,%d;offset=%d,%d;scale=%d\a", color.c_str(),
      size_x, size_y, offset_x, offset_y, kFreconScalingFactor);

  if (!base::AppendToFile(base::FilePath(root_).Append(kConsole0),
                          command.c_str(), command.size())) {
    LOG(ERROR) << "Could not write show box command to console.";
    return false;
  }

  return true;
}

bool ScreenBase::ShowMessage(const std::string& message_token,
                             int offset_x,
                             int offset_y) {
  // Determine the filename of the message resource. Fall back to en-US if
  // the localized version of the message is not available.
  base::FilePath message_file_path =
      screens_path_.Append(locale_).Append(message_token + ".png");
  if (!base::PathExists(message_file_path)) {
    if (locale_ == "en-US") {
      LOG(ERROR) << "Message " << message_token
                 << " not found in en-US. No fallback available.";
      return false;
    }
    LOG(WARNING) << "Could not find " << message_token << " in " << locale_
                 << " trying default locale en-US.";
    message_file_path =
        screens_path_.Append("en-US").Append(message_token + ".png");
    if (!base::PathExists(message_file_path)) {
      LOG(ERROR) << "Message " << message_token << " not found in path "
                 << message_file_path;
      return false;
    }
  }
  return ShowImage(message_file_path, offset_x, offset_y);
}

void ScreenBase::ShowInstructions(const std::string& message_token) {
  constexpr int kXOffset = (-kCanvasSize / 2) + (kDefaultMessageWidth / 2);
  constexpr int kYOffset = (-kCanvasSize / 2) + 283;
  if (!ShowMessage(message_token, kXOffset, kYOffset))
    LOG(WARNING) << "Unable to show " << message_token;
}

void ScreenBase::ShowInstructionsWithTitle(const std::string& message_token) {
  constexpr int kXOffset = (-kCanvasSize / 2) + (kDefaultMessageWidth / 2);

  int title_height;
  if (!GetDimension("TITLE_" + message_token + "_HEIGHT", &title_height)) {
    title_height = 40;
    LOG(WARNING) << "Unable to get title constant for  " << message_token
                 << ". Defaulting to " << title_height;
  }
  int desc_height;
  if (!GetDimension("DESC_" + message_token + "_HEIGHT", &desc_height)) {
    desc_height = 40;
    LOG(WARNING) << "Unable to get description constant for  " << message_token
                 << ". Defaulting to " << desc_height;
  }

  const int kTitleY = (-kCanvasSize / 2) + 220 + (title_height / 2);
  const int kDescY = kTitleY + (title_height / 2) + 16 + (desc_height / 2);
  if (!ShowMessage("title_" + message_token, kXOffset, kTitleY))
    LOG(WARNING) << "Unable to show title " << message_token;
  if (!ShowMessage("desc_" + message_token, kXOffset, kDescY))
    LOG(WARNING) << "Unable to show description " << message_token;
}

void ScreenBase::ShowProgressBar(double seconds) {
  constexpr int kProgressIncrement = 10;
  constexpr int kProgressHeight = 4;

  ShowBox(0, 0, kProgressIncrement * 100, kProgressHeight, kMenuGrey);

  constexpr int kLeftIncrement = -500;
  int leftmost = kLeftIncrement;

  // Can be increased for a smoother progress bar.
  constexpr int kUpdatesPerSecond = 10;
  const double kPercentUpdate = 100 / (seconds * kUpdatesPerSecond);
  double current_percent = 0;

  while (current_percent < 100) {
    current_percent += kPercentUpdate;
    int rightmost = kLeftIncrement + kProgressIncrement * current_percent;
    while (leftmost < rightmost) {
      ShowBox(leftmost + kProgressIncrement / 2, 0, kProgressIncrement + 2,
              kProgressHeight, kMenuBlue);
      leftmost += kProgressIncrement;
    }
    base::PlatformThread::Sleep(
        base::TimeDelta::FromMilliseconds(1000 / kUpdatesPerSecond));
  }
}

void ScreenBase::ClearMainArea() {
  constexpr int kFooterHeight = 142;
  if (!ShowBox(0, -kFooterHeight / 2, kCanvasSize + 100,
               (kCanvasSize - kFooterHeight), kMenuBlack))
    LOG(WARNING) << "Could not clear main area.";
}

void ScreenBase::ClearScreen() {
  if (!ShowBox(0, 0, kCanvasSize + 100, kCanvasSize, kMenuBlack))
    LOG(WARNING) << "Could not clear screen.";
}

void ScreenBase::ShowButton(const std::string& message_token,
                            int offset_y,
                            bool is_selected,
                            int inner_width,
                            bool is_text) {
  const int kBtnPadding = 32;  // Left and right padding.
  int left_padding_x = (-kCanvasSize / 2) + (kBtnPadding / 2);
  const int kOffsetX = left_padding_x + (kBtnPadding / 2) + (inner_width / 2);
  int right_padding_x = kOffsetX + (kBtnPadding / 2) + (inner_width / 2);
  // Clear previous state.
  if (!ShowBox(kOffsetX, offset_y, (kBtnPadding * 2 + inner_width),
               kButtonHeight, kMenuBlack)) {
    LOG(WARNING) << "Could not clear button area.";
  }

  if (right_to_left_) {
    std::swap(left_padding_x, right_padding_x);
  }

  if (is_selected) {
    ShowImage(screens_path_.Append("btn_bg_left_focused.png"), left_padding_x,
              offset_y);
    ShowImage(screens_path_.Append("btn_bg_right_focused.png"), right_padding_x,
              offset_y);

    ShowBox(kOffsetX, offset_y, inner_width, kButtonHeight, kMenuBlue);
    if (is_text) {
      ShowText(message_token, left_padding_x, offset_y, "black");
    } else {
      ShowMessage(message_token + "_focused", kOffsetX, offset_y);
    }
  } else {
    ShowImage(screens_path_.Append("btn_bg_left.png"), left_padding_x,
              offset_y);
    ShowImage(screens_path_.Append("btn_bg_right.png"), right_padding_x,
              offset_y);
    ShowBox(kOffsetX, offset_y - (kButtonHeight / 2) + 1, inner_width, 1,
            kMenuButtonFrameGrey);
    ShowBox(kOffsetX, offset_y + (kButtonHeight / 2), inner_width, 1,
            kMenuButtonFrameGrey);
    if (is_text) {
      ShowText(message_token, left_padding_x, offset_y, "white");
    } else {
      ShowMessage(message_token, kOffsetX, offset_y);
    }
  }
}

void ScreenBase::ShowStepper(const std::vector<std::string>& steps) {
  // The icon real size is 24x24, but it occupies a 36x36 block. Use 36 here for
  // simplicity.
  constexpr int kIconSize = 36;
  constexpr int kSeparatorLength = 46;
  constexpr int kPadding = 6;

  int stepper_x = (-kCanvasSize / 2) + (kIconSize / 2);
  constexpr int kStepperXStep = kIconSize + kSeparatorLength + (kPadding * 2);
  constexpr int kStepperY = 144 - (kCanvasSize / 2);
  int separator_x =
      (-kCanvasSize / 2) + kIconSize + kPadding + (kSeparatorLength / 2);

  for (const auto& step : steps) {
    base::FilePath stepper_image = screens_path_.Append("ic_" + step + ".png");
    if (!base::PathExists(stepper_image)) {
      // TODO(vyshu): Create a new generic icon to be used instead of done.
      LOG(WARNING) << "Stepper icon " << stepper_image
                   << " not found. Defaulting to the done icon.";
      stepper_image = screens_path_.Append("ic_done.png");
      if (!base::PathExists(stepper_image)) {
        LOG(ERROR) << "Could not find stepper icon done. Cannot show stepper.";
        return;
      }
    }
    ShowImage(stepper_image, stepper_x, kStepperY);
    stepper_x += kStepperXStep;
  }

  for (int i = 0; i < steps.size() - 1; ++i) {
    ShowBox(separator_x, kStepperY, kSeparatorLength, 1, kMenuGrey);
    separator_x += kStepperXStep;
  }
}

void ScreenBase::ReadDimensionConstants() {
  image_dimensions_.clear();
  base::FilePath path = screens_path_.Append(locale_).Append("constants.sh");
  std::string dimension_consts;
  if (!ReadFileToString(path, &dimension_consts)) {
    LOG(ERROR) << "Could not read constants.sh file for language " << locale_;
    return;
  }
  if (!base::SplitStringIntoKeyValuePairs(dimension_consts, '=', '\n',
                                          &image_dimensions_)) {
    LOG(WARNING) << "Unable to parse all dimension information for " << locale_;
    return;
  }

  // Save default button width for this locale.
  if (!GetDimension(kButtonWidthToken, &default_button_width_)) {
    default_button_width_ = kDefaultButtonWidth;
    LOG(WARNING) << "Unable to get dimension for " << kButtonWidthToken
                 << ". Defaulting to width " << kDefaultButtonWidth;
  }
}

bool ScreenBase::GetDimension(const std::string& token, int* token_dimension) {
  if (image_dimensions_.empty()) {
    LOG(ERROR) << "No dimensions available.";
    return false;
  }

  // Find the dimension for the token.
  for (const auto& dimension : image_dimensions_) {
    if (dimension.first == token) {
      if (!base::StringToInt(dimension.second, token_dimension)) {
        LOG(ERROR) << "Could not convert " << dimension.second
                   << " to a number.";
        return false;
      }
      return true;
    }
  }
  return false;
}

}  // namespace screens