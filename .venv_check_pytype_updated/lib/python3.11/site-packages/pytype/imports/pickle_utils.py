"""Pickle file loading and saving.

This file is tested by serialize_ast_test.py.
"""

# We now use msgspec for serialization, which prompted a rewrite of this file's
# API. In the new API, only serialize_ast.SerializableAst and
# serialize_ast.ModuleBundle may be directly serialized and deserialized.
# There are methods for handling pytd.TypeDeclUnit objects, which will be
# turned into SerializableAst objects before serialization, and will be
# deserialized as SerializableAst objects.
# - Turn an object into binary data: (i.e. in-memory serialization.)
#   - Old: SavePickle(filename=None), or StoreAst(filename=None) for
#   pytd.TypeDeclUnit.
#   - New: Encode(), or Serialize() for pytd.TypeDeclUnit.
# - Turn binary data into an object:
#   - Old: LoadAst()
#   - New: DecodeAst() for SerializableAst, DecodeBuiltins() for ModuleBundle.
# - Turn an object into binary data and save it to a file:
#   - Old: SavePickle(filename=str), StoreAst(filename=str) for
#   pytd.TypeDeclUnit.
#   - New: Save() for SerializableAst and ModuleBundle, SerializeAndSave() for
#   pytd.TypeDeclUnit.
# - Load binary data from a file and turn it into an object:
#   - Old: LoadPickle()
#   - New: LoadAst() for SerializeAst, LoadBuiltins() for ModuleBundle.
# - There is also PrepareModuleBundle, which takes an iterable of (typically
# builtin) modules to be encoded in one file.

from collections.abc import Iterable
import gzip
import os
from typing import TypeVar, Union

import msgspec
from pytype.pytd import pytd
from pytype.pytd import serialize_ast

Path = Union[str, os.PathLike[str]]


class LoadPickleError(Exception):
  """Errors when loading a pickled pytd file."""

  def __init__(self, filename: Path):
    self.filename = os.fspath(filename)
    msg = f"Error loading pickle file: {self.filename}"
    super().__init__(msg)


Encoder = msgspec.msgpack.Encoder(order="deterministic")
AstDecoder = msgspec.msgpack.Decoder(type=serialize_ast.SerializableAst)
BuiltinsDecoder = msgspec.msgpack.Decoder(type=serialize_ast.ModuleBundle)

_DecT = TypeVar(
    "_DecT", serialize_ast.SerializableAst, serialize_ast.ModuleBundle
)
_Dec = msgspec.msgpack.Decoder
_Serializable = Union[serialize_ast.SerializableAst, serialize_ast.ModuleBundle]


def _Load(
    dec: "_Dec[_DecT]",
    filename: Path,
    compress: bool = False,
    open_function=open,
) -> _DecT:
  """Loads a serialized file.

  Args:
    dec: The msgspec.Decoder to use.
    filename: The file to read.
    compress: if True, the file will be opened using gzip.
    open_function: The function to open the file with.

  Returns:
    The decoded object.

  Raises:
    LoadPickleError, if there is an OSError, gzip error, or msgspec error.
  """
  try:
    with open_function(filename, "rb") as fi:
      if compress:
        with gzip.GzipFile(fileobj=fi) as zfi:
          data = zfi.read()
      else:
        data = fi.read()
    return dec.decode(data)
  except (
      OSError,
      gzip.BadGzipFile,
      msgspec.DecodeError,
      msgspec.ValidationError,
  ) as e:
    raise LoadPickleError(filename) from e


def DecodeAst(data: bytes) -> serialize_ast.SerializableAst:
  return AstDecoder.decode(data)


def LoadAst(
    filename: Path, compress: bool = False, open_function=open
) -> serialize_ast.SerializableAst:
  return _Load(AstDecoder, filename, compress, open_function)


def DecodeBuiltins(data: bytes) -> serialize_ast.ModuleBundle:
  return BuiltinsDecoder.decode(data)


def LoadBuiltins(
    filename: Path, compress: bool = False, open_function=open
) -> serialize_ast.ModuleBundle:
  return _Load(BuiltinsDecoder, filename, compress, open_function)


def Encode(obj: _Serializable) -> bytes:
  return Encoder.encode(obj)


def Save(
    obj: _Serializable,
    filename: Path,
    compress: bool = False,
    open_function=open,
) -> None:
  """Saves a serializable object to a file.

  Args:
    obj: The object to serialize.
    filename: filename to write to.
    compress: if True, the data will be compressed using gzip. The given
      filename will be used, unaltered.
    open_function: The function to use to open files. Defaults to the builtin
      open() function.
  """
  with open_function(filename, "wb") as fi:
    if compress:
      # We blank the filename and set the mtime explicitly to produce
      # deterministic gzip files.
      with gzip.GzipFile(filename="", mode="wb", fileobj=fi, mtime=1.0) as zfi:
        zfi.write(Encode(obj))
    else:
      fi.write(Encode(obj))


def Serialize(
    ast: pytd.TypeDeclUnit, src_path: str | None = None, metadata=None
) -> bytes:
  out = serialize_ast.SerializeAst(ast, src_path, metadata)
  return Encode(out)


def SerializeAndSave(
    ast: pytd.TypeDeclUnit,
    filename: Path,
    *,
    compress: bool = False,
    open_function=open,
    src_path: str | None = None,
    metadata=None,
) -> None:
  out = serialize_ast.SerializeAst(ast, src_path, metadata)
  Save(out, filename, compress, open_function)


def PrepareModuleBundle(
    modules: Iterable[tuple[str, str, pytd.TypeDeclUnit]],
) -> serialize_ast.ModuleBundle:
  raw = lambda ast, filename: msgspec.Raw(Serialize(ast, src_path=filename))
  return tuple(
      ((name, raw(module, filename)) for name, filename, module in modules)
  )
