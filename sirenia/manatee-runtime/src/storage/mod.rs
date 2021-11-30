// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Defines the messages and abstracts out communication for storage between
//! TEE apps, Trichechus, and Dugong.

use std::sync::Arc;

use libsirenia::communication::persistence::Status;
use libsirenia::communication::{StorageRpc, StorageRpcClient};
use libsirenia::storage::{to_read_data_error, to_write_data_error, Error, Result, Storage};
use libsirenia::transport::{create_transport_from_default_fds, Transport};
use once_cell::sync::OnceCell;
use sync::Mutex;

/// Holds the rpc client for the specific instance of the TEE App.
#[derive(Clone)]
pub struct TrichechusStorage {
    rpc: Arc<Mutex<StorageRpcClient>>,
}

impl TrichechusStorage {
    /*
     * Initialize the Transport between TEE App and Trichechus.
     *
     * Note: This can only be called once as it will create a file from the
     * connection file descriptor which is unsafe if done more than once. Every
     * call made after the first will simply return the storage object.
     */
    pub fn new() -> Self {
        static RPC: OnceCell<TrichechusStorage> = OnceCell::new();
        RPC.get_or_init(|| {
            TrichechusStorage {
                rpc: Arc::new(Mutex::new(StorageRpcClient::new(
                    // Safe because this is only called once by get_or_init().
                    unsafe { create_transport_from_default_fds() }.unwrap(),
                ))),
            }
        })
        .clone()
    }
}

impl Default for TrichechusStorage {
    fn default() -> Self {
        Self::new()
    }
}

impl From<Transport> for TrichechusStorage {
    fn from(transport: Transport) -> Self {
        TrichechusStorage {
            rpc: Arc::new(Mutex::new(StorageRpcClient::new(transport))),
        }
    }
}

impl Storage for TrichechusStorage {
    /// Read without deserializing.
    fn read_raw(&mut self, id: &str) -> Result<Vec<u8>> {
        // TODO: Log the rpc error.
        match self.rpc.lock().read_data(id.to_string()) {
            Ok((Status::Success, res)) => Ok(res),
            Ok((Status::IdNotFound, _)) => Err(Error::IdNotFound(id.to_string())),
            Ok((Status::Failure, _)) | Ok((Status::CryptoFailure, _)) => Err(Error::ReadData(None)),
            Err(err) => Err(to_read_data_error(err)),
        }
    }

    /// Write without serializing.
    fn write_raw(&mut self, id: &str, data: &[u8]) -> Result<()> {
        match self.rpc.lock().write_data(id.to_string(), data.to_vec()) {
            Ok(Status::Success) => Ok(()),
            Ok(_) => Err(Error::WriteData(None)),
            Err(err) => Err(to_write_data_error(err)),
        }
    }
}

#[cfg(test)]
pub mod tests {
    use super::*;

    use std::cell::RefCell;
    use std::collections::BTreeMap as Map;
    use std::rc::Rc;
    use std::result::Result as StdResult;
    use std::thread::spawn;
    use std::time::{SystemTime, UNIX_EPOCH};

    use assert_matches::assert_matches;
    use libsirenia::communication::StorageRpcServer;
    use libsirenia::rpc::RpcDispatcher;
    use libsirenia::storage::Error as StorageError;
    use libsirenia::transport::create_transport_from_pipes;

    const TEST_ID: &str = "id";

    #[derive(Clone)]
    struct StorageRpcServerImpl {
        map: Rc<RefCell<Map<String, Vec<u8>>>>,
    }

    impl StorageRpc for StorageRpcServerImpl {
        type Error = ();

        // TODO: Want to return nested Result - but Error needs to be serializable first
        fn read_data(&self, id: String) -> StdResult<(Status, Vec<u8>), Self::Error> {
            match self.map.borrow().get(&id) {
                Some(val) => Ok((Status::Success, val.to_vec())),
                None => Err(()),
            }
        }

        fn write_data(&self, id: String, data: Vec<u8>) -> StdResult<Status, Self::Error> {
            self.map.borrow_mut().insert(id, data);
            Ok(Status::Success)
        }
    }

    fn get_test_value() -> String {
        SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap()
            .as_secs()
            .to_string()
    }

    fn setup() -> (RpcDispatcher<Box<dyn StorageRpcServer>>, TrichechusStorage) {
        let (server_transport, client_transport) = create_transport_from_pipes().unwrap();

        let handler: Box<dyn StorageRpcServer> = Box::new(StorageRpcServerImpl {
            map: Rc::new(RefCell::new(Map::new())),
        });
        let dispatcher = RpcDispatcher::new(handler, server_transport).unwrap();

        (dispatcher, TrichechusStorage::from(client_transport))
    }

    #[test]
    fn write_and_read() {
        let (mut dispatcher, mut trichechus_storage) = setup();

        let client_thread = spawn(move || {
            let data = get_test_value();
            trichechus_storage.write_data(TEST_ID, &data).unwrap();

            let retrieved_data = trichechus_storage.read_data::<String>(TEST_ID).unwrap();
            assert_eq!(retrieved_data, data);
        });

        let sleep_for = None;
        assert_matches!(dispatcher.read_complete_message(sleep_for), Ok(None));
        assert_matches!(dispatcher.read_complete_message(sleep_for), Ok(None));

        client_thread.join().unwrap();
    }

    #[test]
    fn read_id_not_found() {
        let (mut dispatcher, mut trichechus_storage) = setup();

        let client_thread = spawn(move || {
            let error = trichechus_storage.read_data::<String>(TEST_ID).unwrap_err();
            println!("Client thread: {}", error);
            assert_matches!(error, StorageError::ReadData(_));
        });

        let sleep_for = None;
        assert_matches!(dispatcher.read_complete_message(sleep_for), Ok(Some(_)));

        // Explicitly call drop to close the pipe so the client thread gets the hang up since the return
        // value should be a RemoveFd mutator.
        drop(dispatcher);

        client_thread.join().unwrap();
    }
}
