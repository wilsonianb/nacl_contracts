# -*- python -*-
{
  'includes': [
    '../../../build/common.gypi',
  ],
  'target_defaults': {
    'variables':{
      'target_base': 'none',
    },
    'target_conditions': [
      ['target_base=="ripple_ledger_service"', {
        'sources': [
          'ripple_ledger_service.h',
          'ripple_ledger_service.c',
        ],
        'xcode_settings': {
          'WARNING_CFLAGS': [
            '-Wno-missing-field-initializers'
          ]
        },
      },
    ]],
  },
  'conditions': [
    ['OS=="win" and target_arch=="ia32"', {
      'targets': [
        {
          'target_name': 'ripple_ledger_service64',
          'type': 'static_library',
          'variables': {
            'target_base': 'ripple_ledger_service',
            'win_target': 'x64',
          },
          'dependencies': [
            '<(DEPTH)/native_client/src/shared/platform/platform.gyp:platform64',
            '<(DEPTH)/native_client/src/shared/srpc/srpc.gyp:nonnacl_srpc64',
            '<(DEPTH)/native_client/src/trusted/threading/threading.gyp:thread_interface64',
            '<(DEPTH)/native_client/src/trusted/desc/desc.gyp:nrd_xfer64',
            '<(DEPTH)/native_client/src/trusted/nacl_base/nacl_base.gyp:nacl_base64',
          ],
        },
      ],
    }],
  ],
  'targets': [
    {
      'target_name': 'ripple_ledger_service',
      'type': 'static_library',
      'variables': {
        'target_base': 'ripple_ledger_service',
      },
      'dependencies': [
        '<(DEPTH)/native_client/src/shared/platform/platform.gyp:platform',
        '<(DEPTH)/native_client/src/shared/srpc/srpc.gyp:nonnacl_srpc',
        '<(DEPTH)/native_client/src/trusted/threading/threading.gyp:thread_interface',
        '<(DEPTH)/native_client/src/trusted/desc/desc.gyp:nrd_xfer',
        '<(DEPTH)/native_client/src/trusted/nacl_base/nacl_base.gyp:nacl_base',
      ],
    },
  ],
}
