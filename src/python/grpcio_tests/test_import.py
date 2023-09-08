# from __future__ import absolute_import

# import importlib
# import unittest
# import re
# import os
# import sys
# import pkgutil

# import _runner

# names = ["tests"]
# TEST_MODULE_REGEX = r"^.*_test$"
# module_matcher = re.compile(TEST_MODULE_REGEX)
# loader = unittest.TestLoader()
# suite = unittest.TestSuite()

# imported_modules = tuple(
#     importlib.import_module(name) for name in names
# )
# print(f"imported_modules: {imported_modules}")

# def _relativize_to_sys_path(path):
#     for sys_path in sys.path:
#         if path.startswith(sys_path):
#             relative = path[len(sys_path) :]
#             if not relative:
#                 return ""
#             if relative.startswith(os.path.sep):
#                 relative = relative[len(os.path.sep) :]
#             if not relative.endswith(os.path.sep):
#                 relative += os.path.sep
#             return relative
#     raise AssertionError("Failed to relativize {} to sys.path.".format(path))

# def _relative_path_to_module_prefix(path):
#     return path.replace(os.path.sep, ".")

# def visit_module(module):
#     """Visits the module, adding discovered tests to the test suite.

#     Args:
#         module (module): Module to match against self.module_matcher; if matched
#         it has its tests loaded via self.loader into self.suite.
#     """
#     if module_matcher.match(module.__name__):
#         import sys; sys.stderr.write(f"_____ matched module: {module.__name__}\n"); sys.stderr.flush()
#         module_suite = loader.loadTestsFromModule(module)
#         import sys; sys.stderr.write(f"_____ loaded module suite: {module_suite}\n"); sys.stderr.flush()
#         suite.addTest(module_suite)

# def _walk_package(package_path):
#     prefix = _relative_path_to_module_prefix(
#         _relativize_to_sys_path(package_path)
#     )
#     print(f"walk_packages with path: {package_path}, prefix: {prefix}")
#     for importer, module_name, is_package in pkgutil.walk_packages(
#         [package_path], prefix
#     ):
#         # print(f"finding module with name: {module_name}")
#         if "observability" in module_name:
#             found_module = importer.find_module(module_name)
#             print(f"found_module: {found_module}")
#             module = None
#             if module_name in sys.modules:
#                 module = sys.modules[module_name]
#             else:
#                 module = found_module.load_module(module_name)
#             print(f"module: {module}")
#             visit_module(module)


# for imported_module in imported_modules:
#     print(f"imported_module.__name__: {imported_module.__name__}")
#     print(f"imported_module.__path__: {imported_module.__path__}")
#     visit_module(imported_module)
#     for imported_module in imported_modules:
#         try:
#             package_paths = imported_module.__path__
#         except AttributeError:
#             continue
#         for path in package_paths:
#             _walk_package(path)


# runner = _runner.Runner(dedicated_threads=True)
# result = runner.run(suite)


import pkgutil
import unittest

import tests

maxDiff = 32768

TEST_PKG_MODULE_NAME = "tests"
TEST_PKG_PATH = "tests"


def testTestsJsonUpToDate():
    """Autodiscovers all test suites and checks that tests.json is up to date"""
    loader = tests.Loader()
    loader.loadTestsFromNames([TEST_PKG_MODULE_NAME])
    test_suite_names = sorted(
        {
            test_case_class.id().rsplit(".", 1)[0]
            for test_case_class in tests._loader.iterate_suite_cases(
                loader.suite
            )
        }
    )
    for name in test_suite_names:
        print(f"test_suite_name: {name}")

    tests_json_string = pkgutil.get_data(TEST_PKG_PATH, "tests.json")
    tests_json = tests_json_string.decode()
    print(f"tests_json: {tests_json}")


testTestsJsonUpToDate()
