# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from textwrap import dedent

from libcst import parse_module
from libcst.testing.utils import UnitTest
from libcst.tool import dump


class CSTDumpTextTest(UnitTest):
    def test_full_tree(self) -> None:
        module = r"""
            Module(
              body=[
                FunctionDef(
                  name=Name(
                    value='foo',
                    lpar=[],
                    rpar=[],
                  ),
                  params=Parameters(
                    params=[
                      Param(
                        name=Name(
                          value='a',
                          lpar=[],
                          rpar=[],
                        ),
                        annotation=Annotation(
                          annotation=Name(
                            value='str',
                            lpar=[],
                            rpar=[],
                          ),
                          whitespace_before_indicator=SimpleWhitespace(
                            value='',
                          ),
                          whitespace_after_indicator=SimpleWhitespace(
                            value=' ',
                          ),
                        ),
                        equal=MaybeSentinel.DEFAULT,
                        default=None,
                        comma=MaybeSentinel.DEFAULT,
                        star='',
                        whitespace_after_star=SimpleWhitespace(
                          value='',
                        ),
                        whitespace_after_param=SimpleWhitespace(
                          value='',
                        ),
                      ),
                    ],
                    star_arg=MaybeSentinel.DEFAULT,
                    kwonly_params=[],
                    star_kwarg=None,
                    posonly_params=[],
                    posonly_ind=MaybeSentinel.DEFAULT,
                  ),
                  body=IndentedBlock(
                    body=[
                      SimpleStatementLine(
                        body=[
                          Pass(
                            semicolon=Semicolon(
                              whitespace_before=SimpleWhitespace(
                                value=' ',
                              ),
                              whitespace_after=SimpleWhitespace(
                                value=' ',
                              ),
                            ),
                          ),
                          Pass(
                            semicolon=MaybeSentinel.DEFAULT,
                          ),
                        ],
                        leading_lines=[],
                        trailing_whitespace=TrailingWhitespace(
                          whitespace=SimpleWhitespace(
                            value='',
                          ),
                          comment=None,
                          newline=Newline(
                            value=None,
                          ),
                        ),
                      ),
                      SimpleStatementLine(
                        body=[
                          Return(
                            value=None,
                            whitespace_after_return=SimpleWhitespace(
                              value='',
                            ),
                            semicolon=MaybeSentinel.DEFAULT,
                          ),
                        ],
                        leading_lines=[],
                        trailing_whitespace=TrailingWhitespace(
                          whitespace=SimpleWhitespace(
                            value='',
                          ),
                          comment=None,
                          newline=Newline(
                            value=None,
                          ),
                        ),
                      ),
                    ],
                    header=TrailingWhitespace(
                      whitespace=SimpleWhitespace(
                        value='',
                      ),
                      comment=None,
                      newline=Newline(
                        value=None,
                      ),
                    ),
                    indent=None,
                    footer=[],
                  ),
                  decorators=[],
                  returns=Annotation(
                    annotation=Name(
                      value='None',
                      lpar=[],
                      rpar=[],
                    ),
                    whitespace_before_indicator=SimpleWhitespace(
                      value=' ',
                    ),
                    whitespace_after_indicator=SimpleWhitespace(
                      value=' ',
                    ),
                  ),
                  asynchronous=None,
                  leading_lines=[],
                  lines_after_decorators=[],
                  whitespace_after_def=SimpleWhitespace(
                    value=' ',
                  ),
                  whitespace_after_name=SimpleWhitespace(
                    value='',
                  ),
                  whitespace_before_params=SimpleWhitespace(
                    value='',
                  ),
                  whitespace_before_colon=SimpleWhitespace(
                    value='',
                  ),
                  type_parameters=None,
                  whitespace_after_type_parameters=SimpleWhitespace(
                    value='',
                  ),
                ),
              ],
              header=[],
              footer=[],
              encoding='utf-8',
              default_indent='    ',
              default_newline='\n',
              has_trailing_newline=True,
            )
        """

        # Compare against a known string representation, as unmangled from
        # python indent.
        self.assertEqual(
            dedent(module[1:])[:-1],
            dump(
                parse_module("def foo(a: str) -> None:\n    pass ; pass\n    return\n"),
                show_whitespace=True,
                show_defaults=True,
                show_syntax=True,
            ),
        )

    def test_hidden_whitespace(self) -> None:
        module = r"""
            Module(
              body=[
                FunctionDef(
                  name=Name(
                    value='foo',
                    lpar=[],
                    rpar=[],
                  ),
                  params=Parameters(
                    params=[
                      Param(
                        name=Name(
                          value='a',
                          lpar=[],
                          rpar=[],
                        ),
                        annotation=Annotation(
                          annotation=Name(
                            value='str',
                            lpar=[],
                            rpar=[],
                          ),
                        ),
                        equal=MaybeSentinel.DEFAULT,
                        default=None,
                        comma=MaybeSentinel.DEFAULT,
                        star='',
                      ),
                    ],
                    star_arg=MaybeSentinel.DEFAULT,
                    kwonly_params=[],
                    star_kwarg=None,
                    posonly_params=[],
                    posonly_ind=MaybeSentinel.DEFAULT,
                  ),
                  body=IndentedBlock(
                    body=[
                      SimpleStatementLine(
                        body=[
                          Pass(
                            semicolon=Semicolon(),
                          ),
                          Pass(
                            semicolon=MaybeSentinel.DEFAULT,
                          ),
                        ],
                      ),
                      SimpleStatementLine(
                        body=[
                          Return(
                            value=None,
                            semicolon=MaybeSentinel.DEFAULT,
                          ),
                        ],
                      ),
                    ],
                  ),
                  decorators=[],
                  returns=Annotation(
                    annotation=Name(
                      value='None',
                      lpar=[],
                      rpar=[],
                    ),
                  ),
                  asynchronous=None,
                  type_parameters=None,
                ),
              ],
              encoding='utf-8',
              default_indent='    ',
              default_newline='\n',
              has_trailing_newline=True,
            )
        """

        # Compare against a known string representation, as unmangled from
        # python indent.
        self.assertEqual(
            dedent(module[1:])[:-1],
            dump(
                parse_module("def foo(a: str) -> None:\n    pass ; pass\n    return\n"),
                show_whitespace=False,
                show_defaults=True,
                show_syntax=True,
            ),
        )

    def test_hidden_defaults(self) -> None:
        module = r"""
            Module(
              body=[
                FunctionDef(
                  name=Name(
                    value='foo',
                  ),
                  params=Parameters(
                    params=[
                      Param(
                        name=Name(
                          value='a',
                        ),
                        annotation=Annotation(
                          annotation=Name(
                            value='str',
                          ),
                          whitespace_before_indicator=SimpleWhitespace(
                            value='',
                          ),
                        ),
                        star='',
                      ),
                    ],
                  ),
                  body=IndentedBlock(
                    body=[
                      SimpleStatementLine(
                        body=[
                          Pass(
                            semicolon=Semicolon(
                              whitespace_before=SimpleWhitespace(
                                value=' ',
                              ),
                              whitespace_after=SimpleWhitespace(
                                value=' ',
                              ),
                            ),
                          ),
                          Pass(),
                        ],
                      ),
                      SimpleStatementLine(
                        body=[
                          Return(
                            whitespace_after_return=SimpleWhitespace(
                              value='',
                            ),
                          ),
                        ],
                      ),
                    ],
                  ),
                  returns=Annotation(
                    annotation=Name(
                      value='None',
                    ),
                    whitespace_before_indicator=SimpleWhitespace(
                      value=' ',
                    ),
                  ),
                ),
              ],
            )
        """

        # Compare against a known string representation, as unmangled from
        # python indent.
        self.assertEqual(
            dedent(module[1:])[:-1],
            dump(
                parse_module("def foo(a: str) -> None:\n    pass ; pass\n    return\n"),
                show_whitespace=True,
                show_defaults=False,
                show_syntax=True,
            ),
        )

    def test_hidden_whitespace_and_defaults(self) -> None:
        module = r"""
            Module(
              body=[
                FunctionDef(
                  name=Name(
                    value='foo',
                  ),
                  params=Parameters(
                    params=[
                      Param(
                        name=Name(
                          value='a',
                        ),
                        annotation=Annotation(
                          annotation=Name(
                            value='str',
                          ),
                        ),
                        star='',
                      ),
                    ],
                  ),
                  body=IndentedBlock(
                    body=[
                      SimpleStatementLine(
                        body=[
                          Pass(
                            semicolon=Semicolon(),
                          ),
                          Pass(),
                        ],
                      ),
                      SimpleStatementLine(
                        body=[
                          Return(),
                        ],
                      ),
                    ],
                  ),
                  returns=Annotation(
                    annotation=Name(
                      value='None',
                    ),
                  ),
                ),
              ],
            )
        """

        # Compare against a known string representation, as unmangled from
        # python indent.
        self.assertEqual(
            dedent(module[1:])[:-1],
            dump(
                parse_module("def foo(a: str) -> None:\n    pass ; pass\n    return\n"),
                show_whitespace=False,
                show_defaults=False,
                show_syntax=True,
            ),
        )

    def test_hidden_syntax(self) -> None:
        module = r"""
            Module(
              body=[
                FunctionDef(
                  name=Name(
                    value='foo',
                    lpar=[],
                    rpar=[],
                  ),
                  params=Parameters(
                    params=[
                      Param(
                        name=Name(
                          value='a',
                          lpar=[],
                          rpar=[],
                        ),
                        annotation=Annotation(
                          annotation=Name(
                            value='str',
                            lpar=[],
                            rpar=[],
                          ),
                          whitespace_before_indicator=SimpleWhitespace(
                            value='',
                          ),
                          whitespace_after_indicator=SimpleWhitespace(
                            value=' ',
                          ),
                        ),
                        default=None,
                        star='',
                        whitespace_after_star=SimpleWhitespace(
                          value='',
                        ),
                        whitespace_after_param=SimpleWhitespace(
                          value='',
                        ),
                      ),
                    ],
                    star_arg=MaybeSentinel.DEFAULT,
                    kwonly_params=[],
                    star_kwarg=None,
                    posonly_params=[],
                    posonly_ind=MaybeSentinel.DEFAULT,
                  ),
                  body=IndentedBlock(
                    body=[
                      SimpleStatementLine(
                        body=[
                          Pass(),
                          Pass(),
                        ],
                        leading_lines=[],
                        trailing_whitespace=TrailingWhitespace(
                          whitespace=SimpleWhitespace(
                            value='',
                          ),
                          comment=None,
                          newline=Newline(
                            value=None,
                          ),
                        ),
                      ),
                      SimpleStatementLine(
                        body=[
                          Return(
                            value=None,
                            whitespace_after_return=SimpleWhitespace(
                              value='',
                            ),
                          ),
                        ],
                        leading_lines=[],
                        trailing_whitespace=TrailingWhitespace(
                          whitespace=SimpleWhitespace(
                            value='',
                          ),
                          comment=None,
                          newline=Newline(
                            value=None,
                          ),
                        ),
                      ),
                    ],
                    header=TrailingWhitespace(
                      whitespace=SimpleWhitespace(
                        value='',
                      ),
                      comment=None,
                      newline=Newline(
                        value=None,
                      ),
                    ),
                    indent=None,
                    footer=[],
                  ),
                  decorators=[],
                  returns=Annotation(
                    annotation=Name(
                      value='None',
                      lpar=[],
                      rpar=[],
                    ),
                    whitespace_before_indicator=SimpleWhitespace(
                      value=' ',
                    ),
                    whitespace_after_indicator=SimpleWhitespace(
                      value=' ',
                    ),
                  ),
                  asynchronous=None,
                  leading_lines=[],
                  lines_after_decorators=[],
                  whitespace_after_def=SimpleWhitespace(
                    value=' ',
                  ),
                  whitespace_after_name=SimpleWhitespace(
                    value='',
                  ),
                  whitespace_before_params=SimpleWhitespace(
                    value='',
                  ),
                  whitespace_before_colon=SimpleWhitespace(
                    value='',
                  ),
                  type_parameters=None,
                  whitespace_after_type_parameters=SimpleWhitespace(
                    value='',
                  ),
                ),
              ],
              header=[],
              footer=[],
            )
        """

        # Compare against a known string representation, as unmangled from
        # python indent.
        self.assertEqual(
            dedent(module[1:])[:-1],
            dump(
                parse_module("def foo(a: str) -> None:\n    pass ; pass\n    return\n"),
                show_whitespace=True,
                show_defaults=True,
                show_syntax=False,
            ),
        )

    def test_hidden_whitespace_and_syntax(self) -> None:
        module = r"""
            Module(
              body=[
                FunctionDef(
                  name=Name(
                    value='foo',
                    lpar=[],
                    rpar=[],
                  ),
                  params=Parameters(
                    params=[
                      Param(
                        name=Name(
                          value='a',
                          lpar=[],
                          rpar=[],
                        ),
                        annotation=Annotation(
                          annotation=Name(
                            value='str',
                            lpar=[],
                            rpar=[],
                          ),
                        ),
                        default=None,
                        star='',
                      ),
                    ],
                    star_arg=MaybeSentinel.DEFAULT,
                    kwonly_params=[],
                    star_kwarg=None,
                    posonly_params=[],
                    posonly_ind=MaybeSentinel.DEFAULT,
                  ),
                  body=IndentedBlock(
                    body=[
                      SimpleStatementLine(
                        body=[
                          Pass(),
                          Pass(),
                        ],
                      ),
                      SimpleStatementLine(
                        body=[
                          Return(
                            value=None,
                          ),
                        ],
                      ),
                    ],
                  ),
                  decorators=[],
                  returns=Annotation(
                    annotation=Name(
                      value='None',
                      lpar=[],
                      rpar=[],
                    ),
                  ),
                  asynchronous=None,
                  type_parameters=None,
                ),
              ],
            )
        """

        # Compare against a known string representation, as unmangled from
        # python indent.
        self.assertEqual(
            dedent(module[1:])[:-1],
            dump(
                parse_module("def foo(a: str) -> None:\n    pass ; pass\n    return\n"),
                show_whitespace=False,
                show_defaults=True,
                show_syntax=False,
            ),
        )

    def test_hidden_defaults_and_syntax(self) -> None:
        module = r"""
            Module(
              body=[
                FunctionDef(
                  name=Name(
                    value='foo',
                  ),
                  params=Parameters(
                    params=[
                      Param(
                        name=Name(
                          value='a',
                        ),
                        annotation=Annotation(
                          annotation=Name(
                            value='str',
                          ),
                          whitespace_before_indicator=SimpleWhitespace(
                            value='',
                          ),
                        ),
                        star='',
                      ),
                    ],
                  ),
                  body=IndentedBlock(
                    body=[
                      SimpleStatementLine(
                        body=[
                          Pass(),
                          Pass(),
                        ],
                      ),
                      SimpleStatementLine(
                        body=[
                          Return(
                            whitespace_after_return=SimpleWhitespace(
                              value='',
                            ),
                          ),
                        ],
                      ),
                    ],
                  ),
                  returns=Annotation(
                    annotation=Name(
                      value='None',
                    ),
                    whitespace_before_indicator=SimpleWhitespace(
                      value=' ',
                    ),
                  ),
                ),
              ],
            )
        """

        # Compare against a known string representation, as unmangled from
        # python indent.
        self.assertEqual(
            dedent(module[1:])[:-1],
            dump(
                parse_module("def foo(a: str) -> None:\n    pass ; pass\n    return\n"),
                show_whitespace=True,
                show_defaults=False,
                show_syntax=False,
            ),
        )

    def test_hidden_whitespace_and_defaults_and_syntax(self) -> None:
        module = r"""
            Module(
              body=[
                FunctionDef(
                  name=Name(
                    value='foo',
                  ),
                  params=Parameters(
                    params=[
                      Param(
                        name=Name(
                          value='a',
                        ),
                        annotation=Annotation(
                          annotation=Name(
                            value='str',
                          ),
                        ),
                        star='',
                      ),
                    ],
                  ),
                  body=IndentedBlock(
                    body=[
                      SimpleStatementLine(
                        body=[
                          Pass(),
                          Pass(),
                        ],
                      ),
                      SimpleStatementLine(
                        body=[
                          Return(),
                        ],
                      ),
                    ],
                  ),
                  returns=Annotation(
                    annotation=Name(
                      value='None',
                    ),
                  ),
                ),
              ],
            )
        """

        # Compare against a known string representation, as unmangled from
        # python indent.
        self.assertEqual(
            dedent(module[1:])[:-1],
            dump(
                parse_module("def foo(a: str) -> None:\n    pass ; pass\n    return\n"),
                show_whitespace=False,
                show_defaults=False,
                show_syntax=False,
            ),
        )
