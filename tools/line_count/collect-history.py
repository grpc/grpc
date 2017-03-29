#!/usr/bin/env python
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

import subprocess
import datetime

# this script is only of historical interest: it's the script that was used to
# bootstrap the dataset

def daterange(start, end):
  for n in range(int((end - start).days)):
    yield start + datetime.timedelta(n)

start_date = datetime.date(2017, 3, 26)
end_date = datetime.date(2017, 3, 29)

for dt in daterange(start_date, end_date):
  dmy = dt.strftime('%Y-%m-%d')
  sha1 = subprocess.check_output(['git', 'rev-list', '-n', '1',
                                  '--before=%s' % dmy,
                                  'master']).strip()
  subprocess.check_call(['git', 'checkout', sha1])
  subprocess.check_call(['git', 'submodule', 'update'])
  subprocess.check_call(['git', 'clean', '-f', '-x', '-d'])
  subprocess.check_call(['cloc', '--vcs=git', '--by-file', '--yaml', '--out=../count/%s.yaml' % dmy, '.'])

