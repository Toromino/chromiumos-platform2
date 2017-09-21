{
  'target_defaults': {
    'variables': {
      'deps': [
        'libminijail',
        'libchrome-<(libbase_ver)',
      ],
    },
    'defines': [
      'USE_device_mapper=<(USE_device_mapper)',
    ],
    'conditions': [
      ['USE_device_mapper == 1', {
        'variables': {
          'deps': [
            'devmapper',
          ],
        },
      }],
    ],
  },
  'targets': [
    {
      'target_name': 'libcontainer',
      'type': 'shared_library',
      'sources': [
        'cgroup.cc',
        'config.cc',
        'container.cc',
        'libcontainer.cc',
        'libcontainer_util.cc',
      ],
    },
  ],
  'conditions': [
    ['USE_test == 1', {
      'targets': [
        {
          'target_name': 'libcontainer_unittest',
          'type': 'executable',
          'sources': [
            'cgroup.cc',
            'config.cc',
            'container.cc',
            'libcontainer.cc',
            'libcontainer_unittest.cc',
            'libcontainer_util.cc',
          ],
        },
        {
          'target_name': 'container_cgroup_unittest',
          'type': 'executable',
          'sources': [
            'cgroup.cc',
            'cgroup_unittest.cc',
            'libcontainer_util.cc',
          ],
        },
      ]},
    ],
  ],
}
