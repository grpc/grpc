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
import jobset
import logging
import os
import socket
import subprocess
import sys
import tempfile
import time


# must be synchronized with test/core/utils/port_server_client.h
_PORT_SERVER_PORT = 32766

def start_port_server():
    # check if a compatible port server is running
    # if incompatible (version mismatch) ==> start a new one
    # if not running ==> start a new one
    # otherwise, leave it up
    try:
        version = int(
            urllib.request.urlopen(
                'http://localhost:%d/version_number' % _PORT_SERVER_PORT,
                timeout=10).read())
        logging.info('detected port server running version %d', version)
        running = True
    except Exception as e:
        logging.exception('failed to detect port server')
        running = False
    if running:
        current_version = int(
            subprocess.check_output([
                sys.executable, os.path.abspath(
                    'tools/run_tests/python_utils/port_server.py'),
                'dump_version'
            ]))
        logging.info('my port server is version %d', current_version)
        running = (version >= current_version)
        if not running:
            logging.info('port_server version mismatch: killing the old one')
            urllib.request.urlopen('http://localhost:%d/quitquitquit' %
                                   _PORT_SERVER_PORT).read()
            time.sleep(1)
    if not running:
        fd, logfile = tempfile.mkstemp()
        os.close(fd)
        logging.info('starting port_server, with log file %s', logfile)
        args = [
            sys.executable,
            os.path.abspath('tools/run_tests/python_utils/port_server.py'),
            '-p', '%d' % _PORT_SERVER_PORT, '-l', logfile
        ]
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
                creationflags=0x00000008,  # detached process
                close_fds=True)
        else:
            port_server = subprocess.Popen(
                args, env=env, preexec_fn=os.setsid, close_fds=True)
        time.sleep(1)
        # ensure port server is up
        waits = 0
        while True:
            if waits > 10:
                logging.warning(
                    'killing port server due to excessive start up waits')
                port_server.kill()
            if port_server.poll() is not None:
                logging.error('port_server failed to start')
                # try one final time: maybe another build managed to start one
                time.sleep(1)
                try:
                    urllib.request.urlopen(
                        'http://localhost:%d/get' % _PORT_SERVER_PORT,
                        timeout=1).read()
                    logging.info(
                        'last ditch attempt to contact port server succeeded')
                    break
                except:
                    logging.exception(
                        'final attempt to contact port server failed')
                    port_log = open(logfile, 'r').read()
                    print(port_log)
                    sys.exit(1)
            try:
                port_server_url = 'http://localhost:%d/get' % _PORT_SERVER_PORT
                urllib.request.urlopen(port_server_url, timeout=1).read()
                logging.info('port server is up and ready')
                break
            except socket.timeout:
                logging.exception('while waiting for port_server')
                time.sleep(1)
                waits += 1
            except urllib.error.URLError:
                logging.exception('while waiting for port_server')
                time.sleep(1)
                waits += 1
            except:
                logging.exception('error while contacting port server at "%s".'
                                  'Will try killing it.', port_server_url)
                port_server.kill()
                raise
