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

from __future__ import print_function

import logging
import os
import socket
import subprocess
import sys
import tempfile
import time

import six.moves.urllib.request as request

sys.path.append(os.path.dirname(os.path.abspath(__file__)))
import jobset

# must be synchronized with test/core/util/port_server_client.h
_PORT_SERVER_PORT = 32766


def start_port_server():
    # check if a compatible port server is running
    # if incompatible (version mismatch) ==> start a new one
    # if not running ==> start a new one
    # otherwise, leave it up
    try:
        version = int(
            request.urlopen(
                "http://localhost:%d/version_number" % _PORT_SERVER_PORT
            ).read()
        )
        logging.info("detected port server running version %d", version)
        running = True
    except Exception as e:
        logging.exception("failed to detect port server")
        running = False
    if running:
        current_version = int(
            subprocess.check_output(
                [
                    sys.executable,  # use the same python binary as this process
                    os.path.abspath(
                        "tools/run_tests/python_utils/port_server.py"
                    ),
                    "dump_version",
                ]
            ).decode()
        )
        logging.info("my port server is version %d", current_version)
        running = version >= current_version
        if not running:
            logging.info("port_server version mismatch: killing the old one")
            request.urlopen(
                "http://localhost:%d/quitquitquit" % _PORT_SERVER_PORT
            ).read()
            time.sleep(1)
    if not running:
        fd, logfile = tempfile.mkstemp()
        os.close(fd)
        logging.info("starting port_server, with log file %s", logfile)
        args = [
            sys.executable,
            os.path.abspath("tools/run_tests/python_utils/port_server.py"),
            "-p",
            "%d" % _PORT_SERVER_PORT,
            "-l",
            logfile,
        ]
        env = dict(os.environ)
        env["BUILD_ID"] = "pleaseDontKillMeJenkins"
        if jobset.platform_string() == "windows":
            # Working directory of port server needs to be outside of Jenkins
            # workspace to prevent file lock issues.
            tempdir = tempfile.mkdtemp()
            if sys.version_info.major == 2:
                creationflags = 0x00000008  # detached process
            else:
                creationflags = (
                    0  # DETACHED_PROCESS doesn't seem to work with python3
                )
            port_server = subprocess.Popen(
                args,
                env=env,
                cwd=tempdir,
                creationflags=creationflags,
                close_fds=True,
            )
        else:
            port_server = subprocess.Popen(
                args, env=env, preexec_fn=os.setsid, close_fds=True
            )
        time.sleep(1)
        # ensure port server is up
        waits = 0
        while True:
            if waits > 10:
                logging.warning(
                    "killing port server due to excessive start up waits"
                )
                port_server.kill()
            if port_server.poll() is not None:
                logging.error("port_server failed to start")
                # try one final time: maybe another build managed to start one
                time.sleep(1)
                try:
                    request.urlopen(
                        "http://localhost:%d/get" % _PORT_SERVER_PORT
                    ).read()
                    logging.info(
                        "last ditch attempt to contact port server succeeded"
                    )
                    break
                except:
                    logging.exception(
                        "final attempt to contact port server failed"
                    )
                    port_log = open(logfile, "r").read()
                    print(port_log)
                    sys.exit(1)
            try:
                port_server_url = "http://localhost:%d/get" % _PORT_SERVER_PORT
                request.urlopen(port_server_url).read()
                logging.info("port server is up and ready")
                break
            except socket.timeout:
                logging.exception("while waiting for port_server")
                time.sleep(1)
                waits += 1
            except IOError:
                logging.exception("while waiting for port_server")
                time.sleep(1)
                waits += 1
            except:
                logging.exception(
                    (
                        'error while contacting port server at "%s".'
                        "Will try killing it."
                    ),
                    port_server_url,
                )
                port_server.kill()
                raise
