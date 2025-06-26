from ._core import (
    DecodeError,
    EncodeError,
    Field as _Field,
    Meta,
    MsgspecError,
    Raw,
    Struct,
    UnsetType,
    UNSET,
    NODEFAULT,
    ValidationError,
    defstruct,
    convert,
    to_builtins,
)


def field(*, default=NODEFAULT, default_factory=NODEFAULT, name=None):
    return _Field(default=default, default_factory=default_factory, name=name)


field.__doc__ = _Field.__doc__


from . import msgpack
from . import json
from . import yaml
from . import toml
from . import inspect
from . import structs
from ._version import get_versions

__version__ = get_versions()["version"]
del get_versions
