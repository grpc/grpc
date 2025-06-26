"""Serialize blocks into json."""

import dataclasses
import json
from typing import Any, NewType

from pytype.blocks import blocks


BlockId = NewType("BlockId", str)


@dataclasses.dataclass
class SerializedBlock:
  """Serialized block."""

  id: BlockId
  code: str
  incoming: list[BlockId]
  outgoing: list[BlockId]

  @classmethod
  def make(cls, namespace: str, block: blocks.Block):
    make_id = lambda b: BlockId(f"{namespace}:{b.id}")
    block_id = make_id(block)
    code = "\n".join(str(op) for op in block.code)
    code = f"<{namespace}: {block.id}>\n" + code
    incoming = [make_id(b) for b in block.incoming]
    outgoing = [make_id(b) for b in block.outgoing]
    return cls(
        id=block_id,
        code=code,
        incoming=incoming,
        outgoing=outgoing,
    )


@dataclasses.dataclass
class SerializedCode:
  blocks: list[SerializedBlock]


class BlockGraphEncoder(json.JSONEncoder):
  """Implements the JSONEncoder behavior for ordered bytecode blocks."""

  def __init__(self, *args, **kwargs):
    super().__init__(*args, **kwargs)

  def _encode_code(self, code: SerializedCode) -> dict[str, Any]:
    return {
        "_type": "Code",
        "blocks": [self._encode_block(b) for b in code.blocks],
    }

  def _encode_block(self, block: SerializedBlock) -> dict[str, Any]:
    return {
        "_type": "Block",
        "id": block.id,
        "code": block.code,
        "incoming": block.incoming,
        "outgoing": block.outgoing,
    }

  def default(self, o):
    if isinstance(o, SerializedCode):
      return self._encode_code(o)
    elif isinstance(o, SerializedBlock):
      return self._encode_block(o)
    else:
      return super().default(o)


def encode_merged_graph(block_graph):
  out = []
  for k, v in block_graph.graph.items():
    for b in v.order:
      out.append(SerializedBlock.make(k, b))
  sc = SerializedCode(out)
  return json.dumps(sc, cls=BlockGraphEncoder)
