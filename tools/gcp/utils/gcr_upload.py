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

"""Upload docker images to Google Container Registry."""

from __future__ import print_function

import argparse
import atexit
import os
import shutil
import subprocess
import tempfile

argp = argparse.ArgumentParser(description='Run interop tests.')
argp.add_argument('--gcr_path',
                  default='gcr.io/grpc-testing',
                  help='Path of docker images in Google Container Registry')

argp.add_argument('--gcr_tag',
                  default='latest',
                  help='the tag string for the images to upload')

argp.add_argument('--with_files',
                  default=[],
                  nargs='+',
                  help='additional files to include in the docker image')

argp.add_argument('--with_file_dest',
                  default='/var/local/image_info',
                  help='Destination directory for with_files inside docker image')

argp.add_argument('--images',
                  default=[],
                  nargs='+',
                  help='local docker images in the form of repo:tag ' +
                  '(i.e. grpc_interop_java:26328ad8) to upload')

argp.add_argument('--keep',
                  action='store_true',
                  help='keep the created local images after uploading to GCR')


args = argp.parse_args()

def upload_to_gcr(image):
  """Tags and Pushes a docker image in Google Containger Registry.

  image: docker image name, i.e. grpc_interop_java:26328ad8

  A docker image image_foo:tag_old will be uploaded as
     <gcr_path>/image_foo:<gcr_tag>
  after inserting extra with_files under with_file_dest in the image.  The
  original image name will be stored as label original_name:"image_foo:tag_old".
  """
  tag_idx = image.find(':')
  if tag_idx == -1:
    print('Failed to parse docker image name %s' % image)
    return False
  new_tag = '%s/%s:%s' % (args.gcr_path, image[:tag_idx], args.gcr_tag)

  lines = ['FROM ' + image]
  lines.append('LABEL original_name="%s"' % image)

  temp_dir = tempfile.mkdtemp()
  atexit.register(lambda: subprocess.call(['rm', '-rf', temp_dir]))

  # Copy with_files inside the tmp directory, which will be the docker build
  # context.
  for f in args.with_files:
    shutil.copy(f, temp_dir)
    lines.append('COPY %s %s/' % (os.path.basename(f), args.with_file_dest))

  # Create a Dockerfile.
  with open(os.path.join(temp_dir, 'Dockerfile'), 'w') as f:
     f.write('\n'.join(lines))

  build_cmd = ['docker', 'build', '--rm', '--tag', new_tag, temp_dir]
  subprocess.check_output(build_cmd)

  if not args.keep:
    atexit.register(lambda: subprocess.call(['docker', 'rmi', new_tag]))

  # Upload to GCR.
  if args.gcr_path:
    subprocess.call(['gcloud', 'docker', '--', 'push', new_tag])

  return True


for image in args.images:
  upload_to_gcr(image)
