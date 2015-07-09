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
      "conditions": [
        ['OS != "win"', {
          'variables': {
            'pkg_config_grpc': '<!(pkg-config --exists grpc >/dev/null 2>&1 && echo true || echo false)'
          },
          'conditions': [
            ['pkg_config_grpc == "true"', {
              'link_settings': {
                'libraries': [
                  '<!@(pkg-config --libs-only-l --static grpc)'
                ]
              },
              'cflags': [
                '<!@(pkg-config --cflags grpc)'
              ],
              'libraries': [
                '<!@(pkg-config --libs-only-L --static grpc)'
              ],
              'ldflags': [
                '<!@(pkg-config --libs-only-other --static grpc)'
              ]
              }, {
                'link_settings': {
                  'libraries': [
                    '-lpthread',
                    '-lgrpc',
                    '-lgpr'
                  ],
                },
                'conditions':[
                  ['OS != "mac"', {
                    'link_settings': {
                      'libraries': [
                        '-lrt'
                      ]
                    }
                  }]
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
