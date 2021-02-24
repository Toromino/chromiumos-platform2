// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Ties together the various modules that make up the Sirenia library used by
//! both Trichechus and Dugong.

pub mod app_info;
pub mod build_info; // This is generated by build.rs.
pub mod cli;
pub mod communication;

include!("bindings/include_modules.rs");