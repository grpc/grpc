"""Basic datatypes for pytype.

This module contains generic base classes that are independent of the internal
representation of pytype objects. Classes here are intended to be largely opaque
types, with a few exposed methods and properties that code not tightly coupled
to the internal representation can use.

Some guiding princples:
- Types here should not depend on any other part of the pytype codebase, except
  possibly the pytd types.
- Representation-independent code like the test framework and error reporting
  module should use types from here wherever possible, rather than using
  concrete types from abstract/
- This module should be considered public, for code that uses pytype as a
  library.
"""

from pytype.types import base
from pytype.types import classes
from pytype.types import functions
from pytype.types import instances


BaseValue = base.BaseValue
Variable = base.Variable

Attribute = classes.Attribute
Class = classes.Class

Arg = functions.Arg
Args = functions.Args
Function = functions.Function
Signature = functions.Signature

Module = instances.Module
PythonConstant = instances.PythonConstant
