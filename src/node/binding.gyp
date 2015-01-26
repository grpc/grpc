{
  "targets" : [
    {
      'include_dirs': [
        "<!(node -e \"require('nan')\")"
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
        '-g',
        '-L/usr/local/google/home/mlumish/grpc_dev/lib'
      ],
      'link_settings': {
        'libraries': [
          '-lgrpc',
          '-lrt',
          '-lgpr',
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
      ]
    }
  ]
}
