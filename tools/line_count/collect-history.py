#!/usr/bin/env python
# Copyright 2017 gRPC authors.
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

