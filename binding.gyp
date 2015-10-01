{
  "variables" : {
    'config': '<!(echo $CONFIG)'
  },
  "targets" : [
    {
      'include_dirs': [
        "<!(node -e \"require('nan')\")"
      ],
      'cflags': [
        '-std=c++0x',
        '-Wall',
        '-pthread',
        '-g',
        '-zdefs',
        '-Werror',
        '-Wno-error=deprecated-declarations'
      ],
      'ldflags': [
        '-g'
      ],
      "conditions": [
        ['OS != "win"', {
          'conditions': [
            ['config=="gcov"', {
              'cflags': [
                '-ftest-coverage',
                '-fprofile-arcs',
                '-O0'
              ],
              'ldflags': [
                '-ftest-coverage',
                '-fprofile-arcs'
              ]
            }
           ]
          ]
        }],
        ['OS == "mac"', {
          'xcode_settings': {
            'MACOSX_DEPLOYMENT_TARGET': '10.9',
            'OTHER_CFLAGS': [
              '-std=c++11',
              '-stdlib=libc++'
            ]
          }
        }]
      ],
      "target_name": "grpc_node",
      "sources": [
        "src/node/ext/byte_buffer.cc",
        "src/node/ext/call.cc",
        "src/node/ext/channel.cc",
        "src/node/ext/completion_queue_async_worker.cc",
        "src/node/ext/credentials.cc",
        "src/node/ext/node_grpc.cc",
        "src/node/ext/server.cc",
        "src/node/ext/server_credentials.cc",
        "src/node/ext/timeval.cc"
      ],
      "dependencies": [
        "grpc.gyp:grpc"
      ]
    }
  ]
}
