{
  "targets" : [
    {
      'include_dirs': [
        "<!(node -e \"require('nan')\")"
      ],
      'cflags': [
        '-std=c++0x',
        '-Wall',
        '-pthread',
        '-pedantic',
        '-g',
        '-zdefs',
        '-Werror'
      ],
      'ldflags': [
        '-g'
      ],
      'link_settings': {
        'libraries': [
          '-lpthread',
          '-lgrpc',
          '-lgpr'
        ]
      },
      "conditions": [
        ['OS == "mac"', {
          'xcode_settings': {
            'MACOSX_DEPLOYMENT_TARGET': '10.9',
            'OTHER_CFLAGS': [
              '-std=c++11',
              '-stdlib=libc++'
            ]
          }
        }],
        ['OS != "mac"', {
          'link_settings': {
            'libraries': [
              '-lrt'
            ]
          }
        }]
      ],
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
      ]
    }
  ]
}
