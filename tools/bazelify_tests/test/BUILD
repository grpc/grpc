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
load("//tools/bazelify_tests:build_defs.bzl", "grpc_build_artifact_task", "grpc_run_cpp_distribtest_test", "grpc_run_distribtest_test", "grpc_run_simple_command_test", "grpc_run_tests_harness_test")
load(":bazel_distribtests.bzl", "generate_bazel_distribtests")
load(":portability_tests.bzl", "generate_run_tests_portability_tests")
load(":strict_tests.bzl", "generate_strict_tests")

licenses(["notice"])

grpc_package(name = "tools/bazelify_tests/test")

generate_run_tests_portability_tests(name = "portability_tests_linux")

generate_bazel_distribtests(name = "bazel_distribtests_linux")

# C/C++
grpc_run_tests_harness_test(
    name = "runtests_c_linux_dbg_build_only",
    size = "enormous",
    args = [
        "-l c -c dbg --build_only",
    ],
    docker_image_version = "tools/dockerfile/test/cxx_debian11_x64.current_version",
)

grpc_run_tests_harness_test(
    name = "runtests_c_linux_opt_build_only",
    size = "enormous",
    args = [
        "-l c -c opt --build_only",
    ],
    docker_image_version = "tools/dockerfile/test/cxx_debian11_x64.current_version",
)

grpc_run_tests_harness_test(
    name = "runtests_cpp_linux_dbg_build_only",
    size = "enormous",
    args = [
        "-l c++ -c dbg --build_only",
    ],
    docker_image_version = "tools/dockerfile/test/cxx_debian11_x64.current_version",
)

grpc_run_tests_harness_test(
    name = "runtests_cpp_linux_opt_build_only",
    size = "enormous",
    args = [
        "-l c++ -c opt --build_only",
    ],
    docker_image_version = "tools/dockerfile/test/cxx_debian11_x64.current_version",
)

# Ruby
grpc_run_tests_harness_test(
    name = "runtests_ruby_linux_dbg",
    size = "enormous",
    args = [
        "-l ruby -c dbg",
    ],
    docker_image_version = "tools/dockerfile/test/ruby_debian11_x64.current_version",
    prepare_script = ":prepare_ruby.sh",
    use_login_shell = True,  # ruby's docker image uses RVM which weirdly requires login shell
)

grpc_run_tests_harness_test(
    name = "runtests_ruby_linux_opt",
    size = "enormous",
    args = [
        "-l ruby -c opt",
    ],
    docker_image_version = "tools/dockerfile/test/ruby_debian11_x64.current_version",
    prepare_script = ":prepare_ruby.sh",
    use_login_shell = True,  # ruby's docker image uses RVM which weirdly requires login shell
)

# PHP
grpc_run_tests_harness_test(
    name = "runtests_php_linux_dbg",
    size = "enormous",
    args = [
        "-l php8 -c dbg",
    ],
    docker_image_version = "tools/dockerfile/test/php8_debian12_x64.current_version",
)

grpc_run_tests_harness_test(
    name = "runtests_php_linux_opt",
    size = "enormous",
    args = [
        "-l php8 -c opt",
    ],
    docker_image_version = "tools/dockerfile/test/php8_debian12_x64.current_version",
)

# Python
grpc_run_tests_harness_test(
    name = "runtests_python_linux_opt",
    size = "enormous",
    args = [
        "-l python -c opt",
    ],
    docker_image_version = "tools/dockerfile/test/python_debian11_default_x64.current_version",
)

# C#
grpc_run_tests_harness_test(
    name = "runtests_csharp_linux_dbg",
    size = "enormous",
    args = [
        "-l csharp -c dbg",
    ],
    docker_image_version = "tools/dockerfile/test/csharp_debian11_x64.current_version",
)

grpc_run_tests_harness_test(
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
        ":runtests_c_linux_dbg_build_only",
        ":runtests_c_linux_opt_build_only",
        ":runtests_cpp_linux_dbg_build_only",
        ":runtests_cpp_linux_opt_build_only",
        ":runtests_csharp_linux_dbg",
        ":runtests_csharp_linux_opt",
        ":runtests_php_linux_dbg",
        ":runtests_php_linux_opt",
        ":runtests_python_linux_opt",
        ":runtests_ruby_linux_dbg",
        ":runtests_ruby_linux_opt",
    ],
)

grpc_run_cpp_distribtest_test(
    name = "cpp_distribtest_cmake_linux",
    size = "enormous",
    args = ["test/distrib/cpp/run_distrib_test_cmake.sh"],
    docker_image_version = "tools/dockerfile/distribtest/cpp_debian11_x64.current_version",
)

grpc_run_cpp_distribtest_test(
    name = "cpp_distribtest_cmake_as_submodule_linux",
    size = "enormous",
    args = ["test/distrib/cpp/run_distrib_test_cmake_as_submodule.sh"],
    docker_image_version = "tools/dockerfile/distribtest/cpp_debian11_x64.current_version",
)

grpc_run_cpp_distribtest_test(
    name = "cpp_distribtest_cmake_as_externalproject_linux",
    size = "enormous",
    args = ["test/distrib/cpp/run_distrib_test_cmake_as_externalproject.sh"],
    docker_image_version = "tools/dockerfile/distribtest/cpp_debian11_x64.current_version",
)

grpc_run_cpp_distribtest_test(
    name = "cpp_distribtest_cmake_fetchcontent_linux",
    size = "enormous",
    args = ["test/distrib/cpp/run_distrib_test_cmake_fetchcontent.sh"],
    docker_image_version = "tools/dockerfile/distribtest/cpp_debian11_x64.current_version",
)

grpc_run_cpp_distribtest_test(
    name = "cpp_distribtest_cmake_module_install_linux",
    size = "enormous",
    args = ["test/distrib/cpp/run_distrib_test_cmake_module_install.sh"],
    docker_image_version = "tools/dockerfile/distribtest/cpp_debian11_x64.current_version",
)

grpc_run_cpp_distribtest_test(
    name = "cpp_distribtest_cmake_pkgconfig_linux",
    size = "enormous",
    args = ["test/distrib/cpp/run_distrib_test_cmake_pkgconfig.sh"],
    docker_image_version = "tools/dockerfile/distribtest/cpp_debian11_x64.current_version",
)

grpc_run_cpp_distribtest_test(
    name = "cpp_distribtest_cmake_aarch64_cross_linux",
    size = "enormous",
    args = ["test/distrib/cpp/run_distrib_test_cmake_aarch64_cross.sh"],
    docker_image_version = "tools/dockerfile/distribtest/cpp_debian11_aarch64_cross_x64.current_version",
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

# "Bazel build" tests
generate_strict_tests()

grpc_run_simple_command_test(
    name = "bazel_build_with_bzlmod_linux",
    size = "enormous",
    args = ["tools/bazelify_tests/test/bazel_build_with_bzlmod_linux.sh"],
    docker_image_version = "tools/dockerfile/test/bazel.current_version",
)

grpc_run_simple_command_test(
    name = "bazel_build_with_grpc_no_xds_linux",
    size = "enormous",
    args = ["tools/bazelify_tests/test/bazel_build_with_grpc_no_xds_linux.sh"],
    docker_image_version = "tools/dockerfile/test/bazel.current_version",
)

grpc_run_simple_command_test(
    name = "bazel_build_with_grpc_no_xds_negative_test_linux",
    size = "enormous",
    args = ["tools/bazelify_tests/test/bazel_build_with_grpc_no_xds_negative_test_linux.sh"],
    docker_image_version = "tools/dockerfile/test/bazel.current_version",
)

test_suite(
    name = "bazel_build_tests_linux",
    tests = [
        ":bazel_build_with_bzlmod_linux",
        ":bazel_build_with_grpc_no_xds_linux",
        ":bazel_build_with_grpc_no_xds_negative_test_linux",
        ":bazel_build_with_strict_warnings_linux",
    ],
)

# protoc artifact build tasks
grpc_build_artifact_task(
    name = "artifact_protoc_linux_x64",
    build_script = "build_artifact_protoc_linux.sh",
    docker_image_version = "tools/dockerfile/grpc_artifact_centos6_x64.current_version",
)

grpc_build_artifact_task(
    name = "artifact_protoc_linux_x86",
    build_script = "build_artifact_protoc_linux.sh",
    docker_image_version = "tools/dockerfile/grpc_artifact_centos6_x86.current_version",
)

grpc_build_artifact_task(
    name = "artifact_protoc_linux_aarch64",
    build_script = "build_artifact_protoc_linux_aarch64.sh",
    docker_image_version = "tools/dockerfile/grpc_artifact_protoc_aarch64.current_version",
)

# PHP artifact build tasks

grpc_build_artifact_task(
    name = "artifact_php_linux_x64",
    build_script = "build_artifact_php_linux.sh",
    docker_image_version = "tools/dockerfile/test/php8_zts_debian12_x64.current_version",
)

# Python artifact build tasks

grpc_build_artifact_task(
    name = "artifact_python_linux_x64_manylinux2014_cp313",
    build_script = "build_artifact_python_linux_x64_cp313.sh",
    docker_image_version = "tools/dockerfile/grpc_artifact_python_manylinux2014_x64.current_version",
)

# Building 39 here since default Python version in testing image is 3.9.
grpc_build_artifact_task(
    name = "artifact_python_linux_x64_manylinux2014_cp39",
    build_script = "build_artifact_python_linux_x64_cp39.sh",
    docker_image_version = "tools/dockerfile/grpc_artifact_python_manylinux2014_x64.current_version",
)

# TODO(jtattermusch): add more grpc_build_artifact_task targets for existing python artifacts from artifact_targets.py

grpc_build_artifact_task(
    name = "package_python_linux",
    # TODO(jtattermusch): add more python artifacts once they are migrated from artifact_targets.py
    artifact_deps = [
        "artifact_python_linux_x64_manylinux2014_cp313",
        "artifact_python_linux_x64_manylinux2014_cp39",
    ],
    build_script = "build_package_python_linux.sh",
    docker_image_version = "tools/dockerfile/grpc_artifact_python_manylinux2014_x64.current_version",
)

# C# package build tasks

grpc_build_artifact_task(
    name = "package_csharp_linux",
    # csharp package needs pre-built protoc and protoc plugin binaries
    artifact_deps = [
        "artifact_protoc_linux_x64",
        "artifact_protoc_linux_x86",
        "artifact_protoc_linux_aarch64",
    ],
    build_script = "build_package_csharp_linux.sh",
    docker_image_version = "tools/dockerfile/test/csharp_debian11_x64.current_version",
)

# C# distribtests

grpc_run_distribtest_test(
    name = "distribtest_csharp_linux_x64_debian11",
    # depend on the C# packages
    artifact_deps = [
        "package_csharp_linux",
    ],
    build_script = "run_distribtest_csharp_linux.sh",
    docker_image_version = "tools/dockerfile/distribtest/csharp_debian11_x64.current_version",
)

grpc_run_distribtest_test(
    name = "distribtest_csharp_linux_x64_ubuntu2204",
    # depend on the C# packages
    artifact_deps = [
        "package_csharp_linux",
    ],
    build_script = "run_distribtest_csharp_linux.sh",
    docker_image_version = "tools/dockerfile/distribtest/csharp_ubuntu2204_x64.current_version",
)

grpc_run_distribtest_test(
    name = "distribtest_csharp_linux_x64_alpine",
    # depend on the C# packages
    artifact_deps = [
        "package_csharp_linux",
    ],
    build_script = "run_distribtest_csharp_linux.sh",
    docker_image_version = "tools/dockerfile/distribtest/csharp_alpine_x64.current_version",
)

grpc_run_distribtest_test(
    name = "distribtest_csharp_linux_x64_dotnet31",
    # depend on the C# packages
    artifact_deps = [
        "package_csharp_linux",
    ],
    build_script = "run_distribtest_csharp_linux.sh",
    docker_image_version = "tools/dockerfile/distribtest/csharp_dotnet31_x64.current_version",
)

grpc_run_distribtest_test(
    name = "distribtest_csharp_linux_x64_dotnet5",
    # depend on the C# packages
    artifact_deps = [
        "package_csharp_linux",
    ],
    build_script = "run_distribtest_csharp_linux.sh",
    docker_image_version = "tools/dockerfile/distribtest/csharp_dotnet5_x64.current_version",
)

test_suite(
    name = "csharp_distribtests_linux",
    tests = [
        ":distribtest_csharp_linux_x64_alpine",
        ":distribtest_csharp_linux_x64_debian11",
        ":distribtest_csharp_linux_x64_dotnet31",
        ":distribtest_csharp_linux_x64_dotnet5",
        ":distribtest_csharp_linux_x64_ubuntu2204",
    ],
)

# PHP distribtests

grpc_run_distribtest_test(
    name = "distribtest_php_linux_x64_debian12",
    size = "enormous",
    artifact_deps = [
        "artifact_php_linux_x64",
    ],
    build_script = "run_distribtest_php_linux.sh",
    docker_image_version = "tools/dockerfile/distribtest/php8_debian12_x64.current_version",
    docker_run_as_root = True,
)

test_suite(
    name = "php_distribtests_linux",
    tests = [
        ":distribtest_php_linux_x64_debian12",
    ],
)

# Python distribtests

grpc_run_distribtest_test(
    name = "distribtest_python_linux_x64_bullseye",
    artifact_deps = [
        "package_python_linux",
    ],
    build_script = "run_distribtest_python_linux.sh",
    docker_image_version = "tools/dockerfile/distribtest/python_bullseye_x64.current_version",
)

test_suite(
    name = "python_distribtests_linux",
    tests = [
        ":distribtest_python_linux_x64_bullseye",
    ],
)

# TODO(jtattermusch): add more grpc_run_distribtest_test targets for existing python distribtests from distribtest_targets.py
# Note that there a two flavors of python distribtests - "binary" (which uses pre-built wheels) and "source" (which compiles python extension for sources)

# TODO(jtattermusch): add grpc_build_artifact_task targets for ruby artifacts (which is tricky, since ruby artifact builds do not run under docker since they invoke docker themselves)

test_suite(
    name = "artifact_build_tests_linux",
    tests = [
        ":artifact_php_linux_x64_build_test",
        ":artifact_protoc_linux_aarch64_build_test",
        ":artifact_protoc_linux_x64_build_test",
        ":artifact_protoc_linux_x86_build_test",
        ":artifact_python_linux_x64_manylinux2014_cp313_build_test",
        ":artifact_python_linux_x64_manylinux2014_cp39_build_test",
        ":package_csharp_linux_build_test",
        ":package_python_linux_build_test",
    ],
)

test_suite(
    name = "all_tests_linux",
    tests = [
        ":artifact_build_tests_linux",
        ":basic_tests_linux",
        ":bazel_build_tests_linux",
        ":bazel_distribtests_linux",
        ":cpp_distribtests_linux",
        ":csharp_distribtests_linux",
        ":php_distribtests_linux",
        ":portability_tests_linux",
        ":python_distribtests_linux",
    ],
)
