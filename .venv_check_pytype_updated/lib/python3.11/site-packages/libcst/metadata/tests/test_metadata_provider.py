# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from unittest.mock import Mock

import libcst as cst
from libcst import parse_module
from libcst._exceptions import MetadataException
from libcst._visitors import CSTTransformer
from libcst.metadata import (
    BatchableMetadataProvider,
    MetadataWrapper,
    VisitorMetadataProvider,
)
from libcst.testing.utils import UnitTest


class MetadataProviderTest(UnitTest):
    def test_visitor_provider(self) -> None:
        """
        Tests that visitor providers are resolved correctly.

        Sets 2 metadata entries for every node:
            SimpleProvider -> 1
            DependentProvider - > 2
        """

        test = self

        class SimpleProvider(VisitorMetadataProvider[int]):
            def on_visit(self, node: cst.CSTNode) -> bool:
                self.set_metadata(node, 1)
                return True

        class DependentProvider(VisitorMetadataProvider[int]):
            METADATA_DEPENDENCIES = (SimpleProvider,)

            def on_visit(self, node: cst.CSTNode) -> bool:
                self.set_metadata(node, self.get_metadata(SimpleProvider, node) + 1)
                return True

        class DependentVisitor(CSTTransformer):
            # Declare both providers so the visitor has acesss to both types of metadata
            METADATA_DEPENDENCIES = (DependentProvider, SimpleProvider)

            def visit_Module(self, node: cst.Module) -> None:
                # Check metadata is set
                test.assertEqual(self.get_metadata(SimpleProvider, node), 1)
                test.assertEqual(self.get_metadata(DependentProvider, node), 2)

            def visit_Pass(self, node: cst.Pass) -> None:
                # Check metadata is set
                test.assertEqual(self.get_metadata(SimpleProvider, node), 1)
                test.assertEqual(self.get_metadata(DependentProvider, node), 2)

        module = parse_module("pass")
        MetadataWrapper(module).visit(DependentVisitor())

    def test_batched_provider(self) -> None:
        """
        Tests that batchable providers are resolved correctly.

        Sets metadata on:
            - pass: BatchedProviderA -> 1
                    BatchedProviderB -> "a"

        """
        test = self
        mock = Mock()

        class BatchedProviderA(BatchableMetadataProvider[int]):
            def visit_Pass(self, node: cst.Pass) -> None:
                mock.visited_a()
                self.set_metadata(node, 1)

        class BatchedProviderB(BatchableMetadataProvider[str]):
            def visit_Pass(self, node: cst.Pass) -> None:
                mock.visited_b()
                self.set_metadata(node, "a")

        class DependentVisitor(CSTTransformer):
            METADATA_DEPENDENCIES = (BatchedProviderA, BatchedProviderB)

            def visit_Pass(self, node: cst.Pass) -> None:
                # Check metadata is set
                test.assertEqual(self.get_metadata(BatchedProviderA, node), 1)
                test.assertEqual(self.get_metadata(BatchedProviderB, node), "a")

        module = parse_module("pass")
        MetadataWrapper(module).visit(DependentVisitor())

        # Check that each batchable visitor is only called once
        mock.visited_a.assert_called_once()
        mock.visited_b.assert_called_once()

    def test_mixed_providers(self) -> None:
        """
        Tests that a mixed set of providers is resolved properly.

        Sets metadata on pass:
            BatchedProviderA -> 2
            BatchedProviderB -> 3
            DependentProvider -> 5
            DependentBatched -> 4
        """
        test = self
        mock = Mock()

        class SimpleProvider(VisitorMetadataProvider[int]):
            def visit_Pass(self, node: cst.CSTNode) -> None:
                mock.visited_simple()
                self.set_metadata(node, 1)

        class BatchedProviderA(BatchableMetadataProvider[int]):
            METADATA_DEPENDENCIES = (SimpleProvider,)

            def visit_Pass(self, node: cst.Pass) -> None:
                mock.visited_a()
                self.set_metadata(node, 2)

        class BatchedProviderB(BatchableMetadataProvider[int]):
            METADATA_DEPENDENCIES = (SimpleProvider,)

            def visit_Pass(self, node: cst.Pass) -> None:
                mock.visited_b()
                self.set_metadata(node, 3)

        class DependentProvider(VisitorMetadataProvider[int]):
            METADATA_DEPENDENCIES = (BatchedProviderA, BatchedProviderB)

            def on_visit(self, node: cst.CSTNode) -> bool:
                sum = self.get_metadata(BatchedProviderA, node, 0) + self.get_metadata(
                    BatchedProviderB, node, 0
                )
                self.set_metadata(node, sum)
                return True

        class BatchedProviderC(BatchableMetadataProvider[int]):
            METADATA_DEPENDENCIES = (BatchedProviderA,)

            def visit_Pass(self, node: cst.Pass) -> None:
                mock.visited_c()
                self.set_metadata(node, self.get_metadata(BatchedProviderA, node) * 2)

        class DependentVisitor(CSTTransformer):
            METADATA_DEPENDENCIES = (
                BatchedProviderA,
                BatchedProviderB,
                BatchedProviderC,
                DependentProvider,
            )

            def visit_Module(self, node: cst.Module) -> None:
                # Dependent visitor set metadata on all nodes but for module it
                # defaulted to 0 because BatchedProviderA/B only set metadata on
                # pass nodes
                test.assertEqual(self.get_metadata(DependentProvider, node), 0)

            def visit_Pass(self, node: cst.Pass) -> None:
                # Check metadata is set
                test.assertEqual(self.get_metadata(BatchedProviderA, node), 2)
                test.assertEqual(self.get_metadata(BatchedProviderB, node), 3)
                test.assertEqual(self.get_metadata(BatchedProviderC, node), 4)
                test.assertEqual(self.get_metadata(DependentProvider, node), 5)

        module = parse_module("pass")
        MetadataWrapper(module).visit(DependentVisitor())

        # Check each visitor is called once
        mock.visited_simple.assert_called_once()
        mock.visited_a.assert_called_once()
        mock.visited_b.assert_called_once()
        mock.visited_c.assert_called_once()

    def test_inherited_metadata(self) -> None:
        """
        Tests that classes inherit access to metadata declared by their base
        classes.
        """
        test_runner = self
        mock = Mock()

        class SimpleProvider(VisitorMetadataProvider[int]):
            def visit_Pass(self, node: cst.Pass) -> None:
                mock.visited_simple()
                self.set_metadata(node, 1)

        class VisitorA(CSTTransformer):
            METADATA_DEPENDENCIES = (SimpleProvider,)

        class VisitorB(VisitorA):
            def visit_Pass(self, node: cst.Pass) -> None:
                test_runner.assertEqual(self.get_metadata(SimpleProvider, node), 1)

        module = parse_module("pass")
        MetadataWrapper(module).visit(VisitorB())

        # Check each visitor is called once
        mock.visited_simple.assert_called_once()

    def test_provider_inherited_metadata(self) -> None:
        """
        Tests that providers inherit access to metadata declared by their base
        classes.
        """
        test_runner = self
        mock = Mock()

        class ProviderA(VisitorMetadataProvider[int]):
            def visit_Pass(self, node: cst.Pass) -> None:
                mock.visited_a()
                self.set_metadata(node, 1)

        class ProviderB(VisitorMetadataProvider[int]):
            METADATA_DEPENDENCIES = (ProviderA,)

        class ProviderC(ProviderB):
            def visit_Pass(self, node: cst.Pass) -> None:
                mock.visited_c()
                test_runner.assertEqual(self.get_metadata(ProviderA, node), 1)

        class Visitor(CSTTransformer):
            METADATA_DEPENDENCIES = (ProviderC,)

        module = parse_module("pass")
        MetadataWrapper(module).visit(Visitor())

        # Check each visitor is called once
        mock.visited_a.assert_called_once()
        mock.visited_c.assert_called_once()

    def test_batchable_provider_inherited_metadata(self) -> None:
        """
        Tests that batchable providers inherit access to metadata declared by
        their base classes.
        """
        test_runner = self
        mock = Mock()

        class ProviderA(VisitorMetadataProvider[int]):
            def visit_Pass(self, node: cst.Pass) -> None:
                mock.visited_a()
                self.set_metadata(node, 1)

        class ProviderB(BatchableMetadataProvider[int]):
            METADATA_DEPENDENCIES = (ProviderA,)

        class ProviderC(ProviderB):
            def visit_Pass(self, node: cst.Pass) -> None:
                mock.visited_c()
                test_runner.assertEqual(self.get_metadata(ProviderA, node), 1)

        class VisitorA(CSTTransformer):
            METADATA_DEPENDENCIES = (ProviderC,)

        module = parse_module("pass")
        MetadataWrapper(module).visit(VisitorA())

        # Check each visitor is called once
        mock.visited_a.assert_called_once()
        mock.visited_c.assert_called_once()

    def test_self_metadata(self) -> None:
        """
        Tests a provider can access its own metadata (assuming it has been
        set properly.)
        """
        test_runner = self

        class ProviderA(VisitorMetadataProvider[bool]):
            def on_visit(self, node: cst.CSTNode) -> bool:
                self.set_metadata(node, True)
                return True

            def on_leave(self, original_node: cst.CSTNode) -> None:
                test_runner.assertEqual(
                    self.get_metadata(type(self), original_node), True
                )

        class AVisitor(CSTTransformer):
            METADATA_DEPENDENCIES = (ProviderA,)

        cst.Module([]).visit(AVisitor())

    def test_unset_metadata(self) -> None:
        """
        Tests that access to unset metadata throws a key error.
        """

        class ProviderA(VisitorMetadataProvider[bool]):
            pass

        class AVisitor(CSTTransformer):
            METADATA_DEPENDENCIES = (ProviderA,)

            def on_visit(self, node: cst.CSTNode) -> bool:
                self.get_metadata(ProviderA, node)
                return True

        with self.assertRaisesRegex(
            KeyError,
            "ProviderA is a dependency, but not set; did you forget a MetadataWrapper?",
        ):
            cst.Module([]).visit(AVisitor())

    def test_undeclared_metadata(self) -> None:
        """
        Tests that access to undeclared metadata throws a key error.
        """

        class ProviderA(VisitorMetadataProvider[bool]):
            pass

        class ProviderB(VisitorMetadataProvider[bool]):
            pass

        class AVisitor(CSTTransformer):
            METADATA_DEPENDENCIES = (ProviderA,)

            def on_visit(self, node: cst.CSTNode) -> bool:
                self.get_metadata(ProviderA, node, True)
                self.get_metadata(ProviderB, node)
                return True

        with self.assertRaisesRegex(
            KeyError,
            "ProviderB is not declared as a dependency in AVisitor.METADATA_DEPENDENCIES.",
        ):
            MetadataWrapper(cst.Module([])).visit(AVisitor())

    def test_circular_dependency(self) -> None:
        """
        Tests that circular dependencies are detected.
        """

        class ProviderA(VisitorMetadataProvider[str]):
            pass

        ProviderA.METADATA_DEPENDENCIES = (ProviderA,)

        class BadVisitor(CSTTransformer):
            METADATA_DEPENDENCIES = (ProviderA,)

        with self.assertRaisesRegex(
            MetadataException, "Detected circular dependencies in ProviderA"
        ):
            MetadataWrapper(cst.Module([])).visit(BadVisitor())
