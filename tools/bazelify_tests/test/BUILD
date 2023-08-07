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
load("//tools/bazelify_tests:build_defs.bzl", "grpc_run_tests_py_test")
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

# Ruby
grpc_run_tests_py_test(
    name = "runtests_ruby_linux_dbg",
    size = "enormous",
    args = [
        "-l ruby -c dbg",
    ],
    docker_image_version = "tools/dockerfile/test/ruby_debian11_x64.current_version",
    prepare_script = ":prepare_ruby.sh",
    use_login_shell = True,  # ruby's docker image uses RVM which wierdly requires login shell
)

grpc_run_tests_py_test(
    name = "runtests_ruby_linux_opt",
    size = "enormous",
    args = [
        "-l ruby -c opt",
    ],
    docker_image_version = "tools/dockerfile/test/ruby_debian11_x64.current_version",
    prepare_script = ":prepare_ruby.sh",
    use_login_shell = True,  # ruby's docker image uses RVM which wierdly requires login shell
)

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

# Python
grpc_run_tests_py_test(
    name = "runtests_python_linux_opt",
    size = "enormous",
    args = [
        "-l python -c opt",
    ],
    docker_image_version = "tools/dockerfile/test/python_debian11_default_x64.current_version",
)

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
        ":runtests_python_linux_opt",
        ":runtests_ruby_linux_dbg",
        ":runtests_ruby_linux_opt",
    ],
)

test_suite(
    name = "all_tests_linux",
    tests = [
        ":basic_tests_linux",
        ":portability_tests_linux",
    ],
)
