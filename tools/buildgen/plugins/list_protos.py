"""Buildgen .proto files list plugin.

This parses the list of targets from the json build file, and creates
a list called "protos" that contains all of the proto file names.

"""


import re


def mako_plugin(dictionary):
  """The exported plugin code for list_protos.

  Some projects generators may want to get the full list of unique .proto files
  that are being included in a project. This code extracts all files referenced
  in any library or target that ends in .proto, and builds and exports that as
  a list called "protos".

  """

  libs = dictionary.get('libs', [])
  targets = dictionary.get('targets', [])

  proto_re = re.compile('(.*)\\.proto')

  protos = set()
  for lib in libs:
    for src in lib.get('src', []):
      m = proto_re.match(src)
      if m:
        protos.add(m.group(1))
  for tgt in targets:
    for src in tgt.get('src', []):
      m = proto_re.match(src)
      if m:
        protos.add(m.group(1))

  protos = sorted(protos)

  dictionary['protos'] = protos
