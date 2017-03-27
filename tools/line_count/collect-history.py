#!/usr/bin/env python

import subprocess
import datetime

def daterange(start, end):
  for n in range(int((end - start).days)):
    yield start + datetime.timedelta(n)

start_date = datetime.date(2014, 11, 21)
end_date = datetime.date(2017, 3, 26)

for dt in daterange(start_date, end_date):
  dmy = dt.strftime('%Y-%m-%d')
  sha1 = subprocess.check_output(['git', 'rev-list', '-n', '1',
                                  '--before=%s' % dmy,
                                  'master']).strip()
  subprocess.check_call(['git', 'checkout', sha1])
  subprocess.check_call(['git', 'submodule', 'update'])
  subprocess.check_call(['git', 'clean', '-f', '-x', '-d'])
  subprocess.check_call(['cloc', '--vcs=git', '--by-file', '--yaml', '--out=../count/%s.yaml' % dmy, '.'])

