# Copyright 2023 The gRPC Authors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

load("//bazel:grpc_build_system.bzl", "grpc_package")
load("//tools/bazelify_tests:build_defs.bzl", "grpc_run_bazel_distribtest_test", "grpc_run_cpp_distribtest_test", "grpc_run_tests_py_test")
load(":portability_tests.bzl", "generate_run_tests_portability_tests")

licenses(["notice"])

grpc_package(name = "tools/bazelify_tests/test")

generate_run_tests_portability_tests(name = "portability_tests_linux")

# C/C++
grpc_run_tests_py_test(
    name = "runtests_c_linux_dbg",
    size = "enormous",
    args = [
        "-l c -c dbg",
    ],
    docker_image_version = "tools/dockerfile/test/cxx_debian11_x64.current_version",
)

grpc_run_tests_py_test(
    name = "runtests_c_linux_opt",
    size = "enormous",
    args = [
        "-l c -c opt",
    ],
    docker_image_version = "tools/dockerfile/test/cxx_debian11_x64.current_version",
)

grpc_run_tests_py_test(
    name = "runtests_cpp_linux_dbg_build_only",
    size = "enormous",
    args = [
        "-l c++ -c dbg --build_only",
    ],
    docker_image_version = "tools/dockerfile/test/cxx_debian11_x64.current_version",
)

grpc_run_tests_py_test(
    name = "runtests_cpp_linux_opt_build_only",
    size = "enormous",
    args = [
        "-l c++ -c opt --build_only",
    ],
    docker_image_version = "tools/dockerfile/test/cxx_debian11_x64.current_version",
)

# TODO(jtattermusch): Reintroduce ruby tests once they pass.
# # Ruby
# grpc_run_tests_py_test(
#     name = "runtests_ruby_linux_dbg",
#     size = "enormous",
#     args = [
#         "-l ruby -c dbg",
#     ],
#     docker_image_version = "tools/dockerfile/test/ruby_debian11_x64.current_version",
#     prepare_script = ":prepare_ruby.sh",
#     use_login_shell = True,  # ruby's docker image uses RVM which wierdly requires login shell
# )

# grpc_run_tests_py_test(
#     name = "runtests_ruby_linux_opt",
#     size = "enormous",
#     args = [
#         "-l ruby -c opt",
#     ],
#     docker_image_version = "tools/dockerfile/test/ruby_debian11_x64.current_version",
#     prepare_script = ":prepare_ruby.sh",
#     use_login_shell = True,  # ruby's docker image uses RVM which wierdly requires login shell
# )

# PHP
grpc_run_tests_py_test(
    name = "runtests_php_linux_dbg",
    size = "enormous",
    args = [
        "-l php7 -c dbg",
    ],
    docker_image_version = "tools/dockerfile/test/php7_debian11_x64.current_version",
)

grpc_run_tests_py_test(
    name = "runtests_php_linux_opt",
    size = "enormous",
    args = [
        "-l php7 -c opt",
    ],
    docker_image_version = "tools/dockerfile/test/php7_debian11_x64.current_version",
)

# TODO(jtattermusch): Reintroduce python tests once they pass.
# # Python
# grpc_run_tests_py_test(
#     name = "runtests_python_linux_opt",
#     size = "enormous",
#     args = [
#         "-l python -c opt",
#     ],
#     docker_image_version = "tools/dockerfile/test/python_debian11_default_x64.current_version",
# )

# C#
grpc_run_tests_py_test(
    name = "runtests_csharp_linux_dbg",
    size = "enormous",
    args = [
        "-l csharp -c dbg",
    ],
    docker_image_version = "tools/dockerfile/test/csharp_debian11_x64.current_version",
)

grpc_run_tests_py_test(
    name = "runtests_csharp_linux_opt",
    size = "enormous",
    args = [
        "-l csharp -c opt",
    ],
    docker_image_version = "tools/dockerfile/test/csharp_debian11_x64.current_version",
)

test_suite(
    name = "basic_tests_linux",
    tests = [
        ":runtests_c_linux_dbg",
        ":runtests_c_linux_opt",
        ":runtests_cpp_linux_dbg_build_only",
        ":runtests_cpp_linux_opt_build_only",
        ":runtests_csharp_linux_dbg",
        ":runtests_csharp_linux_opt",
        # TODO(jtattermusch): reenable the test once not flaky anymore
        #":runtests_php_linux_dbg",
        ":runtests_php_linux_opt",
        #":runtests_python_linux_opt",
        #":runtests_ruby_linux_dbg",
        #":runtests_ruby_linux_opt",
    ],
)

grpc_run_cpp_distribtest_test(
    name = "cpp_distribtest_cmake_linux",
    size = "enormous",
    args = ["test/distrib/cpp/run_distrib_test_cmake.sh"],
    docker_image_version = "tools/dockerfile/distribtest/cpp_debian10_x64.current_version",
)

grpc_run_cpp_distribtest_test(
    name = "cpp_distribtest_cmake_as_submodule_linux",
    size = "enormous",
    args = ["test/distrib/cpp/run_distrib_test_cmake_as_submodule.sh"],
    docker_image_version = "tools/dockerfile/distribtest/cpp_debian10_x64.current_version",
)

grpc_run_cpp_distribtest_test(
    name = "cpp_distribtest_cmake_as_externalproject_linux",
    size = "enormous",
    args = ["test/distrib/cpp/run_distrib_test_cmake_as_externalproject.sh"],
    docker_image_version = "tools/dockerfile/distribtest/cpp_debian10_x64.current_version",
)

grpc_run_cpp_distribtest_test(
    name = "cpp_distribtest_cmake_fetchcontent_linux",
    size = "enormous",
    args = ["test/distrib/cpp/run_distrib_test_cmake_fetchcontent.sh"],
    docker_image_version = "tools/dockerfile/distribtest/cpp_debian10_x64.current_version",
)

grpc_run_cpp_distribtest_test(
    name = "cpp_distribtest_cmake_module_install_linux",
    size = "enormous",
    args = ["test/distrib/cpp/run_distrib_test_cmake_module_install.sh"],
    docker_image_version = "tools/dockerfile/distribtest/cpp_debian10_x64.current_version",
)

grpc_run_cpp_distribtest_test(
    name = "cpp_distribtest_cmake_pkgconfig_linux",
    size = "enormous",
    args = ["test/distrib/cpp/run_distrib_test_cmake_pkgconfig.sh"],
    docker_image_version = "tools/dockerfile/distribtest/cpp_debian10_x64.current_version",
)

grpc_run_cpp_distribtest_test(
    name = "cpp_distribtest_cmake_aarch64_cross_linux",
    size = "enormous",
    args = ["test/distrib/cpp/run_distrib_test_cmake_aarch64_cross.sh"],
    docker_image_version = "tools/dockerfile/distribtest/cpp_debian10_aarch64_cross_x64.current_version",
)

test_suite(
    name = "cpp_distribtests_linux",
    tests = [
        ":cpp_distribtest_cmake_aarch64_cross_linux",
        ":cpp_distribtest_cmake_as_externalproject_linux",
        ":cpp_distribtest_cmake_as_submodule_linux",
        ":cpp_distribtest_cmake_fetchcontent_linux",
        ":cpp_distribtest_cmake_linux",
        ":cpp_distribtest_cmake_module_install_linux",
        ":cpp_distribtest_cmake_pkgconfig_linux",
    ],
)

# TODO: load supported versions from //bazel/supported_versions.txt
grpc_run_bazel_distribtest_test(
    name = "bazel_distribtest_5.4.1",
    size = "enormous",
    args = ["5.4.1"],
    docker_image_version = "tools/dockerfile/test/bazel.current_version",
)

# TODO: load supported versions from //bazel/supported_versions.txt
grpc_run_bazel_distribtest_test(
    name = "bazel_distribtest_6.1.2",
    size = "enormous",
    args = ["6.1.2"],
    docker_image_version = "tools/dockerfile/test/bazel.current_version",
)

test_suite(
    name = "bazel_distribtests_linux",
    tests = [
        ":bazel_distribtest_5.4.1",
        ":bazel_distribtest_6.1.2",
    ],
)

test_suite(
    name = "all_tests_linux",
    tests = [
        ":basic_tests_linux",
        ":bazel_distribtests_linux",
        ":cpp_distribtests_linux",
        ":portability_tests_linux",
    ],
)
