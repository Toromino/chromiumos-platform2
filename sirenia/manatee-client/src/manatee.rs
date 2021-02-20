// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The client binary for interacting with ManaTEE from command line on Chrome OS.

use std::env;
use std::fs::File;
use std::io::{self, copy, stdin, stdout};
use std::os::unix::io::FromRawFd;
use std::process::exit;
use std::thread::spawn;
use std::time::Duration;

use dbus::blocking::Connection;
use getopts::Options;
use manatee_client::client::OrgChromiumManaTEEInterface;
use thiserror::Error as ThisError;

const DEFAULT_DBUS_TIMEOUT: Duration = Duration::from_secs(25);

const DEVELOPER_SHELL_APP_ID: &str = "shell";
const SANDBOXED_SHELL_APP_ID: &str = "sandboxed-shell";

#[derive(ThisError, Debug)]
enum Error {
    #[error("failed parse command line options: {0:}")]
    Options(getopts::Fail),
    #[error("failed to get D-Bus connection: {0:}")]
    NewDBusConnection(dbus::Error),
    #[error("failed to call D-Bus method: {0:}")]
    DBusCall(dbus::Error),
    #[error("start app failed with code: {0:}")]
    StartApp(i32),
    #[error("copy failed: {0:}")]
    Copy(io::Error),
}

/// The result of an operation in this crate.
type Result<T> = std::result::Result<T, Error>;

fn start_manatee_app(app_id: &str) -> Result<()> {
    let connection = Connection::new_system().map_err(Error::NewDBusConnection)?;
    let conn_path = connection.with_proxy(
        "org.chromium.ManaTEE",
        "/org/chromium/ManaTEE1",
        DEFAULT_DBUS_TIMEOUT,
    );
    let (fd_in, fd_out) = match conn_path
        .start_teeapplication(app_id)
        .map_err(Error::DBusCall)?
    {
        (0, fd_in, fd_out) => (fd_in, fd_out),
        (code, _, _) => return Err(Error::StartApp(code)),
    };

    let output = spawn(move || -> Result<()> {
        // Safe because ownership of the file descriptor is transferred.
        let mut file_in = unsafe { File::from_raw_fd(fd_in.into_fd()) };
        copy(&mut file_in, &mut stdout()).map_err(Error::Copy)?;
        // Once stdout is closed, stdin is invalid and it is time to exit.
        exit(0);
    });

    // Safe because ownership of the file descriptor is transferred.
    let mut file_out = unsafe { File::from_raw_fd(fd_out.into_fd()) };
    copy(&mut stdin(), &mut file_out).map_err(Error::Copy)?;

    output
        .join()
        .map_err(|boxed_err| *boxed_err.downcast::<Error>().unwrap())??;

    Ok(())
}

fn main() -> Result<()> {
    const HELP_SHORT_NAME: &str = "h";
    const SANDBOX_SHORT_NAME: &str = "s";

    let mut options = Options::new();
    options.optflag(HELP_SHORT_NAME, "help", "Show this help string.");
    options.optflag(
        SANDBOX_SHORT_NAME,
        "enable-sandbox",
        "Run the shell in the default sandbox.",
    );

    let args: Vec<String> = env::args().collect();
    let matches = options.parse(&args[1..]).map_err(|err| {
        eprintln!("{}", options.usage(""));
        Error::Options(err)
    })?;
    if matches.opt_present(HELP_SHORT_NAME) {
        println!("{}", options.usage(""));
        return Ok(());
    }

    let app_id = if matches.opt_present(SANDBOX_SHORT_NAME) {
        SANDBOXED_SHELL_APP_ID
    } else {
        DEVELOPER_SHELL_APP_ID
    };
    start_manatee_app(app_id)
}