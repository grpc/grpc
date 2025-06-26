"""Utilities for sealing instrumented types for testing (aka fakes, mocks).

NOTE: The term "sealing" here only applies to STATIC TYPE CHECKING
      (e.g. pytype).

The main idea behind these utilities is:

  Only test code needs to, and should have, awareness of instrumented types.
  Production code should only ever see production type APIs.

Wikipedia:
    In the context of computer programming, INSTRUMENTATION refers to the
    measure of a product's performance, in order to diagnose errors and to
    write trace information.

Wikipedia:
    A mechanical SEAL is a device that helps join systems and mechanisms
    together by preventing leakage, containing pressure, or excluding
    contamination.

In the context of extension types (C++ types wrapped in Python objects), the
utilities here are particularly useful for safely instrumenting types that
cannot be initialized from Python. However, the utilities are more generally
useful, to ensure that the API of a production type is not unintentionally
contaminated with artifacts of the instrumented type.

General usage:

    from ... import instrumentation_for_testing as i4t

    class FakeType(i4t.ProductionType[RealType]):
      def __init__(self, ...):  # With arguments.
        ...
      ...

    obj = FakeType(...).Seal()  # Type checking "sees" obj as RealType.
    ...
    fake_obj = i4t.Unseal(obj, FakeType)

    ####

    @i4t.SealAsProductionType(RealSimple)
    class FakeSimple:
      def __init__(self):  # No arguments.
        ...
      ...

    obj = FakeSimple()  # Already sealed. (The unsealed type is inaccessible.)
"""

from typing import Any, Generic, TypeVar
from typing import Type  # Python 3.8 compatibility.

_T = TypeVar("_T")


class SealAsProductionType(Generic[_T]):

  def __init__(self, production_type: _T):
    pass

  def __call__(self, instrumented_type: Any) -> _T:
    return instrumented_type


class ProductionType(Generic[_T]):

  def Seal(self) -> _T:
    return self

  @classmethod
  def SealType(cls) -> Type[_T]:
    return cls


def Unseal(instrumented_object: Any, instrumented_type: Type[_T]) -> _T:
  assert instrumented_object.__class__ is instrumented_type
  return instrumented_object
