from pytype.rewrite import frame as frame_lib
from pytype.rewrite.abstract import abstract
from pytype.rewrite.tests import test_utils

import unittest


class TestBase(test_utils.ContextfulTestBase):

  def setUp(self):
    super().setUp()
    frame = frame_lib.Frame(
        self.ctx,
        '__main__',
        test_utils.parse(''),
        initial_locals={},
        initial_globals={},
    )
    self.helper = frame._call_helper


class MakeFunctionArgsTest(TestBase):

  def test_make_args_positional(self):
    raw_args = [
        self.ctx.consts[0].to_variable(),
        self.ctx.consts[1].to_variable(),
    ]
    args = self.helper.make_function_args(raw_args)
    self.assertEqual(
        args, abstract.Args(posargs=tuple(raw_args), frame=self.helper._frame)
    )

  def test_make_args_positional_and_keyword(self):
    raw_args = [
        self.ctx.consts[0].to_variable(),
        self.ctx.consts[1].to_variable(),
    ]
    self.helper.set_kw_names(('x',))
    args = self.helper.make_function_args(raw_args)
    expected_args = abstract.Args(
        posargs=(raw_args[0],),
        kwargs={'x': raw_args[1]},
        frame=self.helper._frame,
    )
    self.assertEqual(args, expected_args)

  def test_make_args_varargs(self):
    varargs = abstract.Tuple(self.ctx, (self.ctx.consts[0].to_variable(),))
    args = self.helper.make_function_args_ex(varargs.to_variable(), None)
    expected_args = abstract.Args(
        posargs=(self.ctx.consts[0].to_variable(),),
        starstarargs=None,
        frame=self.helper._frame,
    )
    self.assertEqual(args, expected_args)

  def test_make_args_kwargs(self):
    varargs = abstract.Tuple(self.ctx, ())
    kwargs = abstract.Dict(
        self.ctx,
        {
            self.ctx.consts['k'].to_variable(): self.ctx.consts[
                'v'
            ].to_variable()
        },
    )
    args = self.helper.make_function_args_ex(
        varargs.to_variable(), kwargs.to_variable()
    )
    expected_args = abstract.Args(
        posargs=(),
        kwargs={'k': self.ctx.consts['v'].to_variable()},
        starargs=None,
        frame=self.helper._frame,
    )
    self.assertEqual(args, expected_args)


class BuildClassTest(TestBase):

  def test_build(self):
    code = test_utils.parse('def C(): pass').consts[0]
    builder = abstract.InterpreterFunction(
        ctx=self.ctx,
        name='C',
        code=code,
        enclosing_scope=(),
        parent_frame=self.helper._frame,
    )
    args = abstract.Args(
        posargs=(builder.to_variable(), self.ctx.consts['C'].to_variable()),
        frame=self.helper._frame,
    )
    self.helper._frame.step()  # initialize frame state
    cls = self.helper.build_class(args)
    self.assertEqual(cls.name, 'C')


if __name__ == '__main__':
  unittest.main()
