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
import fileinput
import logging
import os
import pathlib
import threading
from typing import Any, Callable, Optional, TextIO

from kubernetes import client
from kubernetes.watch import watch

from framework.infrastructure import k8s

logger = logging.getLogger(__name__)


class PodLogCollector(threading.Thread):
    """A thread that streams logs from the remote pod to a local file."""
    pod_name: str
    namespace_name: str
    stop_event: threading.Event
    log_path: pathlib.Path
    log_to_stdout: bool
    error_backoff_sec: int
    _out_stream: Optional[TextIO]
    _watcher: Optional[watch.Watch]
    _read_pod_log_fn: Callable[..., Any]

    def __init__(self,
                 *,
                 pod_name: str,
                 k8s_namespace: k8s.KubernetesNamespace,
                 stop_event: threading.Event,
                 log_path: pathlib.Path,
                 log_to_stdout: bool = False,
                 error_backoff_sec: int = 1):
        self.pod_name = pod_name
        self.namespace_name = k8s_namespace.name
        self.stop_event = stop_event
        self.log_path = log_path
        self.log_to_stdout = log_to_stdout
        self.error_backoff_sec = error_backoff_sec
        self._read_pod_log_fn = k8s_namespace.api.core.read_namespaced_pod_log
        self._out_stream = None
        self._watcher = None
        super().__init__(name=f'pod-log-{pod_name}', daemon=True)

    def run(self):
        logger.info('Starting log collection on a thread %s', self.name)
        try:
            self._out_stream = open(self.log_path, "wx", errors='ignore')
            while not self.stop_event.is_set():
                self._stream_log()
        finally:
            self.stop()

    def stop(self):
        if self._watcher is not None:
            self._watcher.stop()
            self._watcher = None
        if self._out_stream is not None:
            self._write(f'Finished log collection for pod {self.pod_name}',
                        force_flush=True)
            self._out_stream.close()
            self._out_stream = None

    def _stream_log(self):
        try:
            self._restart_stream()
        except client.ApiException as e:
            self._write(f"Exception fetching logs: {e}")
            self._write(
                f'Restarting log fetching in {self.error_backoff_sec} sec. '
                f'Will attempt to read from the beginning, but log '
                f'truncation may occur.',
                force_flush=True)
            # Instead of time.sleep(), we're waiting on the stop event
            # in case it gets set earlier.
            self.stop_event.wait(timeout=self.error_backoff_sec)

    def _restart_stream(self):
        self._watcher = watch.Watch()
        for msg in self._watcher.stream(self._read_pod_log_fn,
                                        name=self.pod_name,
                                        namespace=self.namespace_name,
                                        timestamps=True,
                                        follow=True):
            self._write(msg)
            # Every message check if a stop is requested.
            if self.stop_event.is_set():
                self.stop()
                return

    def _write(self, msg: str, force_flush: bool = False):
        self._out_stream.write(msg)
        self._out_stream.write("\n")
        if force_flush:
            self._out_stream.flush()
            os.fsync(self._out_stream.fileno())
        if self.log_to_stdout:
            logger.info(msg)
