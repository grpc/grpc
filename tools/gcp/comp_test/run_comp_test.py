#!/usr/bin/env python2.7
# Copyright 2017, Google Inc.
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

"""Run tests using images stored in Google Container Registry."""

from __future__ import print_function

import argparse
import atexit
import json
import os
import shutil
import subprocess

argp = argparse.ArgumentParser(description='Run interop tests.')
argp.add_argument('--gcr_path',
                  default='gcr.io/grpc-testing',
                  help='Path of docker images in Google Container Registry')

argp.add_argument('--gcr_tag',
                  default='comp_latest',
                  help='the tag string of the images to run')

argp.add_argument('--run_files',
                  default=['interop_client_cmds.sh'],
                  nargs='+',
                  help='scripts (in with_file_path) from docker images to invoke')

argp.add_argument('--with_file_path',
                  default='/var/local/image_info',
                  help='Directory inside docker image for image_info')

argp.add_argument('--keep',
                  action='store_true',
                  help='keep the docker images after finishing the tests.')


args = argp.parse_args()

# Images with this prefix are for running comp test.
_IMAGE_NAME_PREFIX = 'grpc_interop_'

def query_gcr():
  """Query GCR for comp_test images.

  Images need to be under args.gcr_path, starts with _IMAGE_NAME_PREFIX, and
  contains args.gcr_tag.

  Returns all the matching images str in the form of
     <gcr_path>/_IMAGE_NAME_PREFIX_lang:<gcr_tag>
  as a list.
  """
  # Query GCR.
  output = subprocess.check_output([
      'gcloud', 'beta', 'container', 'images', 'list',
      '--repository=%s' % args.gcr_path,
      '--filter', 'name:%s' % _IMAGE_NAME_PREFIX, '--format=json'])

  # Find images with desired tag.
  images = []
  for entry in json.loads(output):
    output = subprocess.check_output([
        'gcloud', 'beta', 'container', 'images', 'list-tags', entry['name'],
        '--filter', 'tags=%s' % args.gcr_tag, '--format=json'])
    # As long as the result is loadable in json and not empty, we have a match.
    if json.loads(output):
      images.append(entry['name'])

  print('Images found: %s' % images)
  return images

def run_tests_in_image(image):
  """Download a docker image and run tests stored inside."""
  image_full = '%s:%s' % (image, args.gcr_tag)
  print('Pulling image: %s' % image_full)

  output = subprocess.check_output(['gcloud', 'docker', '--', 'pull', image_full])
  # Register clean up.
  atexit.register(lambda: subprocess.call(['docker', 'rmi', image_full]))

  output = subprocess.check_output(['docker', 'inspect', image_full])
  info = json.loads(output)
  try:
    original_name = info[0]['Config']['Labels']['original_name']
  except:
    print('No info in docker image: %s' % image_full)
    sys.exit(1)

  with_basename = os.path.basename(args.with_file_path)
  print('Extracting %s' % args.run_files)
  # Extract the shell commands for running the test under /tmp/image_info/.
  cmd = "docker run --rm %s tar -c -C %s %s |tar -x -C /tmp" % (
      image_full, os.path.dirname(args.with_file_path), with_basename)
  subprocess.check_call(cmd, shell=True)

  print('Running test')
  for run_file in args.run_files:
    with open(os.path.join('/tmp', with_basename, run_file)) as f:
      for test_line in f:
        if test_line.startswith('#'):
          continue
        # We need to replace the original image name (containing uuid) in the
        # docker run command with the new gcr based name (image_full).
        if not original_name in test_line:
          # this line doesn't use current docker image.
          continue

        test = test_line.replace(original_name, image_full)
        print(test)
        p = subprocess.Popen(test, shell=True, stdout=subprocess.PIPE)
        print(p.communicate()[0])

        #TODO(yongni): figure out how to present the result.

  # Clean up.
  shutil.rmtree(os.path.join('/tmp', with_basename))


for image in query_gcr():
  run_tests_in_image(image)
