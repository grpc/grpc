# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.
#
from typing import Dict, Sequence, Union

import libcst as cst
import libcst.matchers as m
import libcst.metadata as meta
from libcst.testing.utils import UnitTest


class MatchersReplaceTest(UnitTest):
    def test_replace_sentinel(self) -> None:
        def _swap_bools(
            node: cst.CSTNode,
            extraction: Dict[str, Union[cst.CSTNode, Sequence[cst.CSTNode]]],
        ) -> cst.CSTNode:
            return cst.Name(
                "True" if cst.ensure_type(node, cst.Name).value == "False" else "False"
            )

        # Verify behavior when provided a sentinel
        replaced = m.replace(
            cst.RemovalSentinel.REMOVE, m.Name("True") | m.Name("False"), _swap_bools
        )
        self.assertEqual(replaced, cst.RemovalSentinel.REMOVE)
        replaced = m.replace(
            cst.MaybeSentinel.DEFAULT, m.Name("True") | m.Name("False"), _swap_bools
        )
        self.assertEqual(replaced, cst.MaybeSentinel.DEFAULT)

    def test_replace_noop(self) -> None:
        def _swap_bools(
            node: cst.CSTNode,
            extraction: Dict[str, Union[cst.CSTNode, Sequence[cst.CSTNode]]],
        ) -> cst.CSTNode:
            return cst.Name(
                "True" if cst.ensure_type(node, cst.Name).value == "False" else "False"
            )

        # Verify behavior when there's nothing to replace.
        original = cst.parse_module("foo: int = 5\ndef bar() -> str:\n    return 's'\n")
        replaced = cst.ensure_type(
            m.replace(original, m.Name("True") | m.Name("False"), _swap_bools),
            cst.Module,
        )
        # Should be identical tree contents
        self.assertTrue(original.deep_equals(replaced))
        # However, should be a new tree by identity
        self.assertNotEqual(id(original), id(replaced))

    def test_replace_simple(self) -> None:
        # Verify behavior when there's a static node as a replacement
        original = cst.parse_module(
            "foo: bool = True\ndef bar() -> bool:\n    return False\n"
        )
        replaced = cst.ensure_type(
            m.replace(original, m.Name("True") | m.Name("False"), cst.Name("boolean")),
            cst.Module,
        ).code
        self.assertEqual(
            replaced, "foo: bool = boolean\ndef bar() -> bool:\n    return boolean\n"
        )

    def test_replace_simple_sentinel(self) -> None:
        # Verify behavior when there's a sentinel as a replacement
        original = cst.parse_module(
            "def bar(x: int, y: int) -> bool:\n    return False\n"
        )
        replaced = cst.ensure_type(
            m.replace(original, m.Param(), cst.RemoveFromParent()), cst.Module
        ).code
        self.assertEqual(replaced, "def bar() -> bool:\n    return False\n")

    def test_replace_actual(self) -> None:
        def _swap_bools(
            node: cst.CSTNode,
            extraction: Dict[str, Union[cst.CSTNode, Sequence[cst.CSTNode]]],
        ) -> cst.CSTNode:
            return cst.Name(
                "True" if cst.ensure_type(node, cst.Name).value == "False" else "False"
            )

        # Verify behavior when there's lots to replace.
        original = cst.parse_module(
            "foo: bool = True\ndef bar() -> bool:\n    return False\n"
        )
        replaced = cst.ensure_type(
            m.replace(original, m.Name("True") | m.Name("False"), _swap_bools),
            cst.Module,
        ).code
        self.assertEqual(
            replaced, "foo: bool = False\ndef bar() -> bool:\n    return True\n"
        )

    def test_replace_add_one(self) -> None:
        def _add_one(
            node: cst.CSTNode,
            extraction: Dict[str, Union[cst.CSTNode, Sequence[cst.CSTNode]]],
        ) -> cst.CSTNode:
            return cst.Integer(str(int(cst.ensure_type(node, cst.Integer).value) + 1))

        # Verify slightly more complex transform behavior.
        original = cst.parse_module("foo: int = 36\ndef bar() -> int:\n    return 41\n")
        replaced = cst.ensure_type(
            m.replace(original, m.Integer(), _add_one), cst.Module
        ).code
        self.assertEqual(replaced, "foo: int = 37\ndef bar() -> int:\n    return 42\n")

    def test_replace_add_one_to_foo_args(self) -> None:
        def _add_one_to_arg(
            node: cst.CSTNode,
            extraction: Dict[str, Union[cst.CSTNode, Sequence[cst.CSTNode]]],
        ) -> cst.CSTNode:
            return node.deep_replace(
                # This can be either a node or a sequence, pyre doesn't know.
                cst.ensure_type(extraction["arg"], cst.CSTNode),
                # Grab the arg and add one to its value.
                cst.Integer(
                    str(int(cst.ensure_type(extraction["arg"], cst.Integer).value) + 1)
                ),
            )

        # Verify way more complex transform behavior.
        original = cst.parse_module(
            "foo: int = 37\ndef bar(baz: int) -> int:\n    return baz\n\nbiz: int = bar(41)\n"
        )
        replaced = cst.ensure_type(
            m.replace(
                original,
                m.Call(
                    func=m.Name("bar"),
                    args=[m.Arg(m.SaveMatchedNode(m.Integer(), "arg"))],
                ),
                _add_one_to_arg,
            ),
            cst.Module,
        ).code
        self.assertEqual(
            replaced,
            "foo: int = 37\ndef bar(baz: int) -> int:\n    return baz\n\nbiz: int = bar(42)\n",
        )

    def test_replace_sequence_extract(self) -> None:
        def _reverse_params(
            node: cst.CSTNode,
            extraction: Dict[str, Union[cst.CSTNode, Sequence[cst.CSTNode]]],
        ) -> cst.CSTNode:
            return cst.ensure_type(node, cst.FunctionDef).with_changes(
                # pyre-ignore We know "params" is a Sequence[Parameters] but asserting that
                # to pyre is difficult.
                params=cst.Parameters(params=list(reversed(extraction["params"])))
            )

        # Verify that we can still extract sequences with replace.
        original = cst.parse_module(
            "def bar(baz: int, foo: int, ) -> int:\n    return baz + foo\n"
        )
        replaced = cst.ensure_type(
            m.replace(
                original,
                m.FunctionDef(
                    params=m.Parameters(
                        params=m.SaveMatchedNode([m.ZeroOrMore(m.Param())], "params")
                    )
                ),
                _reverse_params,
            ),
            cst.Module,
        ).code
        self.assertEqual(
            replaced, "def bar(foo: int, baz: int, ) -> int:\n    return baz + foo\n"
        )

    def test_replace_metadata(self) -> None:
        def _rename_foo(
            node: cst.CSTNode,
            extraction: Dict[str, Union[cst.CSTNode, Sequence[cst.CSTNode]]],
        ) -> cst.CSTNode:
            return cst.ensure_type(node, cst.Name).with_changes(value="replaced")

        original = cst.parse_module(
            "foo: int = 37\ndef bar(foo: int) -> int:\n    return foo\n\nbiz: int = bar(42)\n"
        )
        wrapper = cst.MetadataWrapper(original)
        replaced = cst.ensure_type(
            m.replace(
                wrapper,
                m.Name(
                    metadata=m.MatchMetadataIfTrue(
                        meta.QualifiedNameProvider,
                        lambda qualnames: any(n.name == "foo" for n in qualnames),
                    )
                ),
                _rename_foo,
            ),
            cst.Module,
        ).code
        self.assertEqual(
            replaced,
            "replaced: int = 37\ndef bar(foo: int) -> int:\n    return foo\n\nbiz: int = bar(42)\n",
        )

    def test_replace_metadata_on_transform(self) -> None:
        def _rename_foo(
            node: cst.CSTNode,
            extraction: Dict[str, Union[cst.CSTNode, Sequence[cst.CSTNode]]],
        ) -> cst.CSTNode:
            return cst.ensure_type(node, cst.Name).with_changes(value="replaced")

        original = cst.parse_module(
            "foo: int = 37\ndef bar(foo: int) -> int:\n    return foo\n\nbiz: int = bar(42)\n"
        )
        wrapper = cst.MetadataWrapper(original)

        class TestTransformer(m.MatcherDecoratableTransformer):
            METADATA_DEPENDENCIES: Sequence[meta.ProviderT] = (
                meta.QualifiedNameProvider,
            )

            def leave_Module(
                self, original_node: cst.Module, updated_node: cst.Module
            ) -> cst.Module:
                # Somewhat contrived scenario to test codepaths.
                return cst.ensure_type(
                    self.replace(
                        original_node,
                        m.Name(
                            metadata=m.MatchMetadataIfTrue(
                                meta.QualifiedNameProvider,
                                lambda qualnames: any(
                                    n.name == "foo" for n in qualnames
                                ),
                            )
                        ),
                        _rename_foo,
                    ),
                    cst.Module,
                )

        replaced = cst.ensure_type(wrapper.visit(TestTransformer()), cst.Module).code
        self.assertEqual(
            replaced,
            "replaced: int = 37\ndef bar(foo: int) -> int:\n    return foo\n\nbiz: int = bar(42)\n",
        )

    def test_replace_updated_node_changes(self) -> None:
        def _replace_nested(
            node: cst.CSTNode,
            extraction: Dict[str, Union[cst.CSTNode, Sequence[cst.CSTNode]]],
        ) -> cst.CSTNode:
            return cst.ensure_type(node, cst.Call).with_changes(
                args=[
                    cst.Arg(
                        cst.Name(
                            value=cst.ensure_type(
                                cst.ensure_type(extraction["inner"], cst.Call).func,
                                cst.Name,
                            ).value
                            + "_immediate"
                        )
                    )
                ]
            )

        original = cst.parse_module(
            "def foo(val: int) -> int:\n    return val\nbar = foo\nbaz = foo\nbiz = foo\nfoo(bar(baz(biz(5))))\n"
        )
        replaced = cst.ensure_type(
            m.replace(
                original,
                m.Call(args=[m.Arg(m.SaveMatchedNode(m.Call(), "inner"))]),
                _replace_nested,
            ),
            cst.Module,
        ).code
        self.assertEqual(
            replaced,
            "def foo(val: int) -> int:\n    return val\nbar = foo\nbaz = foo\nbiz = foo\nfoo(bar_immediate)\n",
        )
