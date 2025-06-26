# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.
#
import libcst
import libcst.matchers as m
from libcst.codemod import VisitorBasedCodemodCommand


class UnnecessaryFormatString(VisitorBasedCodemodCommand):
    DESCRIPTION: str = (
        "Converts f-strings which perform no formatting to regular strings."
    )

    @m.leave(m.FormattedString(parts=(m.FormattedStringText(),)))
    def _check_formatted_string(
        self,
        _original_node: libcst.FormattedString,
        updated_node: libcst.FormattedString,
    ) -> libcst.BaseExpression:
        old_string_inner = libcst.ensure_type(
            updated_node.parts[0], libcst.FormattedStringText
        ).value
        if "{{" in old_string_inner or "}}" in old_string_inner:
            # there are only two characters we need to worry about escaping.
            return updated_node

        old_string_literal = updated_node.start + old_string_inner + updated_node.end
        new_string_literal = (
            updated_node.start.replace("f", "").replace("F", "")
            + old_string_inner
            + updated_node.end
        )

        old_string_evaled = eval(old_string_literal)  # noqa
        new_string_evaled = eval(new_string_literal)  # noqa
        if old_string_evaled != new_string_evaled:
            warn_message = (
                f"Attempted to codemod |{old_string_literal}| to "
                + f"|{new_string_literal}| but don't eval to the same! First is |{old_string_evaled}| and "
                + f"second is |{new_string_evaled}|"
            )
            self.warn(warn_message)
            return updated_node

        return libcst.SimpleString(new_string_literal)
