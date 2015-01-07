"""Allows dot-accessible dictionaries."""


class Bunch(dict):

  def __init__(self, d):
    dict.__init__(self, d)
    self.__dict__.update(d)


# Converts any kind of variable to a Bunch
def to_bunch(var):
  if isinstance(var, list):
    return [to_bunch(i) for i in var]
  if isinstance(var, dict):
    ret = {}
    for k, v in var.items():
      if isinstance(v, (list, dict)):
        v = to_bunch(v)
      ret[k] = v
    return Bunch(ret)
  else:
    return var


# Merges JSON 'add' into JSON 'dst'
def merge_json(dst, add):
  if isinstance(dst, dict) and isinstance(add, dict):
    for k, v in add.items():
      if k in dst:
        merge_json(dst[k], v)
      else:
        dst[k] = v
  elif isinstance(dst, list) and isinstance(add, list):
    dst.extend(add)
  else:
    raise Exception('Tried to merge incompatible objects %r, %r' % (dst, add))
