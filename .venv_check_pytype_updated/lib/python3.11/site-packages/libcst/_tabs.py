# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.


def expand_tabs(line: str) -> str:
    """
    Tabs are treated as 1-8 spaces according to
    https://docs.python.org/3/reference/lexical_analysis.html#indentation

    Given a string with tabs, this removes all tab characters and replaces them with the
    appropriate number of spaces.
    """
    result_list = []
    total = 0
    for ch in line:
        if ch == "\t":
            prev_total = total
            total = ((total + 8) // 8) * 8
            result_list.append(" " * (total - prev_total))
        else:
            total += 1
            result_list.append(ch)

    return "".join(result_list)
