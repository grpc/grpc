# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

import dataclasses

import libcst as cst
import libcst.matchers as m
from libcst.matchers import matches
from libcst.testing.utils import UnitTest


class MatchersMatcherTest(UnitTest):
    def test_simple_matcher_true(self) -> None:
        # Match based on identical attributes.
        self.assertTrue(matches(cst.Name("foo"), m.Name("foo")))

    def test_simple_matcher_false(self) -> None:
        # Fail to match due to incorrect value on Name.
        self.assertFalse(matches(cst.Name("foo"), m.Name("bar")))

    def test_complex_matcher_true(self) -> None:
        # Match on any Call, not caring about arguments.
        self.assertTrue(
            matches(
                cst.Call(
                    func=cst.Name("foo"),
                    args=(
                        cst.Arg(cst.Integer("1")),
                        cst.Arg(cst.Integer("2")),
                        cst.Arg(cst.Integer("3")),
                    ),
                ),
                m.Call(),
            )
        )
        # Match on any Call to a function named "foo".
        self.assertTrue(
            matches(
                cst.Call(
                    func=cst.Name("foo"),
                    args=(
                        cst.Arg(cst.Integer("1")),
                        cst.Arg(cst.Integer("2")),
                        cst.Arg(cst.Integer("3")),
                    ),
                ),
                m.Call(m.Name("foo")),
            )
        )
        # Match on any Call to a function named "foo" with three arguments.
        self.assertTrue(
            matches(
                cst.Call(
                    func=cst.Name("foo"),
                    args=(
                        cst.Arg(cst.Integer("1")),
                        cst.Arg(cst.Integer("2")),
                        cst.Arg(cst.Integer("3")),
                    ),
                ),
                m.Call(func=m.Name("foo"), args=(m.Arg(), m.Arg(), m.Arg())),
            )
        )
        # Match any Call to a function named "foo" with three integer arguments.
        self.assertTrue(
            matches(
                cst.Call(
                    func=cst.Name("foo"),
                    args=(
                        cst.Arg(cst.Integer("1")),
                        cst.Arg(cst.Integer("2")),
                        cst.Arg(cst.Integer("3")),
                    ),
                ),
                m.Call(
                    func=m.Name("foo"),
                    args=(m.Arg(m.Integer()), m.Arg(m.Integer()), m.Arg(m.Integer())),
                ),
            )
        )
        # Match any Call to a function named "foo" with integer arguments 1, 2, 3.
        self.assertTrue(
            matches(
                cst.Call(
                    func=cst.Name("foo"),
                    args=(
                        cst.Arg(cst.Integer("1")),
                        cst.Arg(cst.Integer("2")),
                        cst.Arg(cst.Integer("3")),
                    ),
                ),
                m.Call(
                    func=m.Name("foo"),
                    args=(
                        m.Arg(m.Integer("1")),
                        m.Arg(m.Integer("2")),
                        m.Arg(m.Integer("3")),
                    ),
                ),
            )
        )
        # Match any Call to a function named "foo" with three arguments, the last one
        # being the integer 3.
        self.assertTrue(
            matches(
                cst.Call(
                    func=cst.Name("foo"),
                    args=(
                        cst.Arg(cst.Integer("1")),
                        cst.Arg(cst.Integer("2")),
                        cst.Arg(cst.Integer("3")),
                    ),
                ),
                m.Call(
                    func=m.Name("foo"),
                    args=(m.DoNotCare(), m.DoNotCare(), m.Arg(m.Integer("3"))),
                ),
            )
        )

    def test_complex_matcher_false(self) -> None:
        # Fail to match since this is a Call, not a FunctionDef.
        self.assertFalse(
            matches(
                cst.Call(
                    func=cst.Name("foo"),
                    args=(
                        cst.Arg(cst.Integer("1")),
                        cst.Arg(cst.Integer("2")),
                        cst.Arg(cst.Integer("3")),
                    ),
                ),
                m.FunctionDef(),
            )
        )
        # Fail to match a function named "bar".
        self.assertFalse(
            matches(
                cst.Call(
                    func=cst.Name("foo"),
                    args=(
                        cst.Arg(cst.Integer("1")),
                        cst.Arg(cst.Integer("2")),
                        cst.Arg(cst.Integer("3")),
                    ),
                ),
                m.Call(m.Name("bar")),
            )
        )
        # Fail to match a function named "foo" with two arguments.
        self.assertFalse(
            matches(
                cst.Call(
                    func=cst.Name("foo"),
                    args=(
                        cst.Arg(cst.Integer("1")),
                        cst.Arg(cst.Integer("2")),
                        cst.Arg(cst.Integer("3")),
                    ),
                ),
                m.Call(func=m.Name("foo"), args=(m.Arg(), m.Arg())),
            )
        )
        # Fail to match a function named "foo" with three integer arguments
        # 3, 2, 1.
        self.assertFalse(
            matches(
                cst.Call(
                    func=cst.Name("foo"),
                    args=(
                        cst.Arg(cst.Integer("1")),
                        cst.Arg(cst.Integer("2")),
                        cst.Arg(cst.Integer("3")),
                    ),
                ),
                m.Call(
                    func=m.Name("foo"),
                    args=(
                        m.Arg(m.Integer("3")),
                        m.Arg(m.Integer("2")),
                        m.Arg(m.Integer("1")),
                    ),
                ),
            )
        )
        # Fail to match a function named "foo" with three arguments, the last one
        # being the integer 1.
        self.assertFalse(
            matches(
                cst.Call(
                    func=cst.Name("foo"),
                    args=(
                        cst.Arg(cst.Integer("1")),
                        cst.Arg(cst.Integer("2")),
                        cst.Arg(cst.Integer("3")),
                    ),
                ),
                m.Call(
                    func=m.Name("foo"),
                    args=(m.DoNotCare(), m.DoNotCare(), m.Arg(m.Integer("1"))),
                ),
            )
        )

    def test_type_of_matcher_true(self) -> None:
        self.assertTrue(matches(cst.Name("true"), m.TypeOf(m.Name)))
        self.assertTrue(matches(cst.Name("true"), m.TypeOf(m.Name)(value="true")))
        self.assertTrue(matches(cst.Name("true"), m.Name | m.Float | m.SimpleString))
        self.assertTrue(
            matches(cst.SimpleString("'foo'"), m.TypeOf(m.Name, m.SimpleString))
        )
        self.assertTrue(
            matches(
                cst.SimpleString("'foo'"),
                m.TypeOf(m.Name, m.SimpleString)(value="'foo'"),
            )
        )
        with self.assertRaises(Exception):
            # pyre-ignore
            m.TypeOf(cst.Float)(value=1.0) | cst.Name

        with self.assertRaises(TypeError):
            # pyre-ignore
            m.TypeOf(cst.Float) & cst.SimpleString

        for case in (
            cst.BinaryOperation(
                left=cst.Name("foo"), operator=cst.Add(), right=cst.Name("bar")
            ),
            cst.BooleanOperation(
                left=cst.Name("foo"), operator=cst.Or(), right=cst.Name("bar")
            ),
        ):
            self.assertTrue(
                matches(
                    case, (m.BinaryOperation | m.BooleanOperation)(left=m.Name("foo"))
                )
            )
            new_case = dataclasses.replace(case, left=case.right, right=case.left)
            self.assertTrue(
                matches(
                    new_case,
                    ~(m.BinaryOperation | m.BooleanOperation)(left=m.Name("foo")),
                )
            )

    def test_type_of_matcher_false(self) -> None:
        self.assertFalse(matches(cst.Name("true"), m.TypeOf(m.SimpleString)))
        self.assertFalse(matches(cst.Name("true"), m.TypeOf(m.Name)(value="false")))
        self.assertFalse(
            matches(cst.Name("true"), m.TypeOf(m.SimpleString)(value="true"))
        )
        self.assertFalse(
            matches(cst.SimpleString("'foo'"), m.TypeOf(m.Name, m.Attribute))
        )
        self.assertFalse(
            matches(
                cst.SimpleString("'foo'"), m.TypeOf(m.Name, m.Attribute)(value="'foo'")
            )
        )
        self.assertFalse(
            matches(
                cst.SimpleString("'foo'"),
                m.TypeOf(m.Name, m.SimpleString)(value="'bar'"),
            )
        )

        for case in (
            cst.BinaryOperation(
                left=cst.Name("foo"), operator=cst.Add(), right=cst.Name("bar")
            ),
            cst.BooleanOperation(
                left=cst.Name("foo"), operator=cst.Or(), right=cst.Name("bar")
            ),
        ):
            self.assertFalse(
                matches(
                    case, (m.BinaryOperation | m.BooleanOperation)(left=m.Name("bar"))
                )
            )
            self.assertFalse(
                matches(
                    case, ~(m.BinaryOperation | m.BooleanOperation)(left=m.Name("foo"))
                )
            )

    def test_or_matcher_true(self) -> None:
        # Match on either True or False identifier.
        self.assertTrue(
            matches(cst.Name("True"), m.OneOf(m.Name("True"), m.Name("False")))
        )
        # Match when one of the option is a TypeOf
        self.assertTrue(
            matches(
                cst.Name("True"),
                m.OneOf(m.TypeOf(m.Name, m.NameItem)("True"), m.Name("False")),
            )
        )
        # Match any assignment that assigns a value of True or False to an
        # unspecified target.
        self.assertTrue(
            matches(
                cst.Assign((cst.AssignTarget(cst.Name("x")),), cst.Name("True")),
                m.Assign(value=m.OneOf(m.Name("True"), m.Name("False"))),
            )
        )
        self.assertTrue(
            matches(
                cst.Call(
                    func=cst.Name("foo"),
                    args=(
                        cst.Arg(cst.Integer("1")),
                        cst.Arg(cst.Integer("2")),
                        cst.Arg(cst.Integer("3")),
                    ),
                ),
                m.Call(
                    func=m.Name("foo"),
                    args=m.OneOf(
                        (
                            m.Arg(m.Integer("3")),
                            m.Arg(m.Integer("2")),
                            m.Arg(m.Integer("1")),
                        ),
                        (
                            m.Arg(m.Integer("1")),
                            m.Arg(m.Integer("2")),
                            m.Arg(m.Integer("3")),
                        ),
                    ),
                ),
            )
        )

    def test_or_matcher_false(self) -> None:
        # Fail to match since None is not True or False.
        self.assertFalse(
            matches(cst.Name("None"), m.OneOf(m.Name("True"), m.Name("False")))
        )
        # Fail to match since assigning None to a target is not the same as
        # assigning True or False to a target.
        self.assertFalse(
            matches(
                cst.Assign((cst.AssignTarget(cst.Name("x")),), cst.Name("None")),
                m.Assign(value=m.OneOf(m.Name("True"), m.Name("False"))),
            )
        )
        self.assertFalse(
            matches(
                cst.Call(
                    func=cst.Name("foo"),
                    args=(
                        cst.Arg(cst.Integer("1")),
                        cst.Arg(cst.Integer("2")),
                        cst.Arg(cst.Integer("3")),
                    ),
                ),
                m.Call(
                    func=m.Name("foo"),
                    args=m.OneOf(
                        (
                            m.Arg(m.Integer("3")),
                            m.Arg(m.Integer("2")),
                            m.Arg(m.Integer("1")),
                        ),
                        (
                            m.Arg(m.Integer("4")),
                            m.Arg(m.Integer("5")),
                            m.Arg(m.Integer("6")),
                        ),
                    ),
                ),
            )
        )

    def test_or_operator_matcher_true(self) -> None:
        # Match on either True or False identifier.
        self.assertTrue(matches(cst.Name("True"), m.Name("True") | m.Name("False")))
        # Match on either True or False identifier.
        self.assertTrue(matches(cst.Name("False"), m.Name("True") | m.Name("False")))
        # Match on either True, False or None identifier.
        self.assertTrue(
            matches(cst.Name("None"), m.Name("True") | m.Name("False") | m.Name("None"))
        )
        # Match any assignment that assigns a value of True or False to an
        # unspecified target.
        self.assertTrue(
            matches(
                cst.Assign((cst.AssignTarget(cst.Name("x")),), cst.Name("True")),
                m.Assign(value=m.Name("True") | m.Name("False")),
            )
        )

    def test_or_operator_matcher_false(self) -> None:
        # Fail to match since None is not True or False.
        self.assertFalse(matches(cst.Name("None"), m.Name("True") | m.Name("False")))
        # Fail to match since assigning None to a target is not the same as
        # assigning True or False to a target.
        self.assertFalse(
            matches(
                cst.Assign((cst.AssignTarget(cst.Name("x")),), cst.Name("None")),
                m.Assign(value=m.Name("True") | m.Name("False")),
            )
        )

    def test_zero_or_more_matcher_no_args_true(self) -> None:
        # Match a function call to "foo" with any number of arguments as
        # long as the first one is an integer with the value 1.
        self.assertTrue(
            matches(
                cst.Call(
                    func=cst.Name("foo"),
                    args=(
                        cst.Arg(cst.Integer("1")),
                        cst.Arg(cst.Integer("2")),
                        cst.Arg(cst.Integer("3")),
                    ),
                ),
                m.Call(
                    func=m.Name("foo"), args=(m.Arg(m.Integer("1")), m.ZeroOrMore())
                ),
            )
        )
        # Match a function call to "foo" with any number of arguments as
        # long as one of them is an integer with the value 1.
        self.assertTrue(
            matches(
                cst.Call(
                    func=cst.Name("foo"),
                    args=(
                        cst.Arg(cst.Integer("1")),
                        cst.Arg(cst.Integer("2")),
                        cst.Arg(cst.Integer("3")),
                    ),
                ),
                m.Call(
                    func=m.Name("foo"),
                    args=(m.ZeroOrMore(), m.Arg(m.Integer("1")), m.ZeroOrMore()),
                ),
            )
        )
        # Match a function call to "foo" with any number of arguments as
        # long as one of them is an integer with the value 2.
        self.assertTrue(
            matches(
                cst.Call(
                    func=cst.Name("foo"),
                    args=(
                        cst.Arg(cst.Integer("1")),
                        cst.Arg(cst.Integer("2")),
                        cst.Arg(cst.Integer("3")),
                    ),
                ),
                m.Call(
                    func=m.Name("foo"),
                    args=(m.ZeroOrMore(), m.Arg(m.Integer("2")), m.ZeroOrMore()),
                ),
            )
        )
        # Match a function call to "foo" with any number of arguments as
        # long as one of them is an integer with the value 3.
        self.assertTrue(
            matches(
                cst.Call(
                    func=cst.Name("foo"),
                    args=(
                        cst.Arg(cst.Integer("1")),
                        cst.Arg(cst.Integer("2")),
                        cst.Arg(cst.Integer("3")),
                    ),
                ),
                m.Call(
                    func=m.Name("foo"),
                    args=(m.ZeroOrMore(), m.Arg(m.Integer("3")), m.ZeroOrMore()),
                ),
            )
        )
        # Match a function call to "foo" with any number of arguments as
        # long as the last one is an integer with the value 3.
        self.assertTrue(
            matches(
                cst.Call(
                    func=cst.Name("foo"),
                    args=(
                        cst.Arg(cst.Integer("1")),
                        cst.Arg(cst.Integer("2")),
                        cst.Arg(cst.Integer("3")),
                    ),
                ),
                m.Call(
                    func=m.Name("foo"), args=(m.ZeroOrMore(), m.Arg(m.Integer("3")))
                ),
            )
        )
        # Match a function call to "foo" with any number of arguments as
        # long as there are two arguments with the values 1 and 3 anywhere
        # in the argument list, respecting order.
        self.assertTrue(
            matches(
                cst.Call(
                    func=cst.Name("foo"),
                    args=(
                        cst.Arg(cst.Integer("1")),
                        cst.Arg(cst.Integer("2")),
                        cst.Arg(cst.Integer("3")),
                    ),
                ),
                m.Call(
                    func=m.Name("foo"),
                    args=(
                        m.ZeroOrMore(),
                        m.Arg(m.Integer("1")),
                        m.ZeroOrMore(),
                        m.Arg(m.Integer("3")),
                        m.ZeroOrMore(),
                    ),
                ),
            )
        )
        # Match a function call to "foo" with any number of arguments as
        # long as there are three arguments with the values 1, 2 and 3 anywhere
        # in the argument list, respecting order.
        self.assertTrue(
            matches(
                cst.Call(
                    func=cst.Name("foo"),
                    args=(
                        cst.Arg(cst.Integer("1")),
                        cst.Arg(cst.Integer("2")),
                        cst.Arg(cst.Integer("3")),
                    ),
                ),
                m.Call(
                    func=m.Name("foo"),
                    args=(
                        m.ZeroOrMore(),
                        m.Arg(m.Integer("1")),
                        m.ZeroOrMore(),
                        m.Arg(m.Integer("2")),
                        m.ZeroOrMore(),
                        m.Arg(m.Integer("3")),
                        m.ZeroOrMore(),
                    ),
                ),
            )
        )

    def test_at_least_n_matcher_no_args_true(self) -> None:
        # Match a function call to "foo" with at least one argument.
        self.assertTrue(
            matches(
                cst.Call(
                    func=cst.Name("foo"),
                    args=(
                        cst.Arg(cst.Integer("1")),
                        cst.Arg(cst.Integer("2")),
                        cst.Arg(cst.Integer("3")),
                    ),
                ),
                m.Call(func=m.Name("foo"), args=(m.AtLeastN(n=1),)),
            )
        )
        # Match a function call to "foo" with at least two arguments.
        self.assertTrue(
            matches(
                cst.Call(
                    func=cst.Name("foo"),
                    args=(
                        cst.Arg(cst.Integer("1")),
                        cst.Arg(cst.Integer("2")),
                        cst.Arg(cst.Integer("3")),
                    ),
                ),
                m.Call(func=m.Name("foo"), args=(m.AtLeastN(n=2),)),
            )
        )
        # Match a function call to "foo" with at least three arguments.
        self.assertTrue(
            matches(
                cst.Call(
                    func=cst.Name("foo"),
                    args=(
                        cst.Arg(cst.Integer("1")),
                        cst.Arg(cst.Integer("2")),
                        cst.Arg(cst.Integer("3")),
                    ),
                ),
                m.Call(func=m.Name("foo"), args=(m.AtLeastN(n=3),)),
            )
        )
        # Match a function call to "foo" with at least two arguments the
        # first one being the integer 1.
        self.assertTrue(
            matches(
                cst.Call(
                    func=cst.Name("foo"),
                    args=(
                        cst.Arg(cst.Integer("1")),
                        cst.Arg(cst.Integer("2")),
                        cst.Arg(cst.Integer("3")),
                    ),
                ),
                m.Call(
                    func=m.Name("foo"), args=(m.Arg(m.Integer("1")), m.AtLeastN(n=1))
                ),
            )
        )
        # Match a function call to "foo" with at least three arguments the
        # first one being the integer 1.
        self.assertTrue(
            matches(
                cst.Call(
                    func=cst.Name("foo"),
                    args=(
                        cst.Arg(cst.Integer("1")),
                        cst.Arg(cst.Integer("2")),
                        cst.Arg(cst.Integer("3")),
                    ),
                ),
                m.Call(
                    func=m.Name("foo"), args=(m.Arg(m.Integer("1")), m.AtLeastN(n=2))
                ),
            )
        )
        # Match a function call to "foo" with at least three arguments. The
        # There should be an argument with the value 2, which should have
        # at least one argument before and one argument after.
        self.assertTrue(
            matches(
                cst.Call(
                    func=cst.Name("foo"),
                    args=(
                        cst.Arg(cst.Integer("1")),
                        cst.Arg(cst.Integer("2")),
                        cst.Arg(cst.Integer("3")),
                    ),
                ),
                m.Call(
                    func=m.Name("foo"),
                    args=(m.AtLeastN(n=1), m.Arg(m.Integer("2")), m.AtLeastN(n=1)),
                ),
            )
        )
        # Match a function call to "foo" with at least two arguments, the last
        # one being the value 3.
        self.assertTrue(
            matches(
                cst.Call(
                    func=cst.Name("foo"),
                    args=(
                        cst.Arg(cst.Integer("1")),
                        cst.Arg(cst.Integer("2")),
                        cst.Arg(cst.Integer("3")),
                    ),
                ),
                m.Call(
                    func=m.Name("foo"), args=(m.AtLeastN(n=1), m.Arg(m.Integer("3")))
                ),
            )
        )
        # Match a function call to "foo" with at least three arguments, the last
        # one being the value 3.
        self.assertTrue(
            matches(
                cst.Call(
                    func=cst.Name("foo"),
                    args=(
                        cst.Arg(cst.Integer("1")),
                        cst.Arg(cst.Integer("2")),
                        cst.Arg(cst.Integer("3")),
                    ),
                ),
                m.Call(
                    func=m.Name("foo"), args=(m.AtLeastN(n=2), m.Arg(m.Integer("3")))
                ),
            )
        )

    def test_at_least_n_matcher_no_args_false(self) -> None:
        # Fail to match a function call to "foo" with at least four arguments.
        self.assertFalse(
            matches(
                cst.Call(
                    func=cst.Name("foo"),
                    args=(
                        cst.Arg(cst.Integer("1")),
                        cst.Arg(cst.Integer("2")),
                        cst.Arg(cst.Integer("3")),
                    ),
                ),
                m.Call(func=m.Name("foo"), args=(m.AtLeastN(n=4),)),
            )
        )
        # Fail to match a function call to "foo" with at least four arguments,
        # the first one being the value 1.
        self.assertFalse(
            matches(
                cst.Call(
                    func=cst.Name("foo"),
                    args=(
                        cst.Arg(cst.Integer("1")),
                        cst.Arg(cst.Integer("2")),
                        cst.Arg(cst.Integer("3")),
                    ),
                ),
                m.Call(
                    func=m.Name("foo"), args=(m.Arg(m.Integer("1")), m.AtLeastN(n=3))
                ),
            )
        )
        # Fail to match a function call to "foo" with at least three arguments,
        # the last one being the value 2.
        self.assertFalse(
            matches(
                cst.Call(
                    func=cst.Name("foo"),
                    args=(
                        cst.Arg(cst.Integer("1")),
                        cst.Arg(cst.Integer("2")),
                        cst.Arg(cst.Integer("3")),
                    ),
                ),
                m.Call(
                    func=m.Name("foo"), args=(m.AtLeastN(n=2), m.Arg(m.Integer("2")))
                ),
            )
        )

    def test_zero_or_more_matcher_args_true(self) -> None:
        # Match a function call to "foo" where the first argument is the integer
        # value 1, and the rest of the arguements are wildcards.
        self.assertTrue(
            matches(
                cst.Call(
                    func=cst.Name("foo"),
                    args=(
                        cst.Arg(cst.Integer("1")),
                        cst.Arg(cst.Integer("2")),
                        cst.Arg(cst.Integer("3")),
                    ),
                ),
                m.Call(
                    func=m.Name("foo"),
                    args=(m.Arg(m.Integer("1")), m.ZeroOrMore(m.Arg())),
                ),
            )
        )
        # Match a function call to "foo" where the first argument is the integer
        # value 1, and the rest of the arguements are integers of any value.
        self.assertTrue(
            matches(
                cst.Call(
                    func=cst.Name("foo"),
                    args=(
                        cst.Arg(cst.Integer("1")),
                        cst.Arg(cst.Integer("2")),
                        cst.Arg(cst.Integer("3")),
                    ),
                ),
                m.Call(
                    func=m.Name("foo"),
                    args=(m.Arg(m.Integer("1")), m.ZeroOrMore(m.Arg(m.Integer()))),
                ),
            )
        )
        # Match a function call to "foo" with zero or more arguments, where the
        # first argument can optionally be the integer 1 or 2, and the second
        # can only be the integer 2. This case verifies non-greedy behavior in the
        # matcher.
        self.assertTrue(
            matches(
                cst.Call(
                    func=cst.Name("foo"),
                    args=(
                        cst.Arg(cst.Integer("1")),
                        cst.Arg(cst.Integer("2")),
                        cst.Arg(cst.Integer("3")),
                    ),
                ),
                m.Call(
                    func=m.Name("foo"),
                    args=(
                        m.ZeroOrMore(m.Arg(m.OneOf(m.Integer("1"), m.Integer("2")))),
                        m.Arg(m.Integer("2")),
                        m.ZeroOrMore(),
                    ),
                ),
            )
        )
        # Match a function call to "foo" where the first argument is the integer
        # value 1, and the rest of the arguements are integers with the value
        # 2 or 3.
        self.assertTrue(
            matches(
                cst.Call(
                    func=cst.Name("foo"),
                    args=(
                        cst.Arg(cst.Integer("1")),
                        cst.Arg(cst.Integer("2")),
                        cst.Arg(cst.Integer("3")),
                    ),
                ),
                m.Call(
                    func=m.Name("foo"),
                    args=(
                        m.Arg(m.Integer("1")),
                        m.ZeroOrMore(m.Arg(m.OneOf(m.Integer("2"), m.Integer("3")))),
                    ),
                ),
            )
        )

    def test_zero_or_more_matcher_args_false(self) -> None:
        # Fail to match a function call to "foo" where the first argument is the
        # integer value 1, and the rest of the arguments are strings.
        self.assertFalse(
            matches(
                cst.Call(
                    func=cst.Name("foo"),
                    args=(
                        cst.Arg(cst.Integer("1")),
                        cst.Arg(cst.Integer("2")),
                        cst.Arg(cst.Integer("3")),
                    ),
                ),
                m.Call(
                    func=m.Name("foo"),
                    args=(m.Arg(m.Integer("1")), m.ZeroOrMore(m.Arg(m.SimpleString()))),
                ),
            )
        )
        # Fail to match a function call to "foo" where the first argument is the
        # integer value 1, and the rest of the arguements are integers with the
        # value 2.
        self.assertFalse(
            matches(
                cst.Call(
                    func=cst.Name("foo"),
                    args=(
                        cst.Arg(cst.Integer("1")),
                        cst.Arg(cst.Integer("2")),
                        cst.Arg(cst.Integer("3")),
                    ),
                ),
                m.Call(
                    func=m.Name("foo"),
                    args=(m.Arg(m.Integer("1")), m.ZeroOrMore(m.Arg(m.Integer("2")))),
                ),
            )
        )

    def test_at_least_n_matcher_args_true(self) -> None:
        # Match a function call to "foo" where the first argument is the integer
        # value 1, and there are at least two wildcard arguments after.
        self.assertTrue(
            matches(
                cst.Call(
                    func=cst.Name("foo"),
                    args=(
                        cst.Arg(cst.Integer("1")),
                        cst.Arg(cst.Integer("2")),
                        cst.Arg(cst.Integer("3")),
                    ),
                ),
                m.Call(
                    func=m.Name("foo"),
                    args=(m.Arg(m.Integer("1")), m.AtLeastN(m.Arg(), n=2)),
                ),
            )
        )
        # Match a function call to "foo" where the first argument is the integer
        # value 1, and there are at least two arguements are integers of any value
        # after.
        self.assertTrue(
            matches(
                cst.Call(
                    func=cst.Name("foo"),
                    args=(
                        cst.Arg(cst.Integer("1")),
                        cst.Arg(cst.Integer("2")),
                        cst.Arg(cst.Integer("3")),
                    ),
                ),
                m.Call(
                    func=m.Name("foo"),
                    args=(m.Arg(m.Integer("1")), m.AtLeastN(m.Arg(m.Integer()), n=2)),
                ),
            )
        )
        # Match a function call to "foo" where the first argument is the integer
        # value 1, and there are at least two arguements that are integers with the
        # value 2 or 3 after.
        self.assertTrue(
            matches(
                cst.Call(
                    func=cst.Name("foo"),
                    args=(
                        cst.Arg(cst.Integer("1")),
                        cst.Arg(cst.Integer("2")),
                        cst.Arg(cst.Integer("3")),
                    ),
                ),
                m.Call(
                    func=m.Name("foo"),
                    args=(
                        m.Arg(m.Integer("1")),
                        m.AtLeastN(m.Arg(m.OneOf(m.Integer("2"), m.Integer("3"))), n=2),
                    ),
                ),
            )
        )

    def test_at_least_n_matcher_args_false(self) -> None:
        # Fail to match a function call to "foo" where the first argument is the
        # integer value 1, and there are at least two arguments after that are
        # strings.
        self.assertFalse(
            matches(
                cst.Call(
                    func=cst.Name("foo"),
                    args=(
                        cst.Arg(cst.Integer("1")),
                        cst.Arg(cst.Integer("2")),
                        cst.Arg(cst.Integer("3")),
                    ),
                ),
                m.Call(
                    func=m.Name("foo"),
                    args=(
                        m.Arg(m.Integer("1")),
                        m.AtLeastN(m.Arg(m.SimpleString()), n=2),
                    ),
                ),
            )
        )
        # Fail to match a function call to "foo" where the first argument is the integer
        # value 1, and there are at least three wildcard arguments after.
        self.assertFalse(
            matches(
                cst.Call(
                    func=cst.Name("foo"),
                    args=(
                        cst.Arg(cst.Integer("1")),
                        cst.Arg(cst.Integer("2")),
                        cst.Arg(cst.Integer("3")),
                    ),
                ),
                m.Call(
                    func=m.Name("foo"),
                    args=(m.Arg(m.Integer("1")), m.AtLeastN(m.Arg(), n=3)),
                ),
            )
        )
        # Fail to match a function call to "foo" where the first argument is the
        # integer value 1, and there are at least two arguements that are integers with
        # the value 2 after.
        self.assertFalse(
            matches(
                cst.Call(
                    func=cst.Name("foo"),
                    args=(
                        cst.Arg(cst.Integer("1")),
                        cst.Arg(cst.Integer("2")),
                        cst.Arg(cst.Integer("3")),
                    ),
                ),
                m.Call(
                    func=m.Name("foo"),
                    args=(
                        m.Arg(m.Integer("1")),
                        m.AtLeastN(m.Arg(m.Integer("2")), n=2),
                    ),
                ),
            )
        )

    def test_at_most_n_matcher_no_args_true(self) -> None:
        # Match a function call to "foo" with at most two arguments.
        self.assertTrue(
            matches(
                cst.Call(func=cst.Name("foo"), args=(cst.Arg(cst.Integer("1")),)),
                m.Call(func=m.Name("foo"), args=(m.AtMostN(n=2),)),
            )
        )
        # Match a function call to "foo" with at most two arguments.
        self.assertTrue(
            matches(
                cst.Call(
                    func=cst.Name("foo"),
                    args=(cst.Arg(cst.Integer("1")), cst.Arg(cst.Integer("2"))),
                ),
                m.Call(func=m.Name("foo"), args=(m.AtMostN(n=2),)),
            )
        )
        # Match a function call to "foo" with at most six arguments, the last
        # one being the integer 1.
        self.assertTrue(
            matches(
                cst.Call(func=cst.Name("foo"), args=(cst.Arg(cst.Integer("1")),)),
                m.Call(
                    func=m.Name("foo"), args=[m.AtMostN(n=5), m.Arg(m.Integer("1"))]
                ),
            )
        )
        # Match a function call to "foo" with at most six arguments, the last
        # one being the integer 1.
        self.assertTrue(
            matches(
                cst.Call(
                    func=cst.Name("foo"),
                    args=(cst.Arg(cst.Integer("1")), cst.Arg(cst.Integer("2"))),
                ),
                m.Call(
                    func=m.Name("foo"), args=(m.AtMostN(n=5), m.Arg(m.Integer("2")))
                ),
            )
        )
        # Match a function call to "foo" with at most six arguments, the first
        # one being the integer 1.
        self.assertTrue(
            matches(
                cst.Call(
                    func=cst.Name("foo"),
                    args=(cst.Arg(cst.Integer("1")), cst.Arg(cst.Integer("2"))),
                ),
                m.Call(
                    func=m.Name("foo"), args=(m.Arg(m.Integer("1")), m.AtMostN(n=5))
                ),
            )
        )
        # Match a function call to "foo" with at most six arguments, the first
        # one being the integer 1.
        self.assertTrue(
            matches(
                cst.Call(
                    func=cst.Name("foo"),
                    args=(cst.Arg(cst.Integer("1")), cst.Arg(cst.Integer("2"))),
                ),
                m.Call(func=m.Name("foo"), args=(m.Arg(m.Integer("1")), m.ZeroOrOne())),
            )
        )

    def test_at_most_n_matcher_no_args_false(self) -> None:
        # Fail to match a function call to "foo" with at most two arguments.
        self.assertFalse(
            matches(
                cst.Call(
                    func=cst.Name("foo"),
                    args=(
                        cst.Arg(cst.Integer("1")),
                        cst.Arg(cst.Integer("2")),
                        cst.Arg(cst.Integer("3")),
                    ),
                ),
                m.Call(func=m.Name("foo"), args=(m.AtMostN(n=2),)),
            )
        )
        # Fail to match a function call to "foo" with at most two arguments,
        # the last one being the integer 3.
        self.assertFalse(
            matches(
                cst.Call(
                    func=cst.Name("foo"),
                    args=(
                        cst.Arg(cst.Integer("1")),
                        cst.Arg(cst.Integer("2")),
                        cst.Arg(cst.Integer("3")),
                    ),
                ),
                m.Call(
                    func=m.Name("foo"), args=(m.AtMostN(n=1), m.Arg(m.Integer("3")))
                ),
            )
        )
        # Fail to match a function call to "foo" with at most two arguments,
        # the last one being the integer 3.
        self.assertFalse(
            matches(
                cst.Call(
                    func=cst.Name("foo"),
                    args=(
                        cst.Arg(cst.Integer("1")),
                        cst.Arg(cst.Integer("2")),
                        cst.Arg(cst.Integer("3")),
                    ),
                ),
                m.Call(func=m.Name("foo"), args=(m.ZeroOrOne(), m.Arg(m.Integer("3")))),
            )
        )

    def test_at_most_n_matcher_args_true(self) -> None:
        # Match a function call to "foo" with at most two arguments, both of which
        # are the integer 1.
        self.assertTrue(
            matches(
                cst.Call(func=cst.Name("foo"), args=(cst.Arg(cst.Integer("1")),)),
                m.Call(
                    func=m.Name("foo"), args=(m.AtMostN(m.Arg(m.Integer("1")), n=2),)
                ),
            )
        )
        # Match a function call to "foo" with at most two arguments, both of which
        # can be the integer 1 or 2.
        self.assertTrue(
            matches(
                cst.Call(
                    func=cst.Name("foo"),
                    args=(cst.Arg(cst.Integer("1")), cst.Arg(cst.Integer("2"))),
                ),
                m.Call(
                    func=m.Name("foo"),
                    args=(
                        m.AtMostN(m.Arg(m.OneOf(m.Integer("1"), m.Integer("2"))), n=2),
                    ),
                ),
            )
        )
        # Match a function call to "foo" with at most two arguments, the first
        # one being the integer 1 and the second one, if included, being the
        # integer 2.
        self.assertTrue(
            matches(
                cst.Call(
                    func=cst.Name("foo"),
                    args=(cst.Arg(cst.Integer("1")), cst.Arg(cst.Integer("2"))),
                ),
                m.Call(
                    func=m.Name("foo"),
                    args=(m.Arg(m.Integer("1")), m.ZeroOrOne(m.Arg(m.Integer("2")))),
                ),
            )
        )
        # Match a function call to "foo" with at most six arguments, the first
        # one being the integer 1 and the second one, if included, being the
        # integer 2.
        self.assertTrue(
            matches(
                cst.Call(
                    func=cst.Name("foo"),
                    args=(cst.Arg(cst.Integer("1")), cst.Arg(cst.Integer("2"))),
                ),
                m.Call(
                    func=m.Name("foo"),
                    args=(m.Arg(m.Integer("1")), m.ZeroOrOne(m.Arg(m.Integer("2")))),
                ),
            )
        )

    def test_at_most_n_matcher_args_false(self) -> None:
        # Fail to match a function call to "foo" with at most three arguments,
        # all of which are the integer 4.
        self.assertFalse(
            matches(
                cst.Call(
                    func=cst.Name("foo"),
                    args=(
                        cst.Arg(cst.Integer("1")),
                        cst.Arg(cst.Integer("2")),
                        cst.Arg(cst.Integer("3")),
                    ),
                ),
                m.Call(
                    func=m.Name("foo"), args=(m.AtMostN(m.Arg(m.Integer("4")), n=3),)
                ),
            )
        )

    def test_lambda_matcher_true(self) -> None:
        # Match based on identical attributes.
        self.assertTrue(
            matches(
                cst.Name("foo"), m.Name(value=m.MatchIfTrue(lambda value: "o" in value))
            )
        )

    def test_lambda_matcher_false(self) -> None:
        # Fail to match due to incorrect value on Name.
        self.assertFalse(
            matches(
                cst.Name("foo"), m.Name(value=m.MatchIfTrue(lambda value: "a" in value))
            )
        )

    def test_regex_matcher_true(self) -> None:
        # Match based on identical attributes.
        self.assertTrue(matches(cst.Name("foo"), m.Name(value=m.MatchRegex(r".*o.*"))))

    def test_regex_matcher_false(self) -> None:
        # Fail to match due to incorrect value on Name.
        self.assertFalse(matches(cst.Name("foo"), m.Name(value=m.MatchRegex(r".*a.*"))))

    def test_and_matcher_true(self) -> None:
        # Match on True identifier in roundabout way.
        self.assertTrue(
            matches(
                cst.Name("True"), m.AllOf(m.Name(), m.Name(value=m.MatchRegex(r"True")))
            )
        )
        self.assertTrue(
            matches(
                cst.Call(
                    func=cst.Name("foo"),
                    args=(
                        cst.Arg(cst.Integer("1")),
                        cst.Arg(cst.Integer("2")),
                        cst.Arg(cst.Integer("3")),
                    ),
                ),
                m.Call(
                    func=m.Name("foo"),
                    args=m.AllOf(
                        (m.Arg(), m.Arg(), m.Arg()),
                        (
                            m.Arg(m.Integer("1")),
                            m.Arg(m.Integer("2")),
                            m.Arg(m.Integer("3")),
                        ),
                    ),
                ),
            )
        )

    def test_and_matcher_false(self) -> None:
        # Fail to match since True and False cannot match.
        self.assertFalse(
            matches(cst.Name("None"), m.AllOf(m.Name("True"), m.Name("False")))
        )
        self.assertFalse(
            matches(
                cst.Call(
                    func=cst.Name("foo"),
                    args=(
                        cst.Arg(cst.Integer("1")),
                        cst.Arg(cst.Integer("2")),
                        cst.Arg(cst.Integer("3")),
                    ),
                ),
                m.Call(
                    func=m.Name("foo"),
                    args=m.AllOf(
                        (m.Arg(), m.Arg(), m.Arg()),
                        (
                            m.Arg(m.Integer("3")),
                            m.Arg(m.Integer("2")),
                            m.Arg(m.Integer("1")),
                        ),
                    ),
                ),
            )
        )

    def test_and_operator_matcher_true(self) -> None:
        # Match on True identifier in roundabout way.
        self.assertTrue(
            matches(cst.Name("True"), m.Name() & m.Name(value=m.MatchRegex(r"True")))
        )
        # Match in a really roundabout way that verifies the __or__ behavior on
        # AllOf itself.
        self.assertTrue(
            matches(
                cst.Name("True"),
                m.Name() & m.Name(value=m.MatchRegex(r"True")) & m.Name("True"),
            )
        )
        # Verify that MatchIfTrue works with __and__ behavior properly.
        self.assertTrue(
            matches(
                cst.Name("True"),
                m.MatchIfTrue(lambda x: isinstance(x, cst.Name))
                & m.Name(value=m.MatchRegex(r"True")),
            )
        )
        self.assertTrue(
            matches(
                cst.Name("True"),
                m.Name(value=m.MatchRegex(r"True"))
                & m.MatchIfTrue(lambda x: isinstance(x, cst.Name)),
            )
        )

    def test_and_operator_matcher_false(self) -> None:
        # Fail to match since True and False cannot match.
        self.assertFalse(matches(cst.Name("None"), m.Name("True") & m.Name("False")))

    def test_does_not_match_true(self) -> None:
        # Match on any call that takes one argument that isn't the value None.
        self.assertTrue(
            matches(
                cst.Call(func=cst.Name("foo"), args=(cst.Arg(cst.Name("True")),)),
                m.Call(args=(m.Arg(value=m.DoesNotMatch(m.Name("None"))),)),
            )
        )
        self.assertTrue(
            matches(
                cst.Call(func=cst.Name("foo"), args=(cst.Arg(cst.Integer("1")),)),
                m.Call(args=(m.DoesNotMatch(m.Arg(m.Name("None"))),)),
            )
        )
        self.assertTrue(
            matches(
                cst.Call(func=cst.Name("foo"), args=(cst.Arg(cst.Integer("1")),)),
                m.Call(args=m.DoesNotMatch((m.Arg(m.Integer("2")),))),
            )
        )
        # Match any call that takes an argument which isn't True or False.
        self.assertTrue(
            matches(
                cst.Call(func=cst.Name("foo"), args=(cst.Arg(cst.Integer("1")),)),
                m.Call(
                    args=(
                        m.Arg(
                            value=m.DoesNotMatch(
                                m.OneOf(m.Name("True"), m.Name("False"))
                            )
                        ),
                    )
                ),
            )
        )
        # Match any name node that doesn't match the regex for True
        self.assertTrue(
            matches(
                cst.Name("False"), m.Name(value=m.DoesNotMatch(m.MatchRegex(r"True")))
            )
        )

    def test_does_not_match_operator_true(self) -> None:
        # Match on any call that takes one argument that isn't the value None.
        self.assertTrue(
            matches(
                cst.Call(func=cst.Name("foo"), args=(cst.Arg(cst.Name("True")),)),
                m.Call(args=(m.Arg(value=~(m.Name("None"))),)),
            )
        )
        self.assertTrue(
            matches(
                cst.Call(func=cst.Name("foo"), args=(cst.Arg(cst.Integer("1")),)),
                m.Call(args=(~(m.Arg(m.Name("None"))),)),
            )
        )
        # Match any call that takes an argument which isn't True or False.
        self.assertTrue(
            matches(
                cst.Call(func=cst.Name("foo"), args=(cst.Arg(cst.Integer("1")),)),
                m.Call(args=(m.Arg(value=~(m.Name("True") | m.Name("False"))),)),
            )
        )
        self.assertTrue(
            matches(
                cst.Call(func=cst.Name("foo"), args=(cst.Arg(cst.Name("None")),)),
                m.Call(args=(m.Arg(value=(~(m.Name("True"))) & (~(m.Name("False")))),)),
            )
        )
        # Roundabout way to verify that or operator works with inverted nodes.
        self.assertTrue(
            matches(
                cst.Call(func=cst.Name("foo"), args=(cst.Arg(cst.Name("False")),)),
                m.Call(args=(m.Arg(value=(~(m.Name("True"))) | (~(m.Name("True")))),)),
            )
        )
        # Roundabout way to verify that inverse operator works properly on AllOf.
        self.assertTrue(
            matches(
                cst.Call(func=cst.Name("foo"), args=(cst.Arg(cst.Integer("1")),)),
                m.Call(args=(m.Arg(value=~(m.Name() & m.Name("True"))),)),
            )
        )
        # Match any name node that doesn't match the regex for True
        self.assertTrue(
            matches(cst.Name("False"), m.Name(value=~(m.MatchRegex(r"True"))))
        )

    def test_does_not_match_false(self) -> None:
        # Match on any call that takes one argument that isn't the value None.
        self.assertFalse(
            matches(
                cst.Call(func=cst.Name("foo"), args=(cst.Arg(cst.Name("None")),)),
                m.Call(args=(m.Arg(value=m.DoesNotMatch(m.Name("None"))),)),
            )
        )
        self.assertFalse(
            matches(
                cst.Call(func=cst.Name("foo"), args=(cst.Arg(cst.Integer("1")),)),
                m.Call(args=(m.DoesNotMatch(m.Arg(m.Integer("1"))),)),
            )
        )
        self.assertFalse(
            matches(
                cst.Call(func=cst.Name("foo"), args=(cst.Arg(cst.Integer("1")),)),
                m.Call(args=m.DoesNotMatch((m.Arg(m.Integer("1")),))),
            )
        )
        # Match any call that takes an argument which isn't True or False.
        self.assertFalse(
            matches(
                cst.Call(func=cst.Name("foo"), args=(cst.Arg(cst.Name("False")),)),
                m.Call(
                    args=(
                        m.Arg(
                            value=m.DoesNotMatch(
                                m.OneOf(m.Name("True"), m.Name("False"))
                            )
                        ),
                    )
                ),
            )
        )
        self.assertFalse(
            matches(
                cst.Call(func=cst.Name("foo"), args=(cst.Arg(cst.Name("True")),)),
                m.Call(args=(m.Arg(value=(~(m.Name("True"))) & (~(m.Name("False")))),)),
            )
        )
        # Match any name node that doesn't match the regex for True
        self.assertFalse(
            matches(
                cst.Name("True"), m.Name(value=m.DoesNotMatch(m.MatchRegex(r"True")))
            )
        )

    def test_does_not_match_operator_false(self) -> None:
        # Match on any call that takes one argument that isn't the value None.
        self.assertFalse(
            matches(
                cst.Call(func=cst.Name("foo"), args=(cst.Arg(cst.Name("None")),)),
                m.Call(args=(m.Arg(value=~(m.Name("None"))),)),
            )
        )
        self.assertFalse(
            matches(
                cst.Call(func=cst.Name("foo"), args=(cst.Arg(cst.Integer("1")),)),
                m.Call(args=((~(m.Arg(m.Integer("1")))),)),
            )
        )
        # Match any call that takes an argument which isn't True or False.
        self.assertFalse(
            matches(
                cst.Call(func=cst.Name("foo"), args=(cst.Arg(cst.Name("False")),)),
                m.Call(args=(m.Arg(value=~(m.Name("True") | m.Name("False"))),)),
            )
        )
        # Roundabout way of verifying ~(x&y) behavior.
        self.assertFalse(
            matches(
                cst.Call(func=cst.Name("foo"), args=(cst.Arg(cst.Name("False")),)),
                m.Call(args=(m.Arg(value=~(m.Name() & m.Name("False"))),)),
            )
        )
        # Roundabout way of verifying (~x)|(~y) behavior
        self.assertFalse(
            matches(
                cst.Call(func=cst.Name("foo"), args=(cst.Arg(cst.Name("True")),)),
                m.Call(args=(m.Arg(value=(~(m.Name("True"))) | (~(m.Name("True")))),)),
            )
        )
        # Match any name node that doesn't match the regex for True
        self.assertFalse(
            matches(cst.Name("True"), m.Name(value=~(m.MatchRegex(r"True"))))
        )

    def test_inverse_inverse_is_identity(self) -> None:
        # Verify that we don't wrap an InverseOf in an InverseOf in normal circumstances.
        identity = m.Name("True")
        self.assertTrue(m.DoesNotMatch(m.DoesNotMatch(identity)) is identity)
        self.assertTrue((~(~identity)) is identity)
