from collections.abc import Sequence

from pytype.rewrite.abstract import classes
from pytype.rewrite.abstract import functions
from pytype.rewrite.tests import test_utils
from typing_extensions import assert_type

import unittest


class FakeFrame:

  def __init__(self, ctx):
    self.ctx = ctx
    self.name = ''
    self.child_frames = []
    self.final_locals = {}
    self.stack = [self]
    self.functions = []
    self.classes = []

  def make_child_frame(self, func, initial_locals):
    self.child_frames.append((func, initial_locals))
    return self

  def run(self):
    pass

  def get_return_value(self):
    return self.ctx.consts.Any

  def load_attr(self, target_var, attr_name):
    del target_var, attr_name  # unused
    return self.ctx.consts.Any.to_variable()


def _get_const(src: str):
  module_code = test_utils.parse(src)
  return module_code.consts[0]


class SignatureTest(test_utils.PytdTestBase,
                    test_utils.ContextfulTestBase):

  def from_pytd(self, pytd_string):
    pytd_sig, = self.build_pytd(pytd_string).signatures
    return functions.Signature.from_pytd(self.ctx, 'f', pytd_sig)

  def test_from_code(self):
    func_code = _get_const("""
      def f(x, /, *args, y, **kwargs):
        pass
    """)
    signature = functions.Signature.from_code(self.ctx, 'f', func_code)
    self.assertEqual(repr(signature), 'def f(x, /, *args, y, **kwargs) -> Any')

  def test_map_args(self):
    signature = functions.Signature(self.ctx, 'f', ('x', 'y'))
    x = self.ctx.consts['x'].to_variable()
    y = self.ctx.consts['y'].to_variable()
    args = signature.map_args(functions.Args((x, y)))
    self.assertEqual(args.argdict, {'x': x, 'y': y})

  def test_fake_args(self):
    annotations = {'x': self.ctx.types[int]}
    signature = functions.Signature(self.ctx, 'f', ('x', 'y'),
                                    annotations=annotations)
    args = signature.make_fake_args()
    self.assertEqual(set(args.argdict), {'x', 'y'})
    x = args.argdict['x'].get_atomic_value()
    self.assertIsInstance(x, classes.FrozenInstance)
    self.assertEqual(x.cls.name, 'int')
    self.assertEqual(args.argdict['y'].get_atomic_value(), self.ctx.consts.Any)

  def test_from_pytd_basic(self):
    sig = self.from_pytd('def f(): ...')
    self.assertEqual(repr(sig), 'def f() -> Any')

  def test_from_pytd_with_annotations(self):
    sig = self.from_pytd('def f(x: int) -> str: ...')
    self.assertEqual(repr(sig), 'def f(x: int) -> str')

  def test_from_pytd_with_defaults(self):
    sig = self.from_pytd('def f(x=0, y=...) -> str: ...')
    self.assertEqual(repr(sig), 'def f(x: int = ..., y: Any = ...) -> str')

  def test_from_pytd_with_special_args(self):
    sig = self.from_pytd('def f(x, /, *args, y, **kwargs): ...')
    self.assertEqual(
        repr(sig), 'def f(x: Any, /, *args: tuple, y, **kwargs: dict) -> Any')


class InterpreterFunctionTest(test_utils.ContextfulTestBase):

  def test_init(self):
    func_code = _get_const("""
      def f(x, /, *args, y, **kwargs):
        pass
    """)
    f = functions.InterpreterFunction(
        ctx=self.ctx, name='f', code=func_code, enclosing_scope=(),
        parent_frame=FakeFrame(self.ctx))
    self.assertEqual(len(f.signatures), 1)
    self.assertEqual(repr(f.signatures[0]),
                     'def f(x, /, *args, y, **kwargs) -> Any')

  def test_map_args(self):
    func_code = _get_const('def f(x): ...')
    f = functions.InterpreterFunction(
        ctx=self.ctx, name='f', code=func_code, enclosing_scope=(),
        parent_frame=FakeFrame(self.ctx))
    x = self.ctx.consts[0].to_variable()
    mapped_args = f.map_args(functions.Args(posargs=(x,)))
    self.assertEqual(mapped_args.signature, f.signatures[0])
    self.assertEqual(mapped_args.argdict, {'x': x})

  def test_call_with_mapped_args(self):
    f = functions.InterpreterFunction(
        ctx=self.ctx, name='f', code=_get_const('def f(x): ...'),
        enclosing_scope=(), parent_frame=FakeFrame(self.ctx))
    x = self.ctx.consts[0].to_variable()
    mapped_args = functions.MappedArgs(f.signatures[0], {'x': x})
    frame = f.call_with_mapped_args(mapped_args)
    assert_type(frame, FakeFrame)
    self.assertIsInstance(frame, FakeFrame)

  def test_call(self):
    f = functions.InterpreterFunction(
        ctx=self.ctx, name='f', code=_get_const('def f(): ...'),
        enclosing_scope=(), parent_frame=FakeFrame(self.ctx))
    frame = f.call(functions.Args())
    assert_type(frame, FakeFrame)
    self.assertIsInstance(frame, FakeFrame)

  def test_analyze(self):
    f = functions.InterpreterFunction(
        ctx=self.ctx, name='f', code=_get_const('def f(): ...'),
        enclosing_scope=(), parent_frame=FakeFrame(self.ctx))
    frames = f.analyze()
    assert_type(frames, Sequence[FakeFrame])
    self.assertEqual(len(frames), 1)
    self.assertIsInstance(frames[0], FakeFrame)


class PytdFunctionTest(test_utils.PytdTestBase,
                       test_utils.ContextfulTestBase):

  def test_return(self):
    pytd_func = self.build_pytd('def f() -> int: ...')
    func = self.ctx.abstract_converter.pytd_function_to_value(pytd_func)
    args = functions.MappedArgs(signature=func.signatures[0], argdict={})
    ret = func.call_with_mapped_args(args).get_return_value()
    self.assertIsInstance(ret, classes.FrozenInstance)
    self.assertEqual(ret.cls.name, 'int')


class BoundFunctionTest(test_utils.ContextfulTestBase):

  def test_call(self):
    f = functions.InterpreterFunction(
        ctx=self.ctx, name='f', code=_get_const('def f(self): ...'),
        enclosing_scope=(), parent_frame=FakeFrame(self.ctx))
    callself = self.ctx.consts[42]
    bound_f = f.bind_to(callself)
    frame = bound_f.call(functions.Args())
    assert_type(frame, FakeFrame)
    argdict = frame.child_frames[0][1]
    self.assertEqual(argdict, {'self': callself.to_variable()})

  def test_analyze(self):
    f = functions.InterpreterFunction(
        ctx=self.ctx, name='f', code=_get_const('def f(self): ...'),
        enclosing_scope=(), parent_frame=FakeFrame(self.ctx))
    callself = self.ctx.consts[42]
    bound_f = f.bind_to(callself)
    frames = bound_f.analyze()
    assert_type(frames, Sequence[FakeFrame])
    self.assertEqual(len(frames), 1)
    argdict = frames[0].child_frames[0][1]
    self.assertEqual(argdict, {'self': callself.to_variable()})


if __name__ == '__main__':
  unittest.main()
