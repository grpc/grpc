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

"""
Generates portability tests.
"""

load("//tools/bazelify_tests:build_defs.bzl", "grpc_run_tests_harness_test")

def _safe_language_name(name):
    """Character '+' isn't allowed in bazel target name"""
    return name.replace("+", "p")

def generate_run_tests_portability_tests(name):
    """Generates run_tests_py portability test targets.

    Args:
        name: Name of the test suite that will be generated.
    """
    test_names = []

    # portability C x86
    grpc_run_tests_harness_test(
        name = "runtests_c_linux_dbg_x86_build_only",
        args = ["-l c -c dbg --build_only"],
        docker_image_version = "tools/dockerfile/test/cxx_debian11_x86.current_version",
        size = "enormous",
    )
    test_names.append("runtests_c_linux_dbg_x86_build_only")

    # C and C++ with no-exceptions on Linux
    for language in ["c", "c++"]:
        test_name = "runtests_%s_linux_dbg_noexcept_build_only" % _safe_language_name(language)
        grpc_run_tests_harness_test(
            name = test_name,
            args = ["-l %s --config noexcept --build_only" % language],
            docker_image_version = "tools/dockerfile/test/cxx_debian11_x64.current_version",
            size = "enormous",
        )
        test_names.append(test_name)

    # C and C++ under different compilers
    for language in ["c", "c++"]:
        compiler_configs = [
            # Some gRPC tests have an issue with gcc-7 so gcc-7 portability test won't build any gRPC tests
            ["gcc_7", "--cmake_configure_extra_args=-DgRPC_BUILD_TESTS=OFF", "tools/dockerfile/test/cxx_gcc_7_x64.current_version"],
            ["gcc_8", "", "tools/dockerfile/test/cxx_gcc_8_x64.current_version"],
            ["gcc_14_cxx20", "--cmake_configure_extra_args=-DCMAKE_CXX_STANDARD=20", "tools/dockerfile/test/cxx_gcc_14_x64.current_version"],
            ["gcc10.2_openssl102", "--cmake_configure_extra_args=-DgRPC_SSL_PROVIDER=package", "tools/dockerfile/test/cxx_debian11_openssl102_x64.current_version"],
            ["gcc10.2_openssl111", "--cmake_configure_extra_args=-DgRPC_SSL_PROVIDER=package", "tools/dockerfile/test/cxx_debian11_openssl111_x64.current_version"],
            ["gcc_12_openssl309", "--cmake_configure_extra_args=-DgRPC_SSL_PROVIDER=package", "tools/dockerfile/test/cxx_debian12_openssl309_x64.current_version"],
            ["gcc_musl", "", "tools/dockerfile/test/cxx_alpine_x64.current_version"],
            ["clang_7", "--cmake_configure_extra_args=-DCMAKE_C_COMPILER=clang --cmake_configure_extra_args=-DCMAKE_CXX_COMPILER=clang++", "tools/dockerfile/test/cxx_clang_7_x64.current_version"],
            ["clang_18_cxx23", "--cmake_configure_extra_args=-DCMAKE_C_COMPILER=clang --cmake_configure_extra_args=-DCMAKE_CXX_COMPILER=clang++ --cmake_configure_extra_args=-DCMAKE_CXX_STANDARD=23", "tools/dockerfile/test/cxx_clang_18_x64.current_version"],
        ]

        for compiler_name, args, docker_image_version in compiler_configs:
            test_name = "runtests_%s_linux_dbg_%s_build_only" % (_safe_language_name(language), compiler_name)
            grpc_run_tests_harness_test(
                name = test_name,
                args = ["-l %s -c dbg %s --build_only" % (language, args)],
                docker_image_version = docker_image_version,
                size = "enormous",
            )
            test_names.append(test_name)

    # Python on alpine
    grpc_run_tests_harness_test(
        name = "runtests_python_linux_dbg_alpine",
        args = [
            "-l python -c dbg --compiler python_alpine",
        ],
        docker_image_version = "tools/dockerfile/test/python_alpine_x64.current_version",
        size = "enormous",
    )
    test_names.append("runtests_python_linux_dbg_alpine")

    # Generate test suite that allows easily running all portability tests.
    native.test_suite(
        name = name,
        tests = [(":%s" % test_name) for test_name in test_names],
    )
