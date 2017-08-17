{
  'target_defaults': {
    'variables': {
      'deps': [
        'fmap',
        'libbrillo-<(libbase_ver)',
        'libchrome-<(libbase_ver)',
        'libusb-1.0',
        'openssl',
      ],
    },
  },
  'targets': [
    {
      'target_name': 'libhammerd',
      'type': 'static_library',
      'sources': [
        # TODO(crbug.com/649672): Upgrade to OpenSSL 1.1 support curve25519.
        'curve25519.c',
        'fmap_utils.cc',
        'hammer_updater.cc',
        'process_lock.cc',
        'update_fw.cc',
        'usb_utils.cc',
      ],
    },
    {
      'target_name': 'hammerd',
      'type': 'executable',
      'dependencies': ['libhammerd'],
      'sources': [
        'main.cc',
      ],
    },
  ],
  'conditions': [
    ['USE_test == 1', {
      'targets': [
        {
          'target_name': 'unittest_runner',
          'type': 'executable',
          'dependencies': [
            'libhammerd',
            '../common-mk/testrunner.gyp:testrunner',
          ],
          'includes': ['../common-mk/common_test.gypi'],
          'sources': [
            'curve25519_unittest.cc',
            'hammer_updater_unittest.cc',
            'update_fw_unittest.cc',
          ],
        },
      ],
    }],
  ],
}
