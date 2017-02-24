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

"""Helpers to run docker instances as jobs."""

from __future__ import print_function

import tempfile
import time
import uuid
import os
import subprocess

import jobset

_DEVNULL = open(os.devnull, 'w')


def random_name(base_name):
  """Randomizes given base name."""
  return '%s_%s' % (base_name, uuid.uuid4())


def docker_kill(cid):
  """Kills a docker container. Returns True if successful."""
  return subprocess.call(['docker','kill', str(cid)],
                         stdin=subprocess.PIPE,
                         stdout=_DEVNULL,
                         stderr=subprocess.STDOUT) == 0


def docker_mapped_port(cid, port, timeout_seconds=15):
  """Get port mapped to internal given internal port for given container."""
  started = time.time()
  while time.time() - started < timeout_seconds:
    try:
      output = subprocess.check_output('docker port %s %s' % (cid, port),
                                       stderr=_DEVNULL,
                                       shell=True)
      return int(output.split(':', 2)[1])
    except subprocess.CalledProcessError as e:
      pass
  raise Exception('Failed to get exposed port %s for container %s.' %
                  (port, cid))


def finish_jobs(jobs):
  """Kills given docker containers and waits for corresponding jobs to finish"""
  for job in jobs:
    job.kill(suppress_failure=True)

  while any(job.is_running() for job in jobs):
    time.sleep(1)


def image_exists(image):
  """Returns True if given docker image exists."""
  return subprocess.call(['docker','inspect', image],
                         stdin=subprocess.PIPE,
                         stdout=_DEVNULL,
                         stderr=subprocess.STDOUT) == 0


def remove_image(image, skip_nonexistent=False, max_retries=10):
  """Attempts to remove docker image with retries."""
  if skip_nonexistent and not image_exists(image):
    return True
  for attempt in range(0, max_retries):
    if subprocess.call(['docker','rmi', '-f', image],
                       stdin=subprocess.PIPE,
                       stdout=_DEVNULL,
                       stderr=subprocess.STDOUT) == 0:
      return True
    time.sleep(2)
  print('Failed to remove docker image %s' % image)
  return False

def push_to_gke_registry(image, gcr_path, gcr_tag='latest', with_files=[]):
  """Tags and Pushes a docker image in Google Containger Registry."""
  tag_idx = image.find(':')
  if tag_idx == -1:
    print('Failed to parse docker image name %s' % image)
    return False

  saved_dir = "/tmp/test_info"
  subprocess.check_call('rm -rf %s; mkdir -p %s' % (saved_dir, saved_dir))
  # Stores the original image:tag which might be referenced by tests
  # during replay.
  subprocess.check_call(
      'echo "%s" > %(dir)s/original_image' % (image, saved_dir))
  # Copy all other files inside the docker image.
  for f in with_files:
    shutil.copyfile(f, saved_dir)

  # Commit the change into the image.
  tag_name = '%s/%s:%s' % (gcr_path, image[:tag_idx], gcr_tag)
  cmd = (
      'docker run -v %(src)s:%(mnt)s:ro --name=%(vm)s %(img)s ' +
      'cp -r %(mnt)s %(dst)s && docker commit %(vm)s %(tag)s'
  ) % {
      'src': saved_dir,
      'mnt': os.path.join('/mnt/', os.path.basename(saved_dir)),
      'vm': uuid.uuid4(),
      'img':image,
      'tag': tag_name
  }

  # cmd = ['docker', 'tag', image, tag_name]
  # if subprocess.call(args=cmd) != 0:
  #   print('Error tagging docker image with command: %r' % cmd)
  #   return False
  if subprocess.call(args=cmd) != 0:
    print('Error in copying files into the image %s' % tag_name)
    return False

  cmd = ['gcloud', 'docker', 'push', tag_name]
  print('Pushing %s to the GKE registry..' % tag_name)
  if subprocess.call(args=cmd) != 0:
    print('Error in pushing the image %s to the GKE registry' % tag_name)
    return False
  return True

class DockerJob:
  """Encapsulates a job"""

  def __init__(self, spec):
    self._spec = spec
    self._job = jobset.Job(spec, newline_on_success=True, travis=True, add_env={})
    self._container_name = spec.container_name

  def mapped_port(self, port):
    return docker_mapped_port(self._container_name, port)

  def kill(self, suppress_failure=False):
    """Sends kill signal to the container."""
    if suppress_failure:
      self._job.suppress_failure_message()
    return docker_kill(self._container_name)

  def is_running(self):
    """Polls a job and returns True if given job is still running."""
    return self._job.state() == jobset._RUNNING
