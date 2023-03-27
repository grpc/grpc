# Copyright 2022 gRPC authors.
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
import logging
import re
import subprocess
import time
from typing import Optional

logger = logging.getLogger(__name__)


class PortForwardingError(Exception):
    """Error forwarding port"""


class PortForwarder:
    PORT_FORWARD_LOCAL_ADDRESS: str = '127.0.0.1'

    def __init__(self,
                 context: str,
                 namespace: str,
                 destination: str,
                 remote_port: int,
                 local_port: Optional[int] = None,
                 local_address: Optional[str] = None):
        self.context = context
        self.namespace = namespace
        self.destination = destination
        self.remote_port = remote_port
        self.local_address = local_address or self.PORT_FORWARD_LOCAL_ADDRESS
        self.local_port: Optional[int] = local_port
        self.subprocess: Optional[subprocess.Popen] = None

    def connect(self) -> None:
        if self.local_port:
            port_mapping = f"{self.local_port}:{self.remote_port}"
        else:
            port_mapping = f":{self.remote_port}"
        cmd = [
            "kubectl", "--context", self.context, "--namespace", self.namespace,
            "port-forward", "--address", self.local_address, self.destination,
            port_mapping
        ]
        logger.debug('Executing port forwarding subprocess cmd: %s',
                     ' '.join(cmd))
        self.subprocess = subprocess.Popen(cmd,
                                           stdout=subprocess.PIPE,
                                           stderr=subprocess.STDOUT,
                                           universal_newlines=True)
        # Wait for stdout line indicating successful start.
        if self.local_port:
            local_port_expected = (
                f"Forwarding from {self.local_address}:{self.local_port}"
                f" -> {self.remote_port}")
        else:
            local_port_re = re.compile(
                f"Forwarding from {self.local_address}:([0-9]+) -> {self.remote_port}"
            )
        try:
            while True:
                time.sleep(0.05)
                output = self.subprocess.stdout.readline().strip()
                if not output:
                    return_code = self.subprocess.poll()
                    if return_code is not None:
                        errors = [
                            error
                            for error in self.subprocess.stdout.readlines()
                        ]
                        raise PortForwardingError(
                            'Error forwarding port, kubectl return '
                            f'code {return_code}, output {errors}')
                    # If there is no output, and the subprocess is not exiting,
                    # continue waiting for the log line.
                    continue

                # Validate output log
                if self.local_port:
                    if output != local_port_expected:
                        raise PortForwardingError(
                            f'Error forwarding port, unexpected output {output}'
                        )
                else:
                    groups = local_port_re.search(output)
                    if groups is None:
                        raise PortForwardingError(
                            f'Error forwarding port, unexpected output {output}'
                        )
                    # Update local port to the randomly picked one
                    self.local_port = int(groups[1])

                logger.info(output)
                break
        except Exception:
            self.close()
            raise

    def close(self) -> None:
        if self.subprocess is not None:
            logger.info('Shutting down port forwarding, pid %s',
                        self.subprocess.pid)
            self.subprocess.kill()
            stdout, _ = self.subprocess.communicate(timeout=5)
            logger.info('Port forwarding stopped')
            logger.debug('Port forwarding remaining stdout: %s', stdout)
            self.subprocess = None
