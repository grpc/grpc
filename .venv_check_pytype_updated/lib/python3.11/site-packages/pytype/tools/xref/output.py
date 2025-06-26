"""Output utilities for xref."""

import dataclasses
import json


def unpack(obj):
  """Recursively expand namedtuples into dicts."""

  if hasattr(obj, "_asdict"):
    return {k: unpack(v) for k, v in obj._asdict().items()}
  elif isinstance(obj, dict):
    return {k: unpack(v) for k, v in obj.items()}
  elif isinstance(obj, list):
    return [unpack(v) for v in obj]
  elif isinstance(obj, tuple):
    return tuple(unpack(v) for v in obj)
  else:
    return obj


def json_kythe_graph(kythe_graph):
  """Generate kythe entries."""

  for x in kythe_graph.entries:
    yield json.dumps(dataclasses.asdict(x))


def output_kythe_graph(kythe_graph):
  for x in json_kythe_graph(kythe_graph):
    print(x)
