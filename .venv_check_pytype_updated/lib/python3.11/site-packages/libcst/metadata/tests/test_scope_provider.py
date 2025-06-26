# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.


import sys
from textwrap import dedent
from typing import cast, Mapping, Sequence, Tuple
from unittest import mock

import libcst as cst
from libcst import ensure_type
from libcst._parser.entrypoints import is_native
from libcst.metadata import MetadataWrapper
from libcst.metadata.scope_provider import (
    _gen_dotted_names,
    AnnotationScope,
    Assignment,
    BuiltinAssignment,
    BuiltinScope,
    ClassScope,
    ComprehensionScope,
    FunctionScope,
    GlobalScope,
    ImportAssignment,
    LocalScope,
    QualifiedName,
    QualifiedNameSource,
    Scope,
    ScopeProvider,
)
from libcst.testing.utils import data_provider, UnitTest


class DependentVisitor(cst.CSTVisitor):
    METADATA_DEPENDENCIES = (ScopeProvider,)


def get_scope_metadata_provider(
    module_str: str,
) -> Tuple[cst.Module, Mapping[cst.CSTNode, Scope]]:
    wrapper = MetadataWrapper(cst.parse_module(dedent(module_str)))
    return (
        wrapper.module,
        cast(
            Mapping[cst.CSTNode, Scope], wrapper.resolve(ScopeProvider)
        ),  # we're sure every node has an associated scope
    )


class ScopeProviderTest(UnitTest):
    def test_not_in_scope(self) -> None:
        m, scopes = get_scope_metadata_provider(
            """
            pass
            """
        )
        global_scope = scopes[m]
        self.assertEqual(global_scope["not_in_scope"], set())

    def test_accesses(self) -> None:
        m, scopes = get_scope_metadata_provider(
            """
            foo = 'toplevel'
            fn1(foo)
            fn2(foo)
            def fn_def():
                foo = 'shadow'
                fn3(foo)
            """
        )
        scope_of_module = scopes[m]
        self.assertIsInstance(scope_of_module, GlobalScope)
        global_foo_assignments = list(scope_of_module["foo"])
        self.assertEqual(len(global_foo_assignments), 1)
        foo_assignment = global_foo_assignments[0]
        self.assertEqual(len(foo_assignment.references), 2)
        fn1_call_arg = ensure_type(
            ensure_type(
                ensure_type(m.body[1], cst.SimpleStatementLine).body[0], cst.Expr
            ).value,
            cst.Call,
        ).args[0]

        fn2_call_arg = ensure_type(
            ensure_type(
                ensure_type(m.body[2], cst.SimpleStatementLine).body[0], cst.Expr
            ).value,
            cst.Call,
        ).args[0]
        self.assertEqual(
            {access.node for access in foo_assignment.references},
            {fn1_call_arg.value, fn2_call_arg.value},
        )
        func_body = ensure_type(m.body[3], cst.FunctionDef).body
        func_foo_statement = func_body.body[0]
        scope_of_func_statement = scopes[func_foo_statement]
        self.assertIsInstance(scope_of_func_statement, FunctionScope)
        func_foo_assignments = scope_of_func_statement["foo"]
        self.assertEqual(len(func_foo_assignments), 1)
        foo_assignment = list(func_foo_assignments)[0]
        self.assertEqual(len(foo_assignment.references), 1)
        fn3_call_arg = ensure_type(
            ensure_type(
                ensure_type(func_body.body[1], cst.SimpleStatementLine).body[0],
                cst.Expr,
            ).value,
            cst.Call,
        ).args[0]
        self.assertEqual(
            {access.node for access in foo_assignment.references}, {fn3_call_arg.value}
        )

        wrapper = MetadataWrapper(cst.parse_module("from a import b\n"))
        wrapper.visit(DependentVisitor())

        wrapper = MetadataWrapper(cst.parse_module("def a():\n    from b import c\n\n"))
        wrapper.visit(DependentVisitor())

    def test_fstring_accesses(self) -> None:
        m, scopes = get_scope_metadata_provider(
            """
            from a import b
            f"{b}" "hello"
            """
        )
        global_scope = scopes[m]
        self.assertIsInstance(global_scope, GlobalScope)
        global_accesses = list(global_scope.accesses)
        self.assertEqual(len(global_accesses), 1)
        import_node = ensure_type(
            ensure_type(m.body[0], cst.SimpleStatementLine).body[0], cst.ImportFrom
        )
        b_referent = list(global_accesses[0].referents)[0]
        self.assertIsInstance(b_referent, Assignment)
        if isinstance(b_referent, Assignment):  # for the typechecker's eyes
            self.assertEqual(b_referent.node, import_node)

    @data_provider((("any",), ("True",), ("Exception",), ("__name__",)))
    def test_builtins(self, builtin: str) -> None:
        m, scopes = get_scope_metadata_provider(
            """
            def fn():
                pass
            """
        )
        scope_of_module = scopes[m]
        self.assertIsInstance(scope_of_module, GlobalScope)
        self.assertEqual(len(scope_of_module[builtin]), 1)
        self.assertEqual(len(scope_of_module["something_not_a_builtin"]), 0)

        scope_of_builtin = scope_of_module.parent
        self.assertIsInstance(scope_of_builtin, BuiltinScope)
        self.assertEqual(len(scope_of_builtin[builtin]), 1)
        self.assertEqual(len(scope_of_builtin["something_not_a_builtin"]), 0)

        func_body = ensure_type(m.body[0], cst.FunctionDef).body
        func_pass_statement = func_body.body[0]
        scope_of_func_statement = scopes[func_pass_statement]
        self.assertIsInstance(scope_of_func_statement, FunctionScope)
        self.assertEqual(len(scope_of_func_statement[builtin]), 1)
        self.assertEqual(len(scope_of_func_statement["something_not_a_builtin"]), 0)

    def test_import(self) -> None:
        m, scopes = get_scope_metadata_provider(
            """
            import foo.bar
            import fizz.buzz as fizzbuzz
            import a.b.c
            import d.e.f as g
            """
        )
        scope_of_module = scopes[m]

        import_0 = cst.ensure_type(
            cst.ensure_type(m.body[0], cst.SimpleStatementLine).body[0], cst.Import
        )
        self.assertEqual(scopes[import_0], scope_of_module)
        import_aliases = import_0.names
        if not isinstance(import_aliases, cst.ImportStar):
            for alias in import_aliases:
                self.assertEqual(scopes[alias], scope_of_module)

        for idx, in_scopes in enumerate(
            [
                ["foo", "foo.bar"],
                ["fizzbuzz"],
                ["a", "a.b", "a.b.c"],
                ["g"],
            ]
        ):
            for in_scope in in_scopes:
                self.assertEqual(
                    len(scope_of_module[in_scope]), 1, f"{in_scope} should be in scope."
                )

                assignment = cast(ImportAssignment, list(scope_of_module[in_scope])[0])
                self.assertEqual(
                    assignment.name,
                    in_scope,
                    f"ImportAssignment name {assignment.name} should equal to {in_scope}.",
                )
                import_node = ensure_type(m.body[idx], cst.SimpleStatementLine).body[0]
                self.assertEqual(
                    assignment.node,
                    import_node,
                    f"The node of ImportAssignment {assignment.node} should equal to {import_node}",
                )
                self.assertTrue(isinstance(import_node, (cst.Import, cst.ImportFrom)))

                names = import_node.names

                self.assertFalse(isinstance(names, cst.ImportStar))

                alias = names[0]
                as_name = alias.asname.name if alias.asname else alias.name
                self.assertEqual(
                    assignment.as_name,
                    as_name,
                    f"The alias name of ImportAssignment {assignment.as_name} should equal to {as_name}",
                )

    def test_dotted_import_access(self) -> None:
        m, scopes = get_scope_metadata_provider(
            """
            import a.b.c, x.y
            a.b.c(x.z)
            """
        )
        scope_of_module = scopes[m]
        first_statement = ensure_type(m.body[1], cst.SimpleStatementLine)
        call = ensure_type(
            ensure_type(first_statement.body[0], cst.Expr).value, cst.Call
        )
        self.assertTrue("a.b.c" in scope_of_module)
        self.assertTrue("a" in scope_of_module)
        self.assertEqual(scope_of_module.accesses["a"], set())

        a_b_c_assignment = cast(ImportAssignment, list(scope_of_module["a.b.c"])[0])
        a_b_c_access = list(a_b_c_assignment.references)[0]
        self.assertEqual(scope_of_module.accesses["a.b.c"], {a_b_c_access})
        self.assertEqual(a_b_c_access.node, call.func)

        x_assignment = cast(Assignment, list(scope_of_module["x"])[0])
        x_access = list(x_assignment.references)[0]
        self.assertEqual(scope_of_module.accesses["x"], {x_access})
        self.assertEqual(
            x_access.node, ensure_type(call.args[0].value, cst.Attribute).value
        )

        self.assertTrue("x.y" in scope_of_module)
        self.assertEqual(list(scope_of_module["x.y"])[0].references, set())
        self.assertEqual(scope_of_module.accesses["x.y"], set())

    def test_dotted_import_access_reference_by_node(self) -> None:
        m, scopes = get_scope_metadata_provider(
            """
            import a.b.c
            a.b.c()
            """
        )
        scope_of_module = scopes[m]
        first_statement = ensure_type(m.body[1], cst.SimpleStatementLine)
        call = ensure_type(
            ensure_type(first_statement.body[0], cst.Expr).value, cst.Call
        )

        a_b_c_assignment = cast(ImportAssignment, list(scope_of_module["a.b.c"])[0])
        a_b_c_access = list(a_b_c_assignment.references)[0]
        self.assertEqual(scope_of_module.accesses[call], {a_b_c_access})
        self.assertEqual(a_b_c_access.node, call.func)

    def test_decorator_access_reference_by_node(self) -> None:
        m, scopes = get_scope_metadata_provider(
            """
            import decorator

            @decorator
            def f():
                pass
            """
        )
        scope_of_module = scopes[m]
        function_def = ensure_type(m.body[1], cst.FunctionDef)
        decorator = function_def.decorators[0]
        self.assertTrue("decorator" in scope_of_module)

        decorator_assignment = cast(
            ImportAssignment, list(scope_of_module["decorator"])[0]
        )
        decorator_access = list(decorator_assignment.references)[0]
        self.assertEqual(scope_of_module.accesses[decorator], {decorator_access})

    def test_dotted_import_with_call_access(self) -> None:
        m, scopes = get_scope_metadata_provider(
            """
            import os.path
            os.path.join("A", "B").lower()
            """
        )
        scope_of_module = scopes[m]
        first_statement = ensure_type(m.body[1], cst.SimpleStatementLine)
        attr = ensure_type(
            ensure_type(
                ensure_type(
                    ensure_type(
                        ensure_type(first_statement.body[0], cst.Expr).value, cst.Call
                    ).func,
                    cst.Attribute,
                ).value,
                cst.Call,
            ).func,
            cst.Attribute,
        ).value
        self.assertTrue("os.path" in scope_of_module)
        self.assertTrue("os" in scope_of_module)

        os_path_join_assignment = cast(
            ImportAssignment, list(scope_of_module["os.path"])[0]
        )
        os_path_join_assignment_references = list(os_path_join_assignment.references)
        self.assertNotEqual(len(os_path_join_assignment_references), 0)
        os_path_join_access = os_path_join_assignment_references[0]
        self.assertEqual(scope_of_module.accesses["os"], set())
        self.assertEqual(scope_of_module.accesses["os.path"], {os_path_join_access})
        self.assertEqual(scope_of_module.accesses["os.path.join"], set())
        self.assertEqual(os_path_join_access.node, attr)

    def test_import_from(self) -> None:
        m, scopes = get_scope_metadata_provider(
            """
            from foo.bar import a, b as b_renamed
            from . import c
            from .foo import d
            """
        )
        scope_of_module = scopes[m]

        import_from = cst.ensure_type(
            cst.ensure_type(m.body[0], cst.SimpleStatementLine).body[0], cst.ImportFrom
        )
        self.assertEqual(scopes[import_from], scope_of_module)
        import_aliases = import_from.names
        if not isinstance(import_aliases, cst.ImportStar):
            for alias in import_aliases:
                self.assertEqual(scopes[alias], scope_of_module)

        for idx, in_scope, imported_object_idx in [
            (0, "a", 0),
            (0, "b_renamed", 1),
            (1, "c", 0),
            (2, "d", 0),
        ]:
            self.assertEqual(
                len(scope_of_module[in_scope]), 1, f"{in_scope} should be in scope."
            )
            import_assignment = cast(
                ImportAssignment, list(scope_of_module[in_scope])[0]
            )
            self.assertEqual(
                import_assignment.name,
                in_scope,
                f"The name of ImportAssignment {import_assignment.name} should equal to {in_scope}.",
            )
            import_node = ensure_type(m.body[idx], cst.SimpleStatementLine).body[0]
            self.assertEqual(
                import_assignment.node,
                import_node,
                f"The node of ImportAssignment {import_assignment.node} should equal to {import_node}",
            )

            self.assertTrue(isinstance(import_node, (cst.Import, cst.ImportFrom)))

            names = import_node.names

            self.assertFalse(isinstance(names, cst.ImportStar))

            alias = names[imported_object_idx]
            as_name = alias.asname.name if alias.asname else alias.name
            self.assertEqual(
                import_assignment.as_name,
                as_name,
                f"The alias name of ImportAssignment {import_assignment.as_name} should equal to {as_name}",
            )

        for not_in_scope in ["foo", "bar", "foo.bar", "b"]:
            self.assertEqual(
                len(scope_of_module[not_in_scope]),
                0,
                f"{not_in_scope} should not be in scope.",
            )

    def test_function_scope(self) -> None:
        m, scopes = get_scope_metadata_provider(
            """
            global_var = None
            def foo(arg, **kwargs):
                local_var = 5
            """
        )
        scope_of_module = scopes[m]
        func_def = ensure_type(m.body[1], cst.FunctionDef)
        self.assertEqual(scopes[func_def], scopes[func_def.name])
        func_body_statement = func_def.body.body[0]
        scope_of_func = scopes[func_body_statement]
        self.assertIsInstance(scope_of_func, FunctionScope)
        self.assertTrue("global_var" in scope_of_module)
        self.assertTrue("global_var" in scope_of_func)
        self.assertTrue("arg" not in scope_of_module)
        self.assertTrue("arg" in scope_of_func)
        self.assertTrue("kwargs" not in scope_of_module)
        self.assertTrue("kwargs" in scope_of_func)
        self.assertTrue("local_var" not in scope_of_module)
        self.assertTrue("local_var" in scope_of_func)

    def test_class_scope(self) -> None:
        m, scopes = get_scope_metadata_provider(
            """
            global_var = None
            @cls_attr
            class Cls(cls_attr, kwarg=cls_attr):
                cls_attr = 5
                def f():
                    pass
            """
        )
        scope_of_module = scopes[m]
        self.assertIsInstance(scope_of_module, GlobalScope)
        cls_assignments = list(scope_of_module["Cls"])
        self.assertEqual(len(cls_assignments), 1)
        cls_assignment = cast(Assignment, cls_assignments[0])
        cls_def = ensure_type(m.body[1], cst.ClassDef)
        self.assertEqual(cls_assignment.node, cls_def)
        self.assertEqual(scopes[cls_def], scopes[cls_def.name])
        cls_body = cls_def.body
        cls_body_statement = cls_body.body[0]
        scope_of_class = scopes[cls_body_statement]
        self.assertIsInstance(scope_of_class, ClassScope)
        func_body = ensure_type(cls_body.body[1], cst.FunctionDef).body
        func_body_statement = func_body.body[0]
        scope_of_func = scopes[func_body_statement]
        self.assertIsInstance(scope_of_func, FunctionScope)
        self.assertTrue("global_var" in scope_of_module)
        self.assertTrue("global_var" in scope_of_class)
        self.assertTrue("global_var" in scope_of_func)
        self.assertTrue("Cls" in scope_of_module)
        self.assertTrue("Cls" in scope_of_class)
        self.assertTrue("Cls" in scope_of_func)
        self.assertTrue("cls_attr" not in scope_of_module)
        self.assertTrue("cls_attr" in scope_of_class)
        self.assertTrue("cls_attr" not in scope_of_func)

    def test_comprehension_scope(self) -> None:
        m, scopes = get_scope_metadata_provider(
            """
            iterator = None
            condition = None
            [elt for target in iterator if condition]
            {elt for target in iterator if condition}
            {elt: target for target in iterator if condition}
            (elt for target in iterator if condition)
            """
        )
        scope_of_module = scopes[m]
        self.assertIsInstance(scope_of_module, GlobalScope)

        list_comp = ensure_type(
            ensure_type(
                ensure_type(m.body[2], cst.SimpleStatementLine).body[0], cst.Expr
            ).value,
            cst.ListComp,
        )
        scope_of_list_comp = scopes[list_comp.elt]
        self.assertIsInstance(scope_of_list_comp, ComprehensionScope)

        set_comp = ensure_type(
            ensure_type(
                ensure_type(m.body[3], cst.SimpleStatementLine).body[0], cst.Expr
            ).value,
            cst.SetComp,
        )
        scope_of_set_comp = scopes[set_comp.elt]
        self.assertIsInstance(scope_of_set_comp, ComprehensionScope)

        dict_comp = ensure_type(
            ensure_type(
                ensure_type(m.body[4], cst.SimpleStatementLine).body[0], cst.Expr
            ).value,
            cst.DictComp,
        )
        scope_of_dict_comp = scopes[dict_comp.key]
        self.assertIsInstance(scope_of_dict_comp, ComprehensionScope)

        generator_expr = ensure_type(
            ensure_type(
                ensure_type(m.body[5], cst.SimpleStatementLine).body[0], cst.Expr
            ).value,
            cst.GeneratorExp,
        )
        scope_of_generator_expr = scopes[generator_expr.elt]
        self.assertIsInstance(scope_of_generator_expr, ComprehensionScope)

        self.assertTrue("iterator" in scope_of_module)
        self.assertTrue("iterator" in scope_of_list_comp)
        self.assertTrue("iterator" in scope_of_set_comp)
        self.assertTrue("iterator" in scope_of_dict_comp)
        self.assertTrue("iterator" in scope_of_generator_expr)

        self.assertTrue("condition" in scope_of_module)
        self.assertTrue("condition" in scope_of_list_comp)
        self.assertTrue("condition" in scope_of_set_comp)
        self.assertTrue("condition" in scope_of_dict_comp)
        self.assertTrue("condition" in scope_of_generator_expr)

        self.assertTrue("elt" not in scope_of_module)
        self.assertTrue("elt" not in scope_of_list_comp)
        self.assertTrue("elt" not in scope_of_set_comp)
        self.assertTrue("elt" not in scope_of_dict_comp)
        self.assertTrue("elt" not in scope_of_generator_expr)

        self.assertTrue("target" not in scope_of_module)
        self.assertTrue("target" in scope_of_list_comp)
        self.assertTrue("target" in scope_of_set_comp)
        self.assertTrue("target" in scope_of_dict_comp)
        self.assertTrue("target" in scope_of_generator_expr)

    def test_nested_comprehension_scope(self) -> None:
        m, scopes = get_scope_metadata_provider(
            """
            [y for x in iterator for y in x]
            """
        )
        scope_of_module = scopes[m]
        self.assertIsInstance(scope_of_module, GlobalScope)

        list_comp = ensure_type(
            ensure_type(
                ensure_type(m.body[0], cst.SimpleStatementLine).body[0], cst.Expr
            ).value,
            cst.ListComp,
        )
        scope_of_list_comp = scopes[list_comp.elt]
        self.assertIsInstance(scope_of_list_comp, ComprehensionScope)

        self.assertIs(scopes[list_comp], scope_of_module)
        self.assertIs(scopes[list_comp.elt], scope_of_list_comp)

        self.assertIs(scopes[list_comp.for_in], scope_of_module)
        self.assertIs(scopes[list_comp.for_in.iter], scope_of_module)
        self.assertIs(scopes[list_comp.for_in.target], scope_of_list_comp)

        inner_for_in = ensure_type(list_comp.for_in.inner_for_in, cst.CompFor)
        self.assertIs(scopes[inner_for_in], scope_of_list_comp)
        self.assertIs(scopes[inner_for_in.iter], scope_of_list_comp)
        self.assertIs(scopes[inner_for_in.target], scope_of_list_comp)

    def test_global_scope_overwrites(self) -> None:
        codes = (
            """
            class Cls:
                def f():
                    global var
                    var = ...
            """,
            """
            class Cls:
                def f():
                    global var
                    import f as var
            """,
        )
        for code in codes:
            m, scopes = get_scope_metadata_provider(code)
            scope_of_module = scopes[m]
            self.assertIsInstance(scope_of_module, GlobalScope)
            self.assertTrue("var" in scope_of_module)

            cls = ensure_type(m.body[0], cst.ClassDef)
            scope_of_cls = scopes[cls.body.body[0]]
            self.assertIsInstance(scope_of_cls, ClassScope)
            self.assertTrue("var" in scope_of_cls)

            f = ensure_type(cls.body.body[0], cst.FunctionDef)
            scope_of_f = scopes[f.body.body[0]]
            self.assertIsInstance(scope_of_f, FunctionScope)
            self.assertTrue("var" in scope_of_f)
            self.assertEqual(scope_of_f["var"], scope_of_module["var"])

    def test_nonlocal_scope_overwrites(self) -> None:
        codes = (
            """
            def outer_f():
                var = ...
                class Cls:
                    var = ...
                    def inner_f():
                        nonlocal var
                        var = ...
            """,
            """
            def outer_f():
                import f as var
                class Cls:
                    var = ...
                    def inner_f():
                        nonlocal var
                        var = ...
            """,
            """
            def outer_f():
                var = ...
                class Cls:
                    var = ...
                    def inner_f():
                        nonlocal var
                        import f as var
            """,
        )
        for code in codes:
            m, scopes = get_scope_metadata_provider(code)
            scope_of_module = scopes[m]
            self.assertIsInstance(scope_of_module, GlobalScope)
            self.assertTrue("var" not in scope_of_module)

            outer_f = ensure_type(m.body[0], cst.FunctionDef)
            outer_f_body_var = ensure_type(
                ensure_type(outer_f.body.body[0], cst.SimpleStatementLine).body[0],
                cst.CSTNode,
            )
            scope_of_outer_f = scopes[outer_f_body_var]
            self.assertIsInstance(scope_of_outer_f, FunctionScope)
            self.assertTrue("var" in scope_of_outer_f)
            self.assertEqual(len(scope_of_outer_f["var"]), 2)

            cls = ensure_type(outer_f.body.body[1], cst.ClassDef)
            scope_of_cls = scopes[cls.body.body[0]]
            self.assertIsInstance(scope_of_cls, ClassScope)
            self.assertTrue("var" in scope_of_cls)

            inner_f = ensure_type(cls.body.body[1], cst.FunctionDef)
            inner_f_body_var = ensure_type(
                ensure_type(inner_f.body.body[1], cst.SimpleStatementLine).body[0],
                cst.CSTNode,
            )
            scope_of_inner_f = scopes[inner_f_body_var]
            self.assertIsInstance(scope_of_inner_f, FunctionScope)
            self.assertTrue("var" in scope_of_inner_f)
            self.assertEqual(len(scope_of_inner_f["var"]), 2)
            self.assertEqual(
                {
                    cast(Assignment, assignment).node
                    for assignment in scope_of_outer_f["var"]
                },
                {
                    (
                        outer_f_body_var.targets[0].target
                        if isinstance(outer_f_body_var, cst.Assign)
                        else outer_f_body_var
                    ),
                    (
                        inner_f_body_var.targets[0].target
                        if isinstance(inner_f_body_var, cst.Assign)
                        else inner_f_body_var
                    ),
                },
            )

    def test_local_scope_shadowing_with_functions(self) -> None:
        m, scopes = get_scope_metadata_provider(
            """
            def f():
                def f():
                    f = ...
            """
        )
        scope_of_module = scopes[m]
        self.assertIsInstance(scope_of_module, GlobalScope)
        self.assertTrue("f" in scope_of_module)

        outer_f = ensure_type(m.body[0], cst.FunctionDef)
        scope_of_outer_f = scopes[outer_f.body.body[0]]
        self.assertIsInstance(scope_of_outer_f, FunctionScope)
        self.assertTrue("f" in scope_of_outer_f)
        out_f_assignment = list(scope_of_module["f"])[0]
        self.assertEqual(cast(Assignment, out_f_assignment).node, outer_f)

        inner_f = ensure_type(outer_f.body.body[0], cst.FunctionDef)
        scope_of_inner_f = scopes[inner_f.body.body[0]]
        self.assertIsInstance(scope_of_inner_f, FunctionScope)
        self.assertTrue("f" in scope_of_inner_f)
        inner_f_assignment = list(scope_of_outer_f["f"])[0]
        self.assertEqual(cast(Assignment, inner_f_assignment).node, inner_f)

    def test_func_param_scope(self) -> None:
        m, scopes = get_scope_metadata_provider(
            """
            @decorator
            def f(x: T=1, *vararg, y: T=2, z, **kwarg) -> RET:
                pass
            """
        )
        scope_of_module = scopes[m]
        self.assertIsInstance(scope_of_module, GlobalScope)
        self.assertTrue("f" in scope_of_module)

        f = ensure_type(m.body[0], cst.FunctionDef)
        scope_of_f = scopes[f.body.body[0]]
        self.assertIsInstance(scope_of_f, FunctionScope)

        decorator = f.decorators[0]
        x = f.params.params[0]
        xT = ensure_type(x.annotation, cst.Annotation)
        one = ensure_type(x.default, cst.BaseExpression)
        vararg = ensure_type(f.params.star_arg, cst.Param)
        y = f.params.kwonly_params[0]
        yT = ensure_type(y.annotation, cst.Annotation)
        two = ensure_type(y.default, cst.BaseExpression)
        z = f.params.kwonly_params[1]
        kwarg = ensure_type(f.params.star_kwarg, cst.Param)
        ret = ensure_type(f.returns, cst.Annotation).annotation

        self.assertEqual(scopes[decorator], scope_of_module)
        self.assertEqual(scopes[x], scope_of_f)
        self.assertEqual(scopes[xT], scope_of_module)
        self.assertEqual(scopes[one], scope_of_module)
        self.assertEqual(scopes[vararg], scope_of_f)
        self.assertEqual(scopes[y], scope_of_f)
        self.assertEqual(scopes[yT], scope_of_module)
        self.assertEqual(scopes[z], scope_of_f)
        self.assertEqual(scopes[two], scope_of_module)
        self.assertEqual(scopes[kwarg], scope_of_f)
        self.assertEqual(scopes[ret], scope_of_module)

        self.assertTrue("x" not in scope_of_module)
        self.assertTrue("x" in scope_of_f)
        self.assertTrue("vararg" not in scope_of_module)
        self.assertTrue("vararg" in scope_of_f)
        self.assertTrue("y" not in scope_of_module)
        self.assertTrue("y" in scope_of_f)
        self.assertTrue("z" not in scope_of_module)
        self.assertTrue("z" in scope_of_f)
        self.assertTrue("kwarg" not in scope_of_module)
        self.assertTrue("kwarg" in scope_of_f)

        self.assertEqual(cast(Assignment, list(scope_of_f["x"])[0]).node, x)
        self.assertEqual(cast(Assignment, list(scope_of_f["vararg"])[0]).node, vararg)
        self.assertEqual(cast(Assignment, list(scope_of_f["y"])[0]).node, y)
        self.assertEqual(cast(Assignment, list(scope_of_f["z"])[0]).node, z)
        self.assertEqual(cast(Assignment, list(scope_of_f["kwarg"])[0]).node, kwarg)

    def test_lambda_param_scope(self) -> None:
        m, scopes = get_scope_metadata_provider(
            """
            lambda x=1, *vararg, y=2, z, **kwarg:x
            """
        )
        scope_of_module = scopes[m]
        self.assertIsInstance(scope_of_module, GlobalScope)

        f = ensure_type(
            ensure_type(
                ensure_type(m.body[0], cst.SimpleStatementLine).body[0], cst.Expr
            ).value,
            cst.Lambda,
        )
        scope_of_f = scopes[f.body]
        self.assertIsInstance(scope_of_f, FunctionScope)

        x = f.params.params[0]
        one = ensure_type(x.default, cst.BaseExpression)
        vararg = ensure_type(f.params.star_arg, cst.Param)
        y = f.params.kwonly_params[0]
        two = ensure_type(y.default, cst.BaseExpression)
        z = f.params.kwonly_params[1]
        kwarg = ensure_type(f.params.star_kwarg, cst.Param)

        self.assertEqual(scopes[x], scope_of_f)
        self.assertEqual(scopes[one], scope_of_module)
        self.assertEqual(scopes[vararg], scope_of_f)
        self.assertEqual(scopes[y], scope_of_f)
        self.assertEqual(scopes[z], scope_of_f)
        self.assertEqual(scopes[two], scope_of_module)
        self.assertEqual(scopes[kwarg], scope_of_f)

        self.assertTrue("x" not in scope_of_module)
        self.assertTrue("x" in scope_of_f)
        self.assertTrue("vararg" not in scope_of_module)
        self.assertTrue("vararg" in scope_of_f)
        self.assertTrue("y" not in scope_of_module)
        self.assertTrue("y" in scope_of_f)
        self.assertTrue("z" not in scope_of_module)
        self.assertTrue("z" in scope_of_f)
        self.assertTrue("kwarg" not in scope_of_module)
        self.assertTrue("kwarg" in scope_of_f)

        self.assertEqual(cast(Assignment, list(scope_of_f["x"])[0]).node, x)
        self.assertEqual(cast(Assignment, list(scope_of_f["vararg"])[0]).node, vararg)
        self.assertEqual(cast(Assignment, list(scope_of_f["y"])[0]).node, y)
        self.assertEqual(cast(Assignment, list(scope_of_f["z"])[0]).node, z)
        self.assertEqual(cast(Assignment, list(scope_of_f["kwarg"])[0]).node, kwarg)

    def test_except_handler(self) -> None:
        """
        The ``except as`` is a special case. The asname is only available in the excep body
        block and it'll be removed when existing the block.
        See https://docs.python.org/3.4/reference/compound_stmts.html#except
        We don't create a new block for except body because we don't handle del in our Scope Analysis.
        """
        m, scopes = get_scope_metadata_provider(
            """
            try:
                ...
            except Exception as ex:
                ...
            """
        )
        scope_of_module = scopes[m]
        self.assertIsInstance(scope_of_module, GlobalScope)
        self.assertTrue("ex" in scope_of_module)
        self.assertEqual(
            cast(Assignment, list(scope_of_module["ex"])[0]).node,
            ensure_type(
                ensure_type(m.body[0], cst.Try).handlers[0].name, cst.AsName
            ).name,
        )

    def test_with_asname(self) -> None:
        m, scopes = get_scope_metadata_provider(
            """
            with open(file_name) as f:
                ...
            """
        )
        scope_of_module = scopes[m]
        self.assertIsInstance(scope_of_module, GlobalScope)
        self.assertTrue("f" in scope_of_module)
        self.assertEqual(
            cast(Assignment, list(scope_of_module["f"])[0]).node,
            ensure_type(
                ensure_type(m.body[0], cst.With).items[0].asname, cst.AsName
            ).name,
        )

    def test_get_qualified_names_for(self) -> None:
        m, scopes = get_scope_metadata_provider(
            """
            from a.b import c
            class Cls:
                def f(self) -> "c":
                    c()
                    d = {}
                    d['key'] = 0
            def g():
                pass
            g()
            """
        )
        cls = ensure_type(m.body[1], cst.ClassDef)
        f = ensure_type(cls.body.body[0], cst.FunctionDef)
        scope_of_module = scopes[m]
        self.assertEqual(
            scope_of_module.get_qualified_names_for(
                ensure_type(f.returns, cst.Annotation).annotation
            ),
            set(),
            "Get qualified name given a SimpleString type annotation is not supported",
        )

        c_call = ensure_type(
            ensure_type(f.body.body[0], cst.SimpleStatementLine).body[0], cst.Expr
        ).value
        scope_of_f = scopes[c_call]
        self.assertIsInstance(scope_of_f, FunctionScope)
        self.assertEqual(
            scope_of_f.get_qualified_names_for(c_call),
            {QualifiedName("a.b.c", QualifiedNameSource.IMPORT)},
        )
        self.assertEqual(
            scope_of_f.get_qualified_names_for(c_call),
            {QualifiedName("a.b.c", QualifiedNameSource.IMPORT)},
        )

        g_call = ensure_type(
            ensure_type(m.body[3], cst.SimpleStatementLine).body[0], cst.Expr
        ).value
        self.assertIsInstance(scope_of_module, GlobalScope)
        self.assertEqual(
            scope_of_module.get_qualified_names_for(g_call),
            {QualifiedName("g", QualifiedNameSource.LOCAL)},
        )
        d_name = (
            ensure_type(
                ensure_type(f.body.body[1], cst.SimpleStatementLine).body[0], cst.Assign
            )
            .targets[0]
            .target
        )
        self.assertEqual(
            scope_of_f.get_qualified_names_for(d_name),
            {QualifiedName("Cls.f.<locals>.d", QualifiedNameSource.LOCAL)},
        )
        d_subscript = (
            ensure_type(
                ensure_type(f.body.body[2], cst.SimpleStatementLine).body[0], cst.Assign
            )
            .targets[0]
            .target
        )
        self.assertEqual(
            scope_of_f.get_qualified_names_for(d_subscript),
            {QualifiedName("Cls.f.<locals>.d", QualifiedNameSource.LOCAL)},
        )

        for builtin in ["map", "int", "dict"]:
            self.assertEqual(
                scope_of_f.get_qualified_names_for(cst.Name(value=builtin)),
                {QualifiedName(f"builtins.{builtin}", QualifiedNameSource.BUILTIN)},
                f"Test builtin: {builtin}.",
            )

        self.assertEqual(
            scope_of_module.get_qualified_names_for(cst.Name(value="d")),
            set(),
            "Test variable d in global scope.",
        )

    def test_get_qualified_names_for_nested_cases(self) -> None:
        m, scopes = get_scope_metadata_provider(
            """
            class A:
                def f1(self):
                    def f2():
                        pass
                    f2()

                def f3(self):
                    class B():
                        ...
                    B()
            def f4():
                def f5():
                    class C:
                        pass
                    C()
                f5()
            """
        )
        cls_a = ensure_type(m.body[0], cst.ClassDef)
        func_f1 = ensure_type(cls_a.body.body[0], cst.FunctionDef)
        scope_of_cls_a = scopes[func_f1]
        self.assertIsInstance(scope_of_cls_a, ClassScope)
        self.assertEqual(
            scope_of_cls_a.get_qualified_names_for(func_f1),
            {QualifiedName("A.f1", QualifiedNameSource.LOCAL)},
        )
        func_f2_call = ensure_type(
            ensure_type(func_f1.body.body[1], cst.SimpleStatementLine).body[0], cst.Expr
        ).value
        scope_of_f1 = scopes[func_f2_call]
        self.assertIsInstance(scope_of_f1, FunctionScope)
        self.assertEqual(
            scope_of_f1.get_qualified_names_for(func_f2_call),
            {QualifiedName("A.f1.<locals>.f2", QualifiedNameSource.LOCAL)},
        )
        func_f3 = ensure_type(cls_a.body.body[1], cst.FunctionDef)
        call_b = ensure_type(
            ensure_type(func_f3.body.body[1], cst.SimpleStatementLine).body[0], cst.Expr
        ).value
        scope_of_f3 = scopes[call_b]
        self.assertIsInstance(scope_of_f3, FunctionScope)
        self.assertEqual(
            scope_of_f3.get_qualified_names_for(call_b),
            {QualifiedName("A.f3.<locals>.B", QualifiedNameSource.LOCAL)},
        )
        func_f4 = ensure_type(m.body[1], cst.FunctionDef)
        func_f5 = ensure_type(func_f4.body.body[0], cst.FunctionDef)
        scope_of_f4 = scopes[func_f5]
        self.assertIsInstance(scope_of_f4, FunctionScope)
        self.assertEqual(
            scope_of_f4.get_qualified_names_for(func_f5),
            {QualifiedName("f4.<locals>.f5", QualifiedNameSource.LOCAL)},
        )
        cls_c = func_f5.body.body[0]
        scope_of_f5 = scopes[cls_c]
        self.assertIsInstance(scope_of_f5, FunctionScope)
        self.assertEqual(
            scope_of_f5.get_qualified_names_for(cls_c),
            {QualifiedName("f4.<locals>.f5.<locals>.C", QualifiedNameSource.LOCAL)},
        )

    def test_get_qualified_names_for_the_same_prefix(self) -> None:
        m, scopes = get_scope_metadata_provider(
            """
                from a import b, bc
                bc()
            """
        )
        call = ensure_type(
            ensure_type(
                ensure_type(m.body[1], cst.SimpleStatementLine).body[0], cst.Expr
            ).value,
            cst.Call,
        )
        module_scope = scopes[m]
        self.assertEqual(
            module_scope.get_qualified_names_for(call.func),
            {QualifiedName("a.bc", QualifiedNameSource.IMPORT)},
        )

    def test_get_qualified_names_for_dotted_imports(self) -> None:
        m, scopes = get_scope_metadata_provider(
            """
                import a.b.c
                a(a.b.d)
            """
        )
        call = ensure_type(
            ensure_type(
                ensure_type(m.body[1], cst.SimpleStatementLine).body[0], cst.Expr
            ).value,
            cst.Call,
        )
        module_scope = scopes[m]
        self.assertEqual(
            module_scope.get_qualified_names_for(call.func),
            {QualifiedName("a", QualifiedNameSource.IMPORT)},
        )
        self.assertEqual(
            module_scope.get_qualified_names_for(call.args[0].value),
            {QualifiedName("a.b.d", QualifiedNameSource.IMPORT)},
        )

        import_stmt = ensure_type(
            ensure_type(m.body[0], cst.SimpleStatementLine).body[0], cst.Import
        )
        a_b_c = ensure_type(import_stmt.names[0].name, cst.Attribute)
        a_b = ensure_type(a_b_c.value, cst.Attribute)
        a = a_b.value
        self.assertEqual(
            module_scope.get_qualified_names_for(a_b_c),
            {QualifiedName("a.b.c", QualifiedNameSource.IMPORT)},
        )
        self.assertEqual(
            module_scope.get_qualified_names_for(a_b),
            {QualifiedName("a.b", QualifiedNameSource.IMPORT)},
        )
        self.assertEqual(
            module_scope.get_qualified_names_for(a),
            {QualifiedName("a", QualifiedNameSource.IMPORT)},
        )

    def test_multiple_assignments(self) -> None:
        m, scopes = get_scope_metadata_provider(
            """
                if 1:
                    from a import b as c
                elif 2:
                    from d import e as c
                c()
            """
        )
        call = ensure_type(
            ensure_type(m.body[1], cst.SimpleStatementLine).body[0], cst.Expr
        ).value
        scope = scopes[call]
        self.assertIsInstance(scope, GlobalScope)
        self.assertEqual(
            scope.get_qualified_names_for(call),
            {
                QualifiedName(name="a.b", source=QualifiedNameSource.IMPORT),
                QualifiedName(name="d.e", source=QualifiedNameSource.IMPORT),
            },
        )
        self.assertEqual(
            scope.get_qualified_names_for("c"),
            {
                QualifiedName(name="a.b", source=QualifiedNameSource.IMPORT),
                QualifiedName(name="d.e", source=QualifiedNameSource.IMPORT),
            },
        )

    def test_assignments_and_accesses(self) -> None:
        m, scopes = get_scope_metadata_provider(
            """
                a = 1
                def f():
                    a = 2
                    a, b
                    def g():
                        b = a
                a
            """
        )
        a_outer_assign = (
            ensure_type(
                ensure_type(m.body[0], cst.SimpleStatementLine).body[0], cst.Assign
            )
            .targets[0]
            .target
        )
        a_outer_access = ensure_type(
            ensure_type(m.body[2], cst.SimpleStatementLine).body[0], cst.Expr
        ).value
        scope_of_module = scopes[a_outer_assign]
        a_outer_assignments = scope_of_module.assignments[a_outer_access]
        self.assertEqual(len(a_outer_assignments), 1)
        a_outer_assignment = list(a_outer_assignments)[0]
        self.assertEqual(cast(Assignment, a_outer_assignment).node, a_outer_assign)
        self.assertEqual(
            {i.node for i in a_outer_assignment.references}, {a_outer_access}
        )

        a_outer_assesses = scope_of_module.accesses[a_outer_assign]
        self.assertEqual(len(a_outer_assesses), 1)
        self.assertEqual(list(a_outer_assesses)[0].node, a_outer_access)

        self.assertEqual(
            {cast(Assignment, i).node for i in list(a_outer_assesses)[0].referents},
            {a_outer_assign},
        )

        self.assertTrue(a_outer_assign in scope_of_module.accesses)
        self.assertTrue(a_outer_assign in scope_of_module.assignments)
        self.assertTrue(a_outer_access in scope_of_module.accesses)
        self.assertTrue(a_outer_access in scope_of_module.assignments)

        f = ensure_type(m.body[1], cst.FunctionDef)
        a_inner_assign = (
            ensure_type(
                ensure_type(
                    ensure_type(f.body, cst.IndentedBlock).body[0],
                    cst.SimpleStatementLine,
                ).body[0],
                cst.Assign,
            )
            .targets[0]
            .target
        )
        scope_of_f = scopes[a_inner_assign]
        a_inner_assignments = scope_of_f.assignments["a"]
        self.assertEqual(len(a_inner_assignments), 1)
        self.assertEqual(
            cast(Assignment, list(a_inner_assignments)[0]).node, a_inner_assign
        )
        tup = ensure_type(
            ensure_type(
                ensure_type(
                    ensure_type(f.body, cst.IndentedBlock).body[1],
                    cst.SimpleStatementLine,
                ).body[0],
                cst.Expr,
            ).value,
            cst.Tuple,
        )
        a_inner_access = tup.elements[0].value
        b_inner_access = tup.elements[1].value
        all_inner_accesses = [i for i in scope_of_f.accesses]
        self.assertEqual(len(all_inner_accesses), 2)
        self.assertEqual(
            {i.node for i in all_inner_accesses}, {a_inner_access, b_inner_access}
        )

        g = ensure_type(ensure_type(f.body, cst.IndentedBlock).body[2], cst.FunctionDef)
        inner_most_assign = ensure_type(
            ensure_type(g.body.body[0], cst.SimpleStatementLine).body[0], cst.Assign
        )
        b_inner_most_assign = inner_most_assign.targets[0].target
        a_inner_most_access = inner_most_assign.value
        scope_of_g = scopes[b_inner_most_assign]
        self.assertEqual({i.node for i in scope_of_g.accesses}, {a_inner_most_access})
        self.assertEqual(
            {cast(Assignment, i).node for i in scope_of_g.assignments},
            {b_inner_most_assign},
        )

        self.assertEqual(len(set(scopes.values())), 3)

    def test_annotation_access(self) -> None:
        m, scopes = get_scope_metadata_provider(
            """
                from typing import Literal, NewType, Optional, TypeVar, Callable, cast
                from a import A, B, C, D, D2, E, E2, F, G, G2, H, I, J, K, K2, L, M
                def x(a: A):
                    pass
                def y(b: "B"):
                    pass
                def z(c: Literal["C"]):
                    pass
                DType = TypeVar("D2", bound=D)
                EType = TypeVar("E2", bound="E")
                FType = TypeVar("F")
                GType = NewType("G2", "Optional[G]")
                HType = Optional["H"]
                IType = Callable[..., I]

                class Test(Generic[J]):
                    pass
                castedK = cast("K", "K2")
                castedL = cast("L", M)
            """
        )
        imp = ensure_type(
            ensure_type(m.body[1], cst.SimpleStatementLine).body[0], cst.ImportFrom
        )
        scope = scopes[imp]

        assignment = list(scope["A"])[0]
        self.assertIsInstance(assignment, Assignment)
        self.assertEqual(len(assignment.references), 1)
        references = list(assignment.references)
        self.assertTrue(references[0].is_annotation)

        assignment = list(scope["B"])[0]
        self.assertIsInstance(assignment, Assignment)
        self.assertEqual(len(assignment.references), 1)
        references = list(assignment.references)
        self.assertTrue(references[0].is_annotation)
        reference_node = references[0].node
        self.assertIsInstance(reference_node, cst.SimpleString)
        if isinstance(reference_node, cst.SimpleString):
            self.assertEqual(reference_node.evaluated_value, "B")

        assignment = list(scope["C"])[0]
        self.assertIsInstance(assignment, Assignment)
        self.assertEqual(len(assignment.references), 0)

        assignment = list(scope["D"])[0]
        self.assertIsInstance(assignment, Assignment)
        self.assertEqual(len(assignment.references), 1)
        references = list(assignment.references)
        self.assertFalse(references[0].is_annotation)
        self.assertTrue(references[0].is_type_hint)

        assignment = list(scope["D2"])[0]
        self.assertIsInstance(assignment, Assignment)
        self.assertEqual(len(assignment.references), 0)

        assignment = list(scope["E"])[0]
        self.assertIsInstance(assignment, Assignment)
        self.assertEqual(len(assignment.references), 1)
        references = list(assignment.references)
        self.assertFalse(references[0].is_annotation)
        self.assertTrue(references[0].is_type_hint)
        reference_node = references[0].node
        self.assertIsInstance(reference_node, cst.SimpleString)
        if isinstance(reference_node, cst.SimpleString):
            self.assertEqual(reference_node.evaluated_value, "E")

        assignment = list(scope["E2"])[0]
        self.assertIsInstance(assignment, Assignment)
        self.assertEqual(len(assignment.references), 0)

        assignment = list(scope["F"])[0]
        self.assertIsInstance(assignment, Assignment)
        self.assertEqual(len(assignment.references), 0)

        assignment = list(scope["G"])[0]
        self.assertIsInstance(assignment, Assignment)
        self.assertEqual(len(assignment.references), 1)
        references = list(assignment.references)
        self.assertFalse(references[0].is_annotation)
        self.assertTrue(references[0].is_type_hint)
        reference_node = references[0].node
        self.assertIsInstance(reference_node, cst.SimpleString)
        if isinstance(reference_node, cst.SimpleString):
            self.assertEqual(reference_node.evaluated_value, "Optional[G]")

        assignment = list(scope["G2"])[0]
        self.assertIsInstance(assignment, Assignment)
        self.assertEqual(len(assignment.references), 0)

        assignment = list(scope["H"])[0]
        self.assertIsInstance(assignment, Assignment)
        self.assertEqual(len(assignment.references), 1)
        references = list(assignment.references)
        self.assertFalse(references[0].is_annotation)
        self.assertTrue(references[0].is_type_hint)
        reference_node = references[0].node
        self.assertIsInstance(reference_node, cst.SimpleString)
        if isinstance(reference_node, cst.SimpleString):
            self.assertEqual(reference_node.evaluated_value, "H")

        assignment = list(scope["I"])[0]
        self.assertIsInstance(assignment, Assignment)
        self.assertEqual(len(assignment.references), 1)
        references = list(assignment.references)
        self.assertFalse(references[0].is_annotation)

        assignment = list(scope["J"])[0]
        self.assertIsInstance(assignment, Assignment)
        self.assertEqual(len(assignment.references), 1)
        references = list(assignment.references)
        self.assertFalse(references[0].is_annotation)

        assignment = list(scope["K"])[0]
        self.assertIsInstance(assignment, Assignment)
        self.assertEqual(len(assignment.references), 1)
        references = list(assignment.references)
        self.assertFalse(references[0].is_annotation)
        reference_node = references[0].node
        self.assertIsInstance(reference_node, cst.SimpleString)
        if isinstance(reference_node, cst.SimpleString):
            self.assertEqual(reference_node.evaluated_value, "K")

        assignment = list(scope["K2"])[0]
        self.assertIsInstance(assignment, Assignment)
        self.assertEqual(len(assignment.references), 0)

        assignment = list(scope["L"])[0]
        self.assertIsInstance(assignment, Assignment)
        self.assertEqual(len(assignment.references), 1)
        references = list(assignment.references)
        reference_node = references[0].node
        self.assertIsInstance(reference_node, cst.SimpleString)
        if isinstance(reference_node, cst.SimpleString):
            self.assertEqual(reference_node.evaluated_value, "L")

        assignment = list(scope["M"])[0]
        self.assertIsInstance(assignment, Assignment)
        self.assertEqual(len(assignment.references), 1)
        references = list(assignment.references)

    def test_insane_annotation_access(self) -> None:
        m, scopes = get_scope_metadata_provider(
            r"""
                from typing import TypeVar, Optional
                from a import G
                TypeVar("G2", bound="Optional[\"G\"]")
            """
        )
        imp = ensure_type(
            ensure_type(m.body[1], cst.SimpleStatementLine).body[0], cst.ImportFrom
        )
        call = ensure_type(
            ensure_type(
                ensure_type(m.body[2], cst.SimpleStatementLine).body[0], cst.Expr
            ).value,
            cst.Call,
        )
        bound = call.args[1].value
        scope = scopes[imp]
        assignment = next(iter(scope["G"]))
        self.assertIsInstance(assignment, Assignment)
        self.assertEqual(len(assignment.references), 1)
        self.assertEqual(list(assignment.references)[0].node, bound)

    def test_dotted_annotation_access(self) -> None:
        m, scopes = get_scope_metadata_provider(
            r"""
                from typing import TypeVar
                import a.G
                TypeVar("G2", bound="a.G")
            """
        )
        imp = ensure_type(
            ensure_type(m.body[1], cst.SimpleStatementLine).body[0], cst.Import
        )
        call = ensure_type(
            ensure_type(
                ensure_type(m.body[2], cst.SimpleStatementLine).body[0], cst.Expr
            ).value,
            cst.Call,
        )
        bound = call.args[1].value
        scope = scopes[imp]
        assignment = next(iter(scope["a.G"]))
        self.assertIsInstance(assignment, Assignment)
        self.assertEqual(len(assignment.references), 1)
        self.assertEqual(list(assignment.references)[0].node, bound)

    def test_node_of_scopes(self) -> None:
        m, scopes = get_scope_metadata_provider(
            """
                def f1():
                    target()

                class C:
                    attr = target()
            """
        )
        f1 = ensure_type(m.body[0], cst.FunctionDef)
        target_call = ensure_type(
            ensure_type(f1.body.body[0], cst.SimpleStatementLine).body[0], cst.Expr
        ).value
        f1_scope = scopes[target_call]
        self.assertIsInstance(f1_scope, FunctionScope)
        self.assertEqual(cast(FunctionScope, f1_scope).node, f1)
        c = ensure_type(m.body[1], cst.ClassDef)
        target_call_2 = ensure_type(
            ensure_type(c.body.body[0], cst.SimpleStatementLine).body[0], cst.Assign
        ).value
        c_scope = scopes[target_call_2]
        self.assertIsInstance(c_scope, ClassScope)
        self.assertEqual(cast(ClassScope, c_scope).node, c)

    def test_with_statement(self) -> None:
        m, scopes = get_scope_metadata_provider(
            """
                import unittest.mock

                with unittest.mock.patch("something") as obj:
                    obj.f1()

                unittest.mock
            """
        )
        import_ = ensure_type(m.body[0], cst.SimpleStatementLine).body[0]
        assignments = scopes[import_]["unittest"]
        self.assertEqual(len(assignments), 1)
        self.assertEqual(cast(Assignment, list(assignments)[0]).node, import_)
        with_ = ensure_type(m.body[1], cst.With)
        fn_call = with_.items[0].item
        self.assertEqual(
            scopes[fn_call].get_qualified_names_for(fn_call),
            {
                QualifiedName(
                    name="unittest.mock.patch", source=QualifiedNameSource.IMPORT
                )
            },
        )
        mock = ensure_type(
            ensure_type(m.body[2], cst.SimpleStatementLine).body[0], cst.Expr
        ).value
        self.assertEqual(
            scopes[fn_call].get_qualified_names_for(mock),
            {QualifiedName(name="unittest.mock", source=QualifiedNameSource.IMPORT)},
        )

    def test_del_context_names(self) -> None:
        m, scopes = get_scope_metadata_provider(
            """
                import a
                dic = {}
                del dic
                del dic["key"]
                del a.b
            """
        )
        dic = ensure_type(
            ensure_type(
                ensure_type(m.body[1], cst.SimpleStatementLine).body[0], cst.Assign
            ).targets[0],
            cst.AssignTarget,
        ).target
        del_dic = ensure_type(
            ensure_type(m.body[2], cst.SimpleStatementLine).body[0], cst.Del
        )
        scope = scopes[del_dic]
        assignments = list(scope["dic"])
        self.assertEqual(len(assignments), 1)
        dic_assign = assignments[0]
        self.assertIsInstance(dic_assign, Assignment)
        self.assertEqual(cast(Assignment, dic_assign).node, dic)
        self.assertEqual(len(dic_assign.references), 2)
        del_dic_subscript = ensure_type(
            ensure_type(
                ensure_type(m.body[3], cst.SimpleStatementLine).body[0], cst.Del
            ).target,
            cst.Subscript,
        )
        self.assertSetEqual(
            {i.node for i in dic_assign.references},
            {del_dic.target, del_dic_subscript.value},
        )
        assignments = list(scope["a"])
        self.assertEqual(len(assignments), 1)
        a_assign = assignments[0]
        self.assertIsInstance(a_assign, Assignment)
        import_a = ensure_type(m.body[0], cst.SimpleStatementLine).body[0]
        self.assertEqual(cast(Assignment, a_assign).node, import_a)
        self.assertEqual(len(a_assign.references), 1)
        del_a_b = ensure_type(
            ensure_type(m.body[4], cst.SimpleStatementLine).body[0], cst.Del
        )
        self.assertEqual(
            {i.node for i in a_assign.references},
            {ensure_type(del_a_b.target, cst.Attribute).value},
        )
        self.assertEqual(scope["b"], set())

    def test_keyword_arg_in_call(self) -> None:
        m, scopes = get_scope_metadata_provider("call(arg=val)")
        call = ensure_type(
            ensure_type(m.body[0], cst.SimpleStatementLine).body[0], cst.Expr
        ).value
        scope = scopes[call]
        self.assertIsInstance(scope, GlobalScope)
        self.assertEqual(len(scope["arg"]), 0)  # no assignment should exist

    def test_global_contains_is_read_only(self) -> None:
        gscope = GlobalScope()
        before_assignments = list(gscope.assignments)
        before_accesses = list(gscope.accesses)
        self.assertFalse("doesnt_exist" in gscope)
        self.assertEqual(list(gscope.accesses), before_accesses)
        self.assertEqual(list(gscope.assignments), before_assignments)

    def test_contains_is_read_only(self) -> None:
        for s in [LocalScope, FunctionScope, ClassScope, ComprehensionScope]:
            with self.subTest(scope=s):
                gscope = GlobalScope()
                scope = s(parent=gscope, node=cst.Name("lol"))
                before_assignments = list(scope.assignments)
                before_accesses = list(scope.accesses)
                before_overwrites = list(scope._scope_overwrites.items())
                before_parent_assignments = list(scope.parent.assignments)
                before_parent_accesses = list(scope.parent.accesses)

                self.assertFalse("doesnt_exist" in scope)
                self.assertEqual(list(scope.accesses), before_accesses)
                self.assertEqual(list(scope.assignments), before_assignments)
                self.assertEqual(
                    list(scope._scope_overwrites.items()), before_overwrites
                )
                self.assertEqual(
                    list(scope.parent.assignments), before_parent_assignments
                )
                self.assertEqual(list(scope.parent.accesses), before_parent_accesses)

    def test_attribute_of_function_call(self) -> None:
        get_scope_metadata_provider("foo().bar")

    def test_attribute_of_subscript_called(self) -> None:
        m, scopes = get_scope_metadata_provider("foo[0].bar.baz()")
        scope = scopes[m]
        self.assertIn("foo", scope.accesses)

    def test_self(self) -> None:
        with open(__file__) as f:
            get_scope_metadata_provider(f.read())

    def test_get_qualified_names_for_is_read_only(self) -> None:
        m, scopes = get_scope_metadata_provider(
            """
                import a
                import b
            """
        )
        a = m.body[0]
        scope = scopes[a]
        assignments_before = list(scope.assignments)
        accesses_before = list(scope.accesses)
        scope.get_qualified_names_for("doesnt_exist")
        self.assertEqual(list(scope.assignments), assignments_before)
        self.assertEqual(list(scope.accesses), accesses_before)

    def test_gen_dotted_names(self) -> None:
        names = {name for name, node in _gen_dotted_names(cst.Name(value="a"))}
        self.assertEqual(names, {"a"})

        names = {
            name
            for name, node in _gen_dotted_names(
                cst.Attribute(value=cst.Name(value="a"), attr=cst.Name(value="b"))
            )
        }
        self.assertEqual(names, {"a.b", "a"})

        names = {
            name
            for name, node in _gen_dotted_names(
                cst.Attribute(
                    value=cst.Call(
                        func=cst.Attribute(
                            value=cst.Attribute(
                                value=cst.Name(value="a"), attr=cst.Name(value="b")
                            ),
                            attr=cst.Name(value="c"),
                        ),
                        args=[],
                    ),
                    attr=cst.Name(value="d"),
                )
            )
        }
        self.assertEqual(names, {"a.b.c", "a.b", "a"})

    def test_ordering(self) -> None:
        m, scopes = get_scope_metadata_provider(
            """
            from a import b
            class X:
                x = b
                b = b
                y = b
            """
        )
        global_scope = scopes[m]
        import_stmt = ensure_type(
            ensure_type(m.body[0], cst.SimpleStatementLine).body[0], cst.ImportFrom
        )
        first_assignment = list(global_scope.assignments)[0]
        assert isinstance(first_assignment, cst.metadata.Assignment)
        self.assertEqual(first_assignment.node, import_stmt)
        global_refs = first_assignment.references
        self.assertEqual(len(global_refs), 2)
        global_refs_nodes = {x.node for x in global_refs}
        class_def = ensure_type(m.body[1], cst.ClassDef)
        x = ensure_type(
            ensure_type(class_def.body.body[0], cst.SimpleStatementLine).body[0],
            cst.Assign,
        )
        self.assertIn(x.value, global_refs_nodes)
        class_b = ensure_type(
            ensure_type(class_def.body.body[1], cst.SimpleStatementLine).body[0],
            cst.Assign,
        )
        self.assertIn(class_b.value, global_refs_nodes)

        class_accesses = list(scopes[x].accesses)
        self.assertEqual(len(class_accesses), 3)
        self.assertIn(
            class_b.targets[0].target,
            [
                ref.node
                for acc in class_accesses
                for ref in acc.referents
                if isinstance(ref, Assignment)
            ],
        )
        y = ensure_type(
            ensure_type(class_def.body.body[2], cst.SimpleStatementLine).body[0],
            cst.Assign,
        )
        self.assertIn(y.value, [access.node for access in class_accesses])

    def test_ordering_between_scopes(self) -> None:
        m, scopes = get_scope_metadata_provider(
            """
            def f(a):
                print(a)
                print(b)
            a = 1
            b = 1
            """
        )
        f = cst.ensure_type(m.body[0], cst.FunctionDef)
        a_param = f.params.params[0].name
        a_param_assignment = list(scopes[a_param]["a"])[0]
        a_param_refs = list(a_param_assignment.references)
        first_print = cst.ensure_type(
            cst.ensure_type(
                cst.ensure_type(f.body.body[0], cst.SimpleStatementLine).body[0],
                cst.Expr,
            ).value,
            cst.Call,
        )
        second_print = cst.ensure_type(
            cst.ensure_type(
                cst.ensure_type(f.body.body[1], cst.SimpleStatementLine).body[0],
                cst.Expr,
            ).value,
            cst.Call,
        )
        self.assertEqual(
            first_print.args[0].value,
            a_param_refs[0].node,
        )
        a_global = (
            cst.ensure_type(
                cst.ensure_type(m.body[1], cst.SimpleStatementLine).body[0], cst.Assign
            )
            .targets[0]
            .target
        )
        a_global_assignment = list(scopes[a_global]["a"])[0]
        a_global_refs = list(a_global_assignment.references)
        self.assertEqual(a_global_refs, [])
        b_global = (
            cst.ensure_type(
                cst.ensure_type(m.body[2], cst.SimpleStatementLine).body[0], cst.Assign
            )
            .targets[0]
            .target
        )
        b_global_assignment = list(scopes[b_global]["b"])[0]
        b_global_refs = list(b_global_assignment.references)
        self.assertEqual(len(b_global_refs), 1)
        self.assertEqual(b_global_refs[0].node, second_print.args[0].value)

    def test_ordering_comprehension(self) -> None:
        m, scopes = get_scope_metadata_provider(
            """
            def f(a):
                [a for a in [] for b in a]
                [b for a in [] for b in a]
                [a for a in [] for a in []]
            a = 1
            """
        )
        f = cst.ensure_type(m.body[0], cst.FunctionDef)
        a_param = f.params.params[0].name
        a_param_assignment = list(scopes[a_param]["a"])[0]
        a_param_refs = list(a_param_assignment.references)
        self.assertEqual(a_param_refs, [])
        first_comp = cst.ensure_type(
            cst.ensure_type(
                cst.ensure_type(f.body.body[0], cst.SimpleStatementLine).body[0],
                cst.Expr,
            ).value,
            cst.ListComp,
        )
        a_comp_assignment = list(scopes[first_comp.elt]["a"])[0]
        self.assertEqual(len(a_comp_assignment.references), 2)
        self.assertIn(
            first_comp.elt, [ref.node for ref in a_comp_assignment.references]
        )

        second_comp = cst.ensure_type(
            cst.ensure_type(
                cst.ensure_type(f.body.body[1], cst.SimpleStatementLine).body[0],
                cst.Expr,
            ).value,
            cst.ListComp,
        )
        b_comp_assignment = list(scopes[second_comp.elt]["b"])[0]
        self.assertEqual(len(b_comp_assignment.references), 1)
        a_second_comp_assignment = list(scopes[second_comp.elt]["a"])[0]
        self.assertEqual(len(a_second_comp_assignment.references), 1)

        third_comp = cst.ensure_type(
            cst.ensure_type(
                cst.ensure_type(f.body.body[2], cst.SimpleStatementLine).body[0],
                cst.Expr,
            ).value,
            cst.ListComp,
        )
        a_third_comp_assignments = list(scopes[third_comp.elt]["a"])
        self.assertEqual(len(a_third_comp_assignments), 2)
        a_third_comp_access = list(scopes[third_comp.elt].accesses)[0]
        self.assertEqual(a_third_comp_access.node, third_comp.elt)
        # We record both assignments because it's impossible to know which one
        # the access refers to without running the program
        self.assertEqual(len(a_third_comp_access.referents), 2)
        inner_for_in = third_comp.for_in.inner_for_in
        self.assertIsNotNone(inner_for_in)
        if inner_for_in:
            self.assertIn(
                inner_for_in.target,
                {
                    ref.node
                    for ref in a_third_comp_access.referents
                    if isinstance(ref, Assignment)
                },
            )

        a_global = (
            cst.ensure_type(
                cst.ensure_type(m.body[1], cst.SimpleStatementLine).body[0], cst.Assign
            )
            .targets[0]
            .target
        )
        a_global_assignment = list(scopes[a_global]["a"])[0]
        a_global_refs = list(a_global_assignment.references)
        self.assertEqual(a_global_refs, [])

    def test_ordering_comprehension_confusing(self) -> None:
        m, scopes = get_scope_metadata_provider(
            """
            def f(a):
                [a for a in a]
            a = 1
            """
        )
        f = cst.ensure_type(m.body[0], cst.FunctionDef)
        a_param = f.params.params[0].name
        a_param_assignment = list(scopes[a_param]["a"])[0]
        a_param_refs = list(a_param_assignment.references)
        self.assertEqual(len(a_param_refs), 1)
        comp = cst.ensure_type(
            cst.ensure_type(
                cst.ensure_type(f.body.body[0], cst.SimpleStatementLine).body[0],
                cst.Expr,
            ).value,
            cst.ListComp,
        )
        a_comp_assignment = list(scopes[comp.elt]["a"])[0]
        self.assertEqual(list(a_param_refs)[0].node, comp.for_in.iter)
        self.assertEqual(len(a_comp_assignment.references), 1)
        self.assertEqual(list(a_comp_assignment.references)[0].node, comp.elt)

    def test_for_scope_ordering(self) -> None:
        m, scopes = get_scope_metadata_provider(
            """
            def f():
                for x in []:
                    x
            class X:
                def f():
                    for x in []:
                        x
            """
        )
        for scope in scopes.values():
            for acc in scope.accesses:
                self.assertEqual(
                    len(acc.referents),
                    1,
                    msg=(
                        "Access for node has incorrect number of referents: "
                        + f"{acc.node}"
                    ),
                )

    def test_no_out_of_order_references_in_global_scope(self) -> None:
        m, scopes = get_scope_metadata_provider(
            """
            x = y
            y = 1
            """
        )
        for scope in scopes.values():
            for acc in scope.accesses:
                self.assertEqual(
                    len(acc.referents),
                    0,
                    msg=(
                        "Access for node has incorrect number of referents: "
                        + f"{acc.node}"
                    ),
                )

    def test_walrus_accesses(self) -> None:
        if sys.version_info < (3, 8):
            self.skipTest("This python version doesn't support :=")
        m, scopes = get_scope_metadata_provider(
            """
            if x := y:
                y = 1
                x
            """
        )
        for scope in scopes.values():
            for acc in scope.accesses:
                self.assertEqual(
                    len(acc.referents),
                    1 if getattr(acc.node, "value", None) == "x" else 0,
                    msg=(
                        "Access for node has incorrect number of referents: "
                        + f"{acc.node}"
                    ),
                )

    @data_provider(
        {
            "TypeVar": {
                "code": """
                    from typing import TypeVar
                    TypeVar("Name", "int")
                """,
                "calls": [mock.call("int")],
            },
            "Dict": {
                "code": """
                    from typing import Dict
                    Dict["str", "int"]
                """,
                "calls": [mock.call("str"), mock.call("int")],
            },
            "cast_no_annotation": {
                "code": """
                    from typing import Dict, cast
                    cast(Dict[str, str], {})["3rr0r"]
                """,
                "calls": [],
            },
            "cast_second_arg": {
                "code": """
                    from typing import cast
                    cast(str, "foo")
                """,
                "calls": [],
            },
            "cast_first_arg": {
                "code": """
                    from typing import cast
                    cast("int", "foo")
                """,
                "calls": [
                    mock.call("int"),
                ],
            },
            "typevar_func": {
                "code": """
                    from typing import TypeVar
                    TypeVar("Name", func("int"))
                """,
                "calls": [],
            },
            "literal": {
                "code": """
                    from typing import Literal
                    Literal[\"G\"]
                """,
                "calls": [],
            },
            "nested_str": {
                "code": r"""
                    from typing import TypeVar, Optional
                    from a import G
                    TypeVar("G2", bound="Optional[\"G\"]")
                """,
                "calls": [mock.call('Optional["G"]'), mock.call("G")],
            },
            "class_self_ref": {
                "code": """
                    from typing import TypeVar
                    class HelperClass:
                        value: TypeVar("THelperClass", bound="HelperClass")
                """,
                "calls": [mock.call("HelperClass")],
            },
        }
    )
    def test_parse_string_annotations(
        self, *, code: str, calls: Sequence[mock._Call]
    ) -> None:
        parse = cst.parse_module
        with mock.patch("libcst.parse_module") as parse_mock:
            parse_mock.side_effect = parse
            get_scope_metadata_provider(dedent(code))
            calls = [mock.call(dedent(code))] + list(calls)
            self.assertEqual(parse_mock.call_count, len(calls))
            parse_mock.assert_has_calls(calls)

    def test_builtin_scope(self) -> None:
        m, scopes = get_scope_metadata_provider(
            """
            a = pow(1, 2)
            def foo():
                b = pow(2, 3)
            """
        )
        scope_of_module = scopes[m]
        self.assertIsInstance(scope_of_module, GlobalScope)
        self.assertEqual(len(scope_of_module["pow"]), 1)
        builtin_pow_assignment = list(scope_of_module["pow"])[0]
        self.assertIsInstance(builtin_pow_assignment, BuiltinAssignment)
        self.assertIsInstance(builtin_pow_assignment.scope, BuiltinScope)

        global_a_assignments = scope_of_module["a"]
        self.assertEqual(len(global_a_assignments), 1)
        a_assignment = list(global_a_assignments)[0]
        self.assertIsInstance(a_assignment, Assignment)

        func_body = ensure_type(m.body[1], cst.FunctionDef).body
        func_statement = func_body.body[0]
        scope_of_func_statement = scopes[func_statement]
        self.assertIsInstance(scope_of_func_statement, FunctionScope)
        func_b_assignments = scope_of_func_statement["b"]
        self.assertEqual(len(func_b_assignments), 1)
        b_assignment = list(func_b_assignments)[0]
        self.assertIsInstance(b_assignment, Assignment)

        builtin_pow_accesses = list(builtin_pow_assignment.references)
        self.assertEqual(len(builtin_pow_accesses), 2)

    def test_override_builtin_scope(self) -> None:
        m, scopes = get_scope_metadata_provider(
            """
            def pow(x, y):
                return x ** y

            a = pow(1, 2)
            def foo():
                b = pow(2, 3)
            """
        )
        scope_of_module = scopes[m]
        self.assertIsInstance(scope_of_module, GlobalScope)
        self.assertEqual(len(scope_of_module["pow"]), 1)
        global_pow_assignment = list(scope_of_module["pow"])[0]
        self.assertIsInstance(global_pow_assignment, Assignment)
        self.assertIsInstance(global_pow_assignment.scope, GlobalScope)

        global_a_assignments = scope_of_module["a"]
        self.assertEqual(len(global_a_assignments), 1)
        a_assignment = list(global_a_assignments)[0]
        self.assertIsInstance(a_assignment, Assignment)

        func_body = ensure_type(m.body[2], cst.FunctionDef).body
        func_statement = func_body.body[0]
        scope_of_func_statement = scopes[func_statement]
        self.assertIsInstance(scope_of_func_statement, FunctionScope)
        func_b_assignments = scope_of_func_statement["b"]
        self.assertEqual(len(func_b_assignments), 1)
        b_assignment = list(func_b_assignments)[0]
        self.assertIsInstance(b_assignment, Assignment)

        global_pow_accesses = list(global_pow_assignment.references)
        self.assertEqual(len(global_pow_accesses), 2)

    def test_annotation_access_in_typevar_bound(self) -> None:
        m, scopes = get_scope_metadata_provider(
            """
                from typing import TypeVar
                class Test:
                    var: TypeVar("T", bound="Test")
            """
        )
        imp = ensure_type(
            ensure_type(m.body[0], cst.SimpleStatementLine).body[0], cst.ImportFrom
        )
        scope = scopes[imp]
        assignment = list(scope["Test"])[0]
        self.assertIsInstance(assignment, Assignment)
        self.assertEqual(len(assignment.references), 1)
        references = list(assignment.references)
        self.assertTrue(references[0].is_annotation)

    def test_prefix_match(self) -> None:
        """Verify that a name doesn't overmatch on prefix"""
        m, scopes = get_scope_metadata_provider(
            """
                def something():
                    ...
            """
        )
        scope = scopes[m]
        self.assertEqual(
            scope.get_qualified_names_for(cst.Name("something")),
            {QualifiedName(name="something", source=QualifiedNameSource.LOCAL)},
        )
        self.assertEqual(
            scope.get_qualified_names_for(cst.Name("something_else")),
            set(),
        )

    def test_type_alias_scope(self) -> None:
        if not is_native():
            self.skipTest("type aliases are only supported in the native parser")
        m, scopes = get_scope_metadata_provider(
            """
                type A = C
                lol: A
            """
        )
        alias = ensure_type(
            ensure_type(m.body[0], cst.SimpleStatementLine).body[0], cst.TypeAlias
        )
        self.assertIsInstance(scopes[alias], GlobalScope)
        a_assignments = list(scopes[alias]["A"])
        self.assertEqual(len(a_assignments), 1)
        lol = ensure_type(
            ensure_type(m.body[1], cst.SimpleStatementLine).body[0], cst.AnnAssign
        )
        self.assertEqual(len(a_references := list(a_assignments[0].references)), 1)
        self.assertEqual(a_references[0].node, lol.annotation.annotation)

        self.assertIsInstance(scopes[alias.value], AnnotationScope)

    def test_type_alias_param(self) -> None:
        if not is_native():
            self.skipTest("type parameters are only supported in the native parser")
        m, scopes = get_scope_metadata_provider(
            """
                B = int
                type A[T: B] = T
                lol: T
            """
        )
        alias = ensure_type(
            ensure_type(m.body[1], cst.SimpleStatementLine).body[0], cst.TypeAlias
        )
        assert alias.type_parameters
        param_scope = scopes[alias.type_parameters]
        self.assertEqual(len(t_assignments := list(param_scope["T"])), 1)
        self.assertEqual(len(t_refs := list(t_assignments[0].references)), 1)
        self.assertEqual(t_refs[0].node, alias.value)

        b = (
            ensure_type(
                ensure_type(m.body[0], cst.SimpleStatementLine).body[0], cst.Assign
            )
            .targets[0]
            .target
        )
        b_assignment = list(scopes[b]["B"])[0]
        self.assertEqual(
            {ref.node for ref in b_assignment.references},
            {ensure_type(alias.type_parameters.params[0].param, cst.TypeVar).bound},
        )

    def test_type_alias_tuple_and_paramspec(self) -> None:
        if not is_native():
            self.skipTest("type parameters are only supported in the native parser")
        m, scopes = get_scope_metadata_provider(
            """
            type A[*T] = T
            lol: T
            type A[**T] = T
            lol: T
            """
        )
        alias_tuple = ensure_type(
            ensure_type(m.body[0], cst.SimpleStatementLine).body[0], cst.TypeAlias
        )
        assert alias_tuple.type_parameters
        param_scope = scopes[alias_tuple.type_parameters]
        self.assertEqual(len(t_assignments := list(param_scope["T"])), 1)
        self.assertEqual(len(t_refs := list(t_assignments[0].references)), 1)
        self.assertEqual(t_refs[0].node, alias_tuple.value)

        alias_paramspec = ensure_type(
            ensure_type(m.body[2], cst.SimpleStatementLine).body[0], cst.TypeAlias
        )
        assert alias_paramspec.type_parameters
        param_scope = scopes[alias_paramspec.type_parameters]
        self.assertEqual(len(t_assignments := list(param_scope["T"])), 1)
        self.assertEqual(len(t_refs := list(t_assignments[0].references)), 1)
        self.assertEqual(t_refs[0].node, alias_paramspec.value)

    def test_class_type_params(self) -> None:
        if not is_native():
            self.skipTest("type parameters are only supported in the native parser")
        m, scopes = get_scope_metadata_provider(
            """
            class W[T]:
                def f() -> T: pass
                def g[T]() -> T: pass
            """
        )
        cls = ensure_type(m.body[0], cst.ClassDef)
        cls_scope = scopes[cls.body.body[0]]
        self.assertEqual(len(t_assignments_in_cls := list(cls_scope["T"])), 1)
        assert cls.type_parameters
        self.assertEqual(
            ensure_type(t_assignments_in_cls[0], Assignment).node,
            cls.type_parameters.params[0].param,
        )
        self.assertEqual(
            len(t_refs_in_cls := list(t_assignments_in_cls[0].references)), 1
        )
        f = ensure_type(cls.body.body[0], cst.FunctionDef)
        assert f.returns
        self.assertEqual(t_refs_in_cls[0].node, f.returns.annotation)

        g = ensure_type(cls.body.body[1], cst.FunctionDef)
        assert g.type_parameters
        assert g.returns
        self.assertEqual(len(t_assignments_in_g := list(scopes[g.body]["T"])), 1)
        self.assertEqual(
            ensure_type(t_assignments_in_g[0], Assignment).node,
            g.type_parameters.params[0].param,
        )
        self.assertEqual(len(t_refs_in_g := list(t_assignments_in_g[0].references)), 1)
        self.assertEqual(t_refs_in_g[0].node, g.returns.annotation)

    def test_nested_class_type_params(self) -> None:
        if not is_native():
            self.skipTest("type parameters are only supported in the native parser")
        m, scopes = get_scope_metadata_provider(
            """
            class Outer:
                class Nested[T: Outer]: pass
            """
        )
        outer = ensure_type(m.body[0], cst.ClassDef)
        outer_refs = list(list(scopes[outer]["Outer"])[0].references)
        self.assertEqual(len(outer_refs), 1)
        inner = ensure_type(outer.body.body[0], cst.ClassDef)
        assert inner.type_parameters
        self.assertEqual(
            outer_refs[0].node,
            ensure_type(inner.type_parameters.params[0].param, cst.TypeVar).bound,
        )

    def test_annotation_refers_to_nested_class(self) -> None:
        if not is_native():
            self.skipTest("type parameters are only supported in the native parser")
        m, scopes = get_scope_metadata_provider(
            """
                class Outer:
                    class Nested:
                        pass
                    
                    type Alias = Nested

                    def meth1[T: Nested](self): pass
                    def meth2[T](self, arg: Nested): pass
            """
        )
        outer = ensure_type(m.body[0], cst.ClassDef)
        nested = ensure_type(outer.body.body[0], cst.ClassDef)
        alias = ensure_type(
            ensure_type(outer.body.body[1], cst.SimpleStatementLine).body[0],
            cst.TypeAlias,
        )
        self.assertIsInstance(scopes[alias.value], AnnotationScope)
        nested_refs_within_alias = list(scopes[alias.value].accesses["Nested"])
        self.assertEqual(len(nested_refs_within_alias), 1)
        self.assertEqual(
            {
                ensure_type(ref, Assignment).node
                for ref in nested_refs_within_alias[0].referents
            },
            {nested},
        )

        meth1 = ensure_type(outer.body.body[2], cst.FunctionDef)
        self.assertIsInstance(scopes[meth1], ClassScope)
        assert meth1.type_parameters
        meth1_typevar = ensure_type(meth1.type_parameters.params[0].param, cst.TypeVar)
        meth1_typevar_scope = scopes[meth1_typevar]
        self.assertIsInstance(meth1_typevar_scope, AnnotationScope)
        nested_refs_within_meth1 = list(meth1_typevar_scope.accesses["Nested"])
        self.assertEqual(len(nested_refs_within_meth1), 1)
        self.assertEqual(
            {
                ensure_type(ref, Assignment).node
                for ref in nested_refs_within_meth1[0].referents
            },
            {nested},
        )

        meth2 = ensure_type(outer.body.body[3], cst.FunctionDef)
        meth2_annotation = meth2.params.params[1].annotation
        assert meth2_annotation
        nested_refs_within_meth2 = list(scopes[meth2_annotation].accesses["Nested"])
        self.assertEqual(len(nested_refs_within_meth2), 1)
        self.assertEqual(
            {
                ensure_type(ref, Assignment).node
                for ref in nested_refs_within_meth2[0].referents
            },
            {nested},
        )

    def test_body_isnt_subject_to_special_annotation_rule(self) -> None:
        if not is_native():
            self.skipTest("type parameters are only supported in the native parser")
        m, scopes = get_scope_metadata_provider(
            """
            class Outer:
                class Inner: pass
                def f[T: Inner](self): Inner
            """
        )
        outer = ensure_type(m.body[0], cst.ClassDef)
        # note: this is different from global scope
        outer_scope = scopes[outer.body.body[0]]
        inner_assignment = list(outer_scope["Inner"])[0]
        self.assertEqual(len(inner_assignment.references), 1)
        f = ensure_type(outer.body.body[1], cst.FunctionDef)
        assert f.type_parameters
        T = ensure_type(f.type_parameters.params[0].param, cst.TypeVar)
        self.assertIs(list(inner_assignment.references)[0].node, T.bound)

        inner_in_func_body = ensure_type(f.body.body[0], cst.Expr)
        f_scope = scopes[inner_in_func_body]
        self.assertIn(inner_in_func_body.value, f_scope.accesses)
        self.assertEqual(list(f_scope.accesses)[0].referents, set())
