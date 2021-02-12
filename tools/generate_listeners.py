# Map from listeners proto, with holes where filter config fragments should go, and
# a list of filter config fragment protos, to a final listeners.pb with the
# config fragments converted to the opaque Struct representation.

import sys

# Some evil hack to deal with the fact that Bazel puts both google/api and
# google/protobuf roots in the sys.path, and Python gets confused, e.g. it
# thinks that there is no api package if it encounters the google/protobuf root
# in sys.path first.
from pkgutil import extend_path
import google
google.__path__ = extend_path(google.__path__, google.__name__)

from google.protobuf import json_format
from google.protobuf import struct_pb2
from google.protobuf import text_format

from envoy.api.v2 import lds_pb2
from envoy.config.filter.network.http_connection_manager.v2 import http_connection_manager_pb2


# Convert an arbitrary proto object to its Struct proto representation.
def ProtoToStruct(proto):
  json_rep = json_format.MessageToJson(proto)
  parsed_msg = struct_pb2.Struct()
  json_format.Parse(json_rep, parsed_msg)
  return parsed_msg


# Parse a proto from the filesystem.
def ParseProto(path, filter_name):
  # We only know about some filter config protos ahead of time.
  KNOWN_FILTERS = {
      'http_connection_manager': lambda: http_connection_manager_pb2.HttpConnectionManager()
  }
  filter_config = KNOWN_FILTERS[filter_name]()
  with open(path, 'r') as f:
    text_format.Merge(f.read(), filter_config)
  return filter_config


def GenerateListeners(listeners_pb_path, output_pb_path, output_json_path, fragments):
  listener = lds_pb2.Listener()
  with open(listeners_pb_path, 'r') as f:
    text_format.Merge(f.read(), listener)

  for filter_chain in listener.filter_chains:
    for f in filter_chain.filters:
      f.config.CopyFrom(ProtoToStruct(ParseProto(next(fragments), f.name)))

  with open(output_pb_path, 'w') as f:
    f.write(str(listener))

  with open(output_json_path, 'w') as f:
    f.write(json_format.MessageToJson(listener))


if __name__ == '__main__':
  if len(sys.argv) < 4:
    print('Usage: %s <path to listeners.pb> <output listeners.pb> <output '
          'listeners.json> <filter config fragment paths>') % sys.argv[0]
    sys.exit(1)

  GenerateListeners(sys.argv[1], sys.argv[2], sys.argv[3], iter(sys.argv[4:]))
