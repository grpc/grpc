# Copyright 2015, Google Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
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
        "src/node/ext/call_credentials.cc",
        "src/node/ext/channel.cc",
        "src/node/ext/channel_credentials.cc",
        "src/node/ext/completion_queue_async_worker.cc",
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
