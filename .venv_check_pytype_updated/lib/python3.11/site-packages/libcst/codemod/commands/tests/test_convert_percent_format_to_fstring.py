# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.
#
from libcst.codemod import CodemodTest
from libcst.codemod.commands.convert_percent_format_to_fstring import (
    ConvertPercentFormatStringCommand,
)


class ConvertPercentFormatStringCommandTest(CodemodTest):
    TRANSFORM = ConvertPercentFormatStringCommand

    def test_simple_cases(self) -> None:
        self.assertCodemod('"a name: %s" % name', 'f"a name: {name}"')
        self.assertCodemod(
            '"an attribute %s ." % obj.attr', 'f"an attribute {obj.attr} ."'
        )
        self.assertCodemod('r"raw string value=%s" % val', 'fr"raw string value={val}"')
        self.assertCodemod(
            '"The type of var: %s" % type(var)', 'f"The type of var: {type(var)}"'
        )
        self.assertCodemod(
            '"type of var: %s, value of var: %s" % (type(var), var)',
            'f"type of var: {type(var)}, value of var: {var}"',
        )
        self.assertCodemod(
            '"var1: %s, var2: %s, var3: %s, var4: %s" % (class_object.attribute, dict_lookup["some_key"], some_module.some_function(), var4)',
            '''f"var1: {class_object.attribute}, var2: {dict_lookup['some_key']}, var3: {some_module.some_function()}, var4: {var4}"''',
        )

    def test_escaping(self) -> None:
        self.assertCodemod('"%s" % "hi"', '''f"{'hi'}"''')  # escape quote
        self.assertCodemod('"{%s}" % val', 'f"{{{val}}}"')  # escape curly bracket
        self.assertCodemod('"{%s" % val', 'f"{{{val}"')  # escape curly bracket
        self.assertCodemod(
            "'%s\" double quote is used' % var", "f'{var}\" double quote is used'"
        )  # escape quote
        self.assertCodemod(
            '"a list: %s" % " ".join(var)', '''f"a list: {' '.join(var)}"'''
        )  # escape quote

    def test_not_supported_case(self) -> None:
        code = '"%s" % obj.this_is_a_very_long_expression(parameter)["a_very_long_key"]'
        self.assertCodemod(code, code)
        code = 'b"a type %s" % var'
        self.assertCodemod(code, code)
