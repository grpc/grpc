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

from __future__ import print_function

from six.moves import urllib
import os
import subprocess
import tempfile
import sys
import time
import jobset
import socket
import traceback


def start_port_server(port_server_port):
  # check if a compatible port server is running
  # if incompatible (version mismatch) ==> start a new one
  # if not running ==> start a new one
  # otherwise, leave it up
  try:
    version = int(urllib.request.urlopen(
        'http://localhost:%d/version_number' % port_server_port,
        timeout=10).read())
    print('detected port server running version %d' % version)
    running = True
  except Exception as e:
    print('failed to detect port server: %s' % sys.exc_info()[0])
    print(e.strerror)
    running = False
  if running:
    current_version = int(subprocess.check_output(
        [sys.executable, os.path.abspath('tools/run_tests/python_utils/port_server.py'),
         'dump_version']))
    print('my port server is version %d' % current_version)
    running = (version >= current_version)
    if not running:
      print('port_server version mismatch: killing the old one')
      urllib.request.urlopen('http://localhost:%d/quitquitquit' % port_server_port).read()
      time.sleep(1)
  if not running:
    fd, logfile = tempfile.mkstemp()
    os.close(fd)
    print('starting port_server, with log file %s' % logfile)
    args = [sys.executable, os.path.abspath('tools/run_tests/python_utils/port_server.py'),
            '-p', '%d' % port_server_port, '-l', logfile]
    env = dict(os.environ)
    env['BUILD_ID'] = 'pleaseDontKillMeJenkins'
    if jobset.platform_string() == 'windows':
      # Working directory of port server needs to be outside of Jenkins
      # workspace to prevent file lock issues.
      tempdir = tempfile.mkdtemp()
      port_server = subprocess.Popen(
          args,
          env=env,
          cwd=tempdir,
          creationflags = 0x00000008, # detached process
          close_fds=True)
    else:
      port_server = subprocess.Popen(
          args,
          env=env,
          preexec_fn=os.setsid,
          close_fds=True)
    time.sleep(1)
    # ensure port server is up
    waits = 0
    while True:
      if waits > 10:
        print('killing port server due to excessive start up waits')
        port_server.kill()
      if port_server.poll() is not None:
        print('port_server failed to start')
        # try one final time: maybe another build managed to start one
        time.sleep(1)
        try:
          urllib.request.urlopen('http://localhost:%d/get' % port_server_port,
                          timeout=1).read()
          print('last ditch attempt to contact port server succeeded')
          break
        except:
          traceback.print_exc()
          port_log = open(logfile, 'r').read()
          print(port_log)
          sys.exit(1)
      try:
        urllib.request.urlopen('http://localhost:%d/get' % port_server_port,
                        timeout=1).read()
        print('port server is up and ready')
        break
      except socket.timeout:
        print('waiting for port_server: timeout')
        traceback.print_exc();
        time.sleep(1)
        waits += 1
      except urllib.error.URLError:
        print('waiting for port_server: urlerror')
        traceback.print_exc();
        time.sleep(1)
        waits += 1
      except:
        traceback.print_exc()
        port_server.kill()
        raise

