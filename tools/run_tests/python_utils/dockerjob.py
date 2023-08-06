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
"""Helpers to run docker instances as jobs."""

from __future__ import print_function

import json
import os
import subprocess
import sys
import tempfile
import time
import uuid

sys.path.append(os.path.dirname(os.path.abspath(__file__)))
import jobset

_DEVNULL = open(os.devnull, "w")


def random_name(base_name):
    """Randomizes given base name."""
    return "%s_%s" % (base_name, uuid.uuid4())


def docker_kill(cid):
    """Kills a docker container. Returns True if successful."""
    return (
        subprocess.call(
            ["docker", "kill", str(cid)],
            stdin=subprocess.PIPE,
            stdout=_DEVNULL,
            stderr=subprocess.STDOUT,
        )
        == 0
    )


def docker_mapped_port(cid, port, timeout_seconds=15):
    """Get port mapped to internal given internal port for given container."""
    started = time.time()
    while time.time() - started < timeout_seconds:
        try:
            output = subprocess.check_output(
                "docker port %s %s" % (cid, port), stderr=_DEVNULL, shell=True
            ).decode()
            return int(output.split("\n", 2)[0].split(":", 2)[1])
        except subprocess.CalledProcessError as e:
            pass
    raise Exception(
        "Failed to get exposed port %s for container %s." % (port, cid)
    )


def docker_ip_address(cid, timeout_seconds=15):
    """Get port mapped to internal given internal port for given container."""
    started = time.time()
    while time.time() - started < timeout_seconds:
        cmd = "docker inspect %s" % cid
        try:
            output = subprocess.check_output(
                cmd, stderr=_DEVNULL, shell=True
            ).decode()
            json_info = json.loads(output)
            assert len(json_info) == 1
            out = json_info[0]["NetworkSettings"]["IPAddress"]
            if not out:
                continue
            return out
        except subprocess.CalledProcessError as e:
            pass
    raise Exception(
        "Non-retryable error: Failed to get ip address of container %s." % cid
    )


def wait_for_healthy(cid, shortname, timeout_seconds):
    """Wait timeout_seconds for the container to become healthy"""
    started = time.time()
    while time.time() - started < timeout_seconds:
        try:
            output = subprocess.check_output(
                [
                    "docker",
                    "inspect",
                    '--format="{{.State.Health.Status}}"',
                    cid,
                ],
                stderr=_DEVNULL,
            ).decode()
            if output.strip("\n") == "healthy":
                return
        except subprocess.CalledProcessError as e:
            pass
        time.sleep(1)
    raise Exception(
        "Timed out waiting for %s (%s) to pass health check" % (shortname, cid)
    )


def finish_jobs(jobs, suppress_failure=True):
    """Kills given docker containers and waits for corresponding jobs to finish"""
    for job in jobs:
        job.kill(suppress_failure=suppress_failure)

    while any(job.is_running() for job in jobs):
        time.sleep(1)


def image_exists(image):
    """Returns True if given docker image exists."""
    return (
        subprocess.call(
            ["docker", "inspect", image],
            stdin=subprocess.PIPE,
            stdout=_DEVNULL,
            stderr=subprocess.STDOUT,
        )
        == 0
    )


def remove_image(image, skip_nonexistent=False, max_retries=10):
    """Attempts to remove docker image with retries."""
    if skip_nonexistent and not image_exists(image):
        return True
    for attempt in range(0, max_retries):
        if (
            subprocess.call(
                ["docker", "rmi", "-f", image],
                stdin=subprocess.PIPE,
                stdout=_DEVNULL,
                stderr=subprocess.STDOUT,
            )
            == 0
        ):
            return True
        time.sleep(2)
    print("Failed to remove docker image %s" % image)
    return False


class DockerJob:
    """Encapsulates a job"""

    def __init__(self, spec):
        self._spec = spec
        self._job = jobset.Job(
            spec, newline_on_success=True, travis=True, add_env={}
        )
        self._container_name = spec.container_name

    def mapped_port(self, port):
        return docker_mapped_port(self._container_name, port)

    def ip_address(self):
        return docker_ip_address(self._container_name)

    def wait_for_healthy(self, timeout_seconds):
        wait_for_healthy(
            self._container_name, self._spec.shortname, timeout_seconds
        )

    def kill(self, suppress_failure=False):
        """Sends kill signal to the container."""
        if suppress_failure:
            self._job.suppress_failure_message()
        return docker_kill(self._container_name)

    def is_running(self):
        """Polls a job and returns True if given job is still running."""
        return self._job.state() == jobset._RUNNING
