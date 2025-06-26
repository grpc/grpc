import unittest
import pytype_extensions
from pytype_extensions import instrumentation_for_testing as i4t

assert_type = pytype_extensions.assert_type


# Similar to an extension type that cannot be initialized from Python.
class NoCtor:

  def __init__(self):
    raise RuntimeError("Meant to be inaccessible")

  def Mul100(self, i):
    return i * 100


class FakeNoCtor(i4t.ProductionType[NoCtor]):

  def __init__(self, state):
    self.state = state
    self.call_count = 0

  def Mul100(self, i):
    self.call_count += 1
    return self.state * i * 100


# When access to instrumented_type is needed and the fake __init__ signature is
# DIFFERENT from that of production_type.
class FakeNoCtorInitArgUnsealed(i4t.ProductionType[NoCtor]):

  def __init__(self, state):
    self.state = state

  def Mul100(self, i):
    return self.state * i * 103


# When access to instrumented_type is needed and the fake __init__ signature is
# IDENTICAL to that of production_type (or __init__ has no arguments if
# production_type has no __init__).
class FakeNoCtorDefaultInitUnsealed(i4t.ProductionType[NoCtor]):

  # __init__ is intentionally missing.

  def Mul100(self, i):
    return i * 104


FakeNoCtorDefaultInitSealed = FakeNoCtorDefaultInitUnsealed.SealType()


# Very similar to FakeNoCtorDefaultInitUnsealed, but with
# explicit __init__.
class FakeNoCtorInitNoArgsUnsealed(i4t.ProductionType[NoCtor]):

  def __init__(self):
    self.state = 8

  def Mul100(self, i):
    return self.state * i * 105


FakeNoCtorInitNoArgsSealed = FakeNoCtorInitNoArgsUnsealed.SealType()


# When access to instrumented_type is not needed.
@i4t.SealAsProductionType(NoCtor)
class FakeNoCtorSealedAs:

  def __init__(self):
    self.state = 3

  def Mul100(self, i):
    return self.state * i * 102


def ProductionCodePassNoCtor(obj: NoCtor):
  return obj.Mul100(2)


class WithCtor:

  def __init__(self, state):
    self.state = state

  def Mul100(self, i):
    return self.state * i * 100


class FakeWithCtor(WithCtor, i4t.ProductionType[WithCtor]):

  def __init__(self):  # pylint: disable=super-init-not-called
    # Assume state is difficult to generate via the normal __init__, which is
    # therefore intentionally not called from here.
    self.state = 5


def ProductionCodePassWithCtor(obj: WithCtor):
  return obj.Mul100(7)


class InstrumentationForTestingTest(unittest.TestCase):

  def testFakeNoCtor(self):
    orig_fake_obj = FakeNoCtor(3)
    obj = orig_fake_obj.Seal()
    assert_type(obj, NoCtor)
    for expected_call_count in (1, 2):
      self.assertEqual(ProductionCodePassNoCtor(obj), 600)
      fake_obj = i4t.Unseal(obj, FakeNoCtor)
      assert fake_obj is orig_fake_obj
      assert_type(fake_obj, FakeNoCtor)
      self.assertEqual(fake_obj.call_count, expected_call_count)

  def testFakeNoCtorInitArg(self):
    obj = FakeNoCtorInitArgUnsealed(5).Seal()
    assert_type(obj, NoCtor)
    self.assertEqual(ProductionCodePassNoCtor(obj), 1030)
    fake_obj = i4t.Unseal(obj, FakeNoCtorInitArgUnsealed)
    assert_type(fake_obj, FakeNoCtorInitArgUnsealed)
    self.assertEqual(fake_obj.state, 5)

  def testFakeNoCtorDefaultInit(self):
    obj = FakeNoCtorDefaultInitSealed()
    assert_type(obj, NoCtor)
    self.assertEqual(ProductionCodePassNoCtor(obj), 208)
    fake_obj = i4t.Unseal(obj, FakeNoCtorDefaultInitUnsealed)
    assert_type(fake_obj, FakeNoCtorDefaultInitUnsealed)

  def testFakeNoCtorInitNoArgs(self):
    obj = FakeNoCtorInitNoArgsSealed()
    assert_type(obj, NoCtor)
    self.assertEqual(ProductionCodePassNoCtor(obj), 1680)
    fake_obj = i4t.Unseal(obj, FakeNoCtorInitNoArgsUnsealed)
    assert_type(fake_obj, FakeNoCtorInitNoArgsUnsealed)
    self.assertEqual(fake_obj.state, 8)

  def testFakeNoCtorSealedAs(self):
    obj = FakeNoCtorSealedAs()
    assert_type(obj, NoCtor)
    self.assertEqual(ProductionCodePassNoCtor(obj), 612)

  def testFakeWithCtor(self):
    orig_fake_obj = FakeWithCtor()
    obj = orig_fake_obj.Seal()
    assert_type(obj, WithCtor)
    self.assertEqual(ProductionCodePassWithCtor(obj), 3500)
    fake_obj = i4t.Unseal(obj, FakeWithCtor)
    assert fake_obj is orig_fake_obj
    assert_type(fake_obj, FakeWithCtor)


if __name__ == "__main__":
  unittest.main()
