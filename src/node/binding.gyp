{
  "variables" : {
    'no_install': "<!(echo $GRPC_NO_INSTALL)",
    'grpc_root': "<!(echo $GRPC_ROOT)",
    'grpc_lib_subdir': "<!(echo $GRPC_LIB_SUBDIR)"
    },
  "targets" : [
    {
      'include_dirs': [
        "<!(nodejs -e \"require('nan')\")"
      ],
      'cxxflags': [
        '-Wall',
        '-pthread',
        '-pedantic',
        '-g',
        '-zdefs'
        '-Werror',
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
        "byte_buffer.cc",
        "call.cc",
        "channel.cc",
        "completion_queue_async_worker.cc",
        "credentials.cc",
        "event.cc",
        "node_grpc.cc",
        "server.cc",
        "server_credentials.cc",
        "tag.cc",
        "timeval.cc"
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
