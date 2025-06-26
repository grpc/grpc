"""Type system extensions for use with pytype."""
import dataclasses
import typing
from typing import Any, Callable, Dict, Tuple, TypeVar

import attr

# pylint: disable=g-import-not-at-top
try:
  from typing import Protocol
except ImportError:
  # typing.Protocol was added in python 3.8.
  from typing_extensions import Protocol
# pylint: enable=g-import-not-at-top

T = TypeVar('T')


class Attrs(Protocol[T]):
  """Protocol that matches any `attrs` class (or instance thereof).

  Can be used to match any `attrs` class. Example:

  @attrs.define
  class Foo:
    x: str
    y: int

  @attrs.define
  class Bar:
    x: str
    y: str

  class Baz:
    x: str
    y: int

  def foo(item: Attr):
    pass

  def bar(item: Attr[str]):
    pass

  def baz(item: Attr[Union[int, str]]):
    pass

  foo(Foo(x='yes', y=1))     # ok
  foo(Bar(x='yes', y='no'))  # ok
  foo(Baz(x='yes', y=1))     # error, not a `attrs` class

  bar(Foo(x='yes', y=1))     # error, has a non-str field
  bar(Bar(x='yes', y='no'))  # ok
  bar(Baz(x='yes', y=1))     # error, not a `attrs` class

  baz(Foo(x='yes', y=1))     # ok
  baz(Bar(x='yes', y='no'))  # ok
  baz(Baz(x='yes', y=1))     # error, not a `attrs` class

  The only way to identify an `attrs` class is to test for the presence of the
  `__attrs_attrs__` member; that is what attrs.has uses:
  https://github.com/python-attrs/attrs/blob/main/src/attr/_funcs.py#L290
  """

  __attrs_attrs__: Tuple['attr.Attribute[T]', ...]


class Dataclass(Protocol[T]):
  """Protocol that matches any dataclass (or instance thereof).

  Can be used to match any dataclass. Example (modulo pytype bugs):

  @dataclasses.dataclass
  class Foo:
    x: str
    y: int

  @dataclasses.dataclass
  class Bar:
    x: str
    y: str

  class Baz:
    x: str
    y: int

  def foo(item: Dataclass):
    pass

  def bar(item: Dataclass[str]):
    pass

  def baz(item: Dataclass[Union[int, str]]):
    pass

  foo(Foo(x='yes', y=1))     # ok
  foo(Bar(x='yes', y='no'))  # ok
  foo(Baz(x='yes', y=1))     # error, not a dataclass

  bar(Foo(x='yes', y=1))     # error, has a non-str field
  bar(Bar(x='yes', y='no'))  # ok
  bar(Baz(x='yes', y=1))     # error, not a dataclass

  baz(Foo(x='yes', y=1))     # ok
  baz(Bar(x='yes', y='no'))  # ok
  baz(Baz(x='yes', y=1))     # error, not a dataclass

  The only way to identify a dataclass is to test for the presence of the
  `__dataclass_fields__` member; that is what `dataclasses.is_dataclass` uses:
  https://github.com/python/cpython/blob/3.7/Lib/dataclasses.py#L1036.
  """

  __dataclass_fields__: Dict[str, 'dataclasses.Field[T]']


if typing.TYPE_CHECKING:

  _GenericCallable = TypeVar('_GenericCallable', bound=Callable[..., Any])

  class Decorator:
    """A type annotation for decorators that do not change signatures.

    This is a stand-in for using `Callable[[T], T]` to represent a decorator.

    Given a decorator function, which takes in a callable and returns a callable
    with the same signature, apply this class as a decorator to that function.
    This can also be used for decorator factories.

    Examples:

    Plain decorator (decorator matches Callable[[T], T]):

    >>> @pytype_extensions.Decorator
    ... def MyDecorator(func):
    ...   def wrapper(...):
    ...     ...
    ...   return wrapper

    Decorator factory (factory matches Callable[..., Callable[[T], T]]):

    >>> def MyDecoratorFactory(foo: int) -> pytype_extensions.Decorator:
    ...   @pytype_extensions.Decorator
    ...   def MyDecorator(func):
    ...     def Wrapper(*args, **kwargs):
    ...       return func(foo, *args, **kwargs)
    ...     return Wrapper
    ...   return MyDecorator

    Note for the above example: the return type annotation (first line) is the
    most important one; it indicates to callers that MyDecoratorFactory is
    returning a decorator. The "@pytype_extensions.Decorator" annotation (second
    line) indicates to pytype that MyDecorator is a Decorator; without it, you
    would need to add "pytype: disable=bad-return-type" on the final line.

    This class only exists at build time, for typechecking. At runtime, the
    'Decorator' member of this module is a simple identity function (see below).

    More information: pytype-decorators

    Shortlink: pytype_extensions.Decorator
    """
    # pylint: disable=line-too-long, unused-argument

    def __init__(self, decorator: Callable[[_GenericCallable], _GenericCallable]):
      ...

    def __call__(self, func: _GenericCallable) -> _GenericCallable:
      ...  # pytype: disable=bad-return-type

else:
  # At runtime, Decorator is a simple identity function that returns its input.
  Decorator = lambda d: d


def assert_type(__x, __t) -> None:  # pylint: disable=invalid-name
  """Prevent runtime errors from assert_type statements.

  assert_type is handled internally by pytype at type-checking time; it should
  do nothing at runtime.

  Usage example:

  ```
  import pytype_extensions
  assert_type = pytype_extensions.assert_type

  x = 3
  assert_type(x, int)
  ```

  Args:
    __x: The object to make the type assertion about.
    __t: The type we want to assert.
  """
  del __x, __t  # Unused.
