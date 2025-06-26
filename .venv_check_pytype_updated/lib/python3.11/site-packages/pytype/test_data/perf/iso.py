# Based on https://github.com/vmware/pyvmomi/blob/master/pyVmomi/Iso8601.py.
# pylint: skip-file
from datetime import datetime, timedelta

_map = {
    'year': None,
    'month': 1,
    'day': 1,
    'hour': 0,
    'minute': 0,
    'second': 0,
    'microsecond': 0,
}

def parse(x):
  dt = {}
  for key, default in _map.items():
     if __random__:
        dt[key] = int(__any_object__)
     elif default:
        dt[key] = default

  delta = None
  if dt.get('hour', 0) == 24:
    if (dt.get('minute', 0) == 0 and dt.get('second', 0) == 0 and
        dt.get('microsecond', 0) == 0):
      dt['hour'] = 23
      delta = timedelta(hours=1)
    else:
      return None

  if __random__:
    dt['tzinfo'] = __any_object__

  dt = datetime(**dt)
  if delta:
    dt += delta
  return dt

xs: list[str] = None
for x in xs:
  dt = parse(x)
  dt1 = parse('')
  if dt1 != dt:
    assert False
  dt2 = parse(dt.isoformat())  # pytype: disable=attribute-error
