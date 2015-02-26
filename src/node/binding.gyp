{
  "variables" : {
    'no_install': "<!(echo $GRPC_NO_INSTALL)",
    'grpc_root': "<!(echo $GRPC_ROOT)",
    'grpc_lib_subdir': "<!(echo $GRPC_LIB_SUBDIR)"
    },
  "targets" : [
    {
      'include_dirs': [
        "<!(node -e \"require('nan')\")"
      ],
      'cflags': [
        '-std=c++11',
        '-Wall',
        '-pthread',
        '-pedantic',
        '-g',
        '-zdefs'
        '-Werror'
      ],
      'ldflags': [
        '-g'
      ],
      'link_settings': {
        'libraries': [
          '-lrt',
          '-lpthread'
        ],
      },
      "target_name": "grpc",
      "sources": [
        "ext/byte_buffer.cc",
        "ext/call.cc",
        "ext/channel.cc",
        "ext/completion_queue_async_worker.cc",
        "ext/credentials.cc",
        "ext/node_grpc.cc",
        "ext/server.cc",
        "ext/server_credentials.cc",
        "ext/timeval.cc"
      ],
      'conditions' : [
        ['no_install=="yes"', {
          'include_dirs': [
            "<(grpc_root)/include"
          ],
          'link_settings': {
            'libraries': [
              '<(grpc_root)/<(grpc_lib_subdir)/libgrpc.a',
              '<(grpc_root)/<(grpc_lib_subdir)/libgpr.a'
            ]
          }
        }],
        ['no_install!="yes"', {
            'link_settings': {
              'libraries': [
                '-lgrpc',
                '-lgpr'
              ]
            }
          }]
      ]
    }
  ]
}
