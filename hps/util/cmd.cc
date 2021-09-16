// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/*
 * Command handler.
 */

#include <iomanip>
#include <iostream>
#include <memory>
#include <utility>

#include <string.h>

#include <base/command_line.h>

#include "hps/dev.h"
#include "hps/hps.h"
#include "hps/hps_reg.h"
#include "hps/util/command.h"

namespace {

int SendCmd(std::unique_ptr<hps::HPS> hps,
            const base::CommandLine::StringVector& args) {
  int cmd;

  if (args.size() <= 1) {
    std::cerr << "Missing command ('reset', 'launch' or 'appl' expected)"
              << std::endl;
    return 1;
  }
  if (args[1] == "reset") {
    cmd = hps::R3::kReset;
  } else if (args[1] == "launch") {
    cmd = hps::R3::kLaunch;
  } else if (args[1] == "appl") {
    cmd = hps::R3::kEnable;
  } else {
    std::cerr << args[0] << ": Unknown command (" << args[1] << ")"
              << std::endl;
    return 1;
  }

  for (auto i = 0; i < 5; i++) {
    std::cout << "reg " << i << " = " << std::hex << std::setfill('0')
              << std::setw(4) << hps->Device()->ReadReg(hps::I2cReg(i))
              << std::endl;
  }
  std::cout << "Sending cmd value " << std::hex << std::setfill('0')
            << std::setw(4) << cmd << " to register 3" << std::endl;
  if (hps->Device()->WriteReg(hps::HpsReg::kSysCmd, cmd)) {
    std::cout << "Success!" << std::endl;
  } else {
    std::cout << "Write failed!" << std::endl;
  }
  return 0;
}

Command cmd("cmd",
            "cmd [ reset | launch | appl ] - Send command to hps.",
            SendCmd);

}  // namespace