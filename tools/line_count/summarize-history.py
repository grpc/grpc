#!/usr/bin/env python

import subprocess
import datetime

def daterange(start, end):
  for n in range(int((end - start).days)):
    yield start + datetime.timedelta(n)

start_date = datetime.date(2017, 3, 26)
end_date = datetime.date(2017, 3, 29)

for dt in daterange(start_date, end_date):
  dmy = dt.strftime('%Y-%m-%d')
  print dmy
  subprocess.check_call(['tools/line_count/yaml2csv.py', '-i', '../count/%s.yaml' % dmy, '-d', dmy, '-o', '../count/%s.csv' % dmy])

