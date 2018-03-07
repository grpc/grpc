#!/usr/bin/env python2.7
# Copyright 2015 gRPC authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Opens a TCP connection to a specified server and then exits."""

import argparse
import signal
import socket

def main():
  argp = argparse.ArgumentParser(description='Open a TCP handshake to a server')
  argp.add_argument('-s', '--server_host', default=None, type=str,
                    help='Server host name or IP.')
  argp.add_argument('-p', '--server_port', default=0, type=int,
                    help='Port that the server is listening on.')
  argp.add_argument('-t', '--timeout', default=1, type=int,
                    help='Force process exit after this number of seconds.')
  args = argp.parse_args()
  signal.alarm(args.timeout)
  socket.create_connection([args.server_host, args.server_port])

if __name__ == '__main__':
  main()
