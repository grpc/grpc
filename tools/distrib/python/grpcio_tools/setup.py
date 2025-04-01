# Copyright 2016 gRPC authors.
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

import errno
import os
import os.path
import platform
import re
import shlex
import shutil
import subprocess
from subprocess import PIPE
import sys
import sysconfig

import setuptools
from setuptools import Extension
from setuptools.command import build_ext

# TODO(atash) add flag to disable Cython use

_PACKAGE_PATH = os.path.realpath(os.path.dirname(__file__))
_README_PATH = os.path.join(_PACKAGE_PATH, "README.rst")

os.chdir(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.abspath("."))

import _parallel_compile_patch
import _spawn_patch
import protoc_lib_deps
import python_version

import grpc_version

_EXT_INIT_SYMBOL = None
if sys.version_info[0] == 2:
    _EXT_INIT_SYMBOL = "init_protoc_compiler"
else:
    _EXT_INIT_SYMBOL = "PyInit__protoc_compiler"

_parallel_compile_patch.monkeypatch_compile_maybe()
_spawn_patch.monkeypatch_spawn()

CLASSIFIERS = [
    "Development Status :: 5 - Production/Stable",
    "Programming Language :: Python",
    "Programming Language :: Python :: 3",
    "License :: OSI Approved :: Apache Software License",
]

PY3 = sys.version_info.major == 3


def _env_bool_value(env_name, default):
    """Parses a bool option from an environment variable"""
    return os.environ.get(env_name, default).upper() not in ["FALSE", "0", ""]


# Environment variable to determine whether or not the Cython extension should
# *use* Cython or use the generated C files. Note that this requires the C files
# to have been generated by building first *with* Cython support.
BUILD_WITH_CYTHON = _env_bool_value("GRPC_PYTHON_BUILD_WITH_CYTHON", "False")

# Export this variable to force building the python extension with a statically linked libstdc++.
# At least on linux, this is normally not needed as we can build manylinux-compatible wheels on linux just fine
# without statically linking libstdc++ (which leads to a slight increase in the wheel size).
# This option is useful when crosscompiling wheels for aarch64 where
# it's difficult to ensure that the crosscompilation toolchain has a high-enough version
# of GCC (we require >=5.1) but still uses old-enough libstdc++ symbols.
# TODO(jtattermusch): remove this workaround once issues with crosscompiler version are resolved.
BUILD_WITH_STATIC_LIBSTDCXX = _env_bool_value(
    "GRPC_PYTHON_BUILD_WITH_STATIC_LIBSTDCXX", "False"
)


def check_linker_need_libatomic():
    """Test if linker on system needs libatomic."""
    code_test = (
        b"#include <atomic>\n"
        + b"int main() { return std::atomic<int64_t>{}; }"
    )
    cxx = os.environ.get("CXX", "c++")
    cpp_test = subprocess.Popen(
        [cxx, "-x", "c++", "-std=c++17", "-"],
        stdin=PIPE,
        stdout=PIPE,
        stderr=PIPE,
    )
    cpp_test.communicate(input=code_test)
    if cpp_test.returncode == 0:
        return False
    # Double-check to see if -latomic actually can solve the problem.
    # https://github.com/grpc/grpc/issues/22491
    cpp_test = subprocess.Popen(
        [cxx, "-x", "c++", "-std=c++17", "-", "-latomic"],
        stdin=PIPE,
        stdout=PIPE,
        stderr=PIPE,
    )
    cpp_test.communicate(input=code_test)
    return cpp_test.returncode == 0


class BuildExt(build_ext.build_ext):
    """Custom build_ext command."""

    def get_ext_filename(self, ext_name):
        # since python3.5, python extensions' shared libraries use a suffix that corresponds to the value
        # of sysconfig.get_config_var('EXT_SUFFIX') and contains info about the architecture the library targets.
        # E.g. on x64 linux the suffix is ".cpython-XYZ-x86_64-linux-gnu.so"
        # When crosscompiling python wheels, we need to be able to override this suffix
        # so that the resulting file name matches the target architecture and we end up with a well-formed
        # wheel.
        filename = build_ext.build_ext.get_ext_filename(self, ext_name)
        orig_ext_suffix = sysconfig.get_config_var("EXT_SUFFIX")
        new_ext_suffix = os.getenv("GRPC_PYTHON_OVERRIDE_EXT_SUFFIX")
        if new_ext_suffix and filename.endswith(orig_ext_suffix):
            filename = filename[: -len(orig_ext_suffix)] + new_ext_suffix
        return filename

    def build_extensions(self):
        # This is to let UnixCompiler get either C or C++ compiler options depending on the source.
        # Note that this doesn't work for MSVCCompiler and will be handled by _spawn_patch.py.
        old_compile = self.compiler._compile

        def new_compile(obj, src, ext, cc_args, extra_postargs, pp_opts):
            if src.endswith(".c"):
                extra_postargs = [
                    arg for arg in extra_postargs if arg != "-std=c++17"
                ]
            elif src.endswith((".cc", ".cpp")):
                extra_postargs = [
                    arg for arg in extra_postargs if arg != "-std=c11"
                ]
            return old_compile(obj, src, ext, cc_args, extra_postargs, pp_opts)

        self.compiler._compile = new_compile

        build_ext.build_ext.build_extensions(self)


# When building extensions for macOS on a system running macOS 11.0 or newer,
# make sure they target macOS 11.0 or newer to use C++17 stdlib properly.
# This overrides the default behavior of distutils, which targets the macOS
# version Python was built on. You can further customize the target macOS
# version by setting the MACOSX_DEPLOYMENT_TARGET environment variable before
# running setup.py.
if sys.platform == "darwin":
    if "MACOSX_DEPLOYMENT_TARGET" not in os.environ:
        target_ver = sysconfig.get_config_var("MACOSX_DEPLOYMENT_TARGET")
        if target_ver == "" or tuple(int(p) for p in target_ver.split(".")) < (
            10,
            14,
        ):
            os.environ["MACOSX_DEPLOYMENT_TARGET"] = "11.0"

# There are some situations (like on Windows) where CC, CFLAGS, and LDFLAGS are
# entirely ignored/dropped/forgotten by distutils and its Cygwin/MinGW support.
# We use these environment variables to thus get around that without locking
# ourselves in w.r.t. the multitude of operating systems this ought to build on.
# We can also use these variables as a way to inject environment-specific
# compiler/linker flags. We assume GCC-like compilers and/or MinGW as a
# reasonable default.
EXTRA_ENV_COMPILE_ARGS = os.environ.get("GRPC_PYTHON_CFLAGS", None)
EXTRA_ENV_LINK_ARGS = os.environ.get("GRPC_PYTHON_LDFLAGS", None)
if EXTRA_ENV_COMPILE_ARGS is None:
    EXTRA_ENV_COMPILE_ARGS = ""
    if "win32" in sys.platform:
        # MSVC by defaults uses C++14 and C89 so both needs to be configured.
        EXTRA_ENV_COMPILE_ARGS += " /std:c++17"
        EXTRA_ENV_COMPILE_ARGS += " /std:c11"
        # We need to statically link the C++ Runtime, only the C runtime is
        # available dynamically
        EXTRA_ENV_COMPILE_ARGS += " /MT"
    elif "linux" in sys.platform:
        # GCC by defaults uses C17 so only C++17 needs to be specified.
        EXTRA_ENV_COMPILE_ARGS += " -std=c++17"
        EXTRA_ENV_COMPILE_ARGS += " -fno-wrapv -frtti"
        # Reduce the optimization level from O3 (in many cases) to O1 to
        # workaround gcc misalignment bug with MOVAPS (internal b/329134877)
        EXTRA_ENV_COMPILE_ARGS += " -O1"
    elif "darwin" in sys.platform:
        # AppleClang by defaults uses C17 so only C++17 needs to be specified.
        EXTRA_ENV_COMPILE_ARGS += " -std=c++17"
        EXTRA_ENV_COMPILE_ARGS += " -fno-wrapv -frtti"
        EXTRA_ENV_COMPILE_ARGS += " -stdlib=libc++ -DHAVE_UNISTD_H"
if EXTRA_ENV_LINK_ARGS is None:
    EXTRA_ENV_LINK_ARGS = ""
    # This is needed for protobuf/main.cc
    if "win32" in sys.platform:
        EXTRA_ENV_LINK_ARGS += " Shell32.lib"
    # NOTE(rbellevi): Clang on Mac OS will make all static symbols (both
    # variables and objects) global weak symbols. When a process loads the
    # protobuf wheel's shared object library before loading *this* C extension,
    # the runtime linker will prefer the protobuf module's version of symbols.
    # This results in the process using a mixture of symbols from the protobuf
    # wheel and this wheel, which may be using different versions of
    # libprotobuf. In the case that they *are* using different versions of
    # libprotobuf *and* there has been a change in data layout (or in other
    # invariants) segfaults, data corruption, or "bad things" may happen.
    #
    # This flag ensures that on Mac, the only global symbol is the one loaded by
    # the Python interpreter. The problematic global weak symbols become local
    # weak symbols.  This is not required on Linux since the compiler does not
    # produce global weak symbols. This is not required on Windows as our ".pyd"
    # file does not contain any symbols.
    #
    # Finally, the leading underscore here is part of the Mach-O ABI. Unlike
    # more modern ABIs (ELF et al.), Mach-O prepends an underscore to the names
    # of C functions.
    if "darwin" in sys.platform:
        EXTRA_ENV_LINK_ARGS += " -Wl,-exported_symbol,_{}".format(
            _EXT_INIT_SYMBOL
        )
    if "linux" in sys.platform or "darwin" in sys.platform:
        EXTRA_ENV_LINK_ARGS += " -lpthread"
        if check_linker_need_libatomic():
            EXTRA_ENV_LINK_ARGS += " -latomic"
    if "linux" in sys.platform:
        EXTRA_ENV_LINK_ARGS += " -static-libgcc"

# Explicitly link Core Foundation framework for MacOS to ensure no symbol is
# missing when compiled using package managers like Conda.
if "darwin" in sys.platform:
    EXTRA_ENV_LINK_ARGS += " -framework CoreFoundation"

EXTRA_COMPILE_ARGS = shlex.split(EXTRA_ENV_COMPILE_ARGS)
EXTRA_LINK_ARGS = shlex.split(EXTRA_ENV_LINK_ARGS)

if BUILD_WITH_STATIC_LIBSTDCXX:
    EXTRA_LINK_ARGS.append("-static-libstdc++")

CC_FILES = [os.path.normpath(cc_file) for cc_file in protoc_lib_deps.CC_FILES]
PROTO_FILES = [
    os.path.normpath(proto_file) for proto_file in protoc_lib_deps.PROTO_FILES
]
CC_INCLUDES = [
    os.path.normpath(include_dir) for include_dir in protoc_lib_deps.CC_INCLUDES
]
PROTO_INCLUDE = os.path.normpath(protoc_lib_deps.PROTO_INCLUDE)

GRPC_PYTHON_TOOLS_PACKAGE = "grpc_tools"
GRPC_PYTHON_PROTO_RESOURCES_NAME = "_proto"

DEFINE_MACROS = ()
if "win32" in sys.platform:
    DEFINE_MACROS += (
        ("WIN32_LEAN_AND_MEAN", 1),
        # avoid https://github.com/abseil/abseil-cpp/issues/1425
        ("NOMINMAX", 1),
    )
    if "64bit" in platform.architecture()[0]:
        DEFINE_MACROS += (("MS_WIN64", 1),)
elif "linux" in sys.platform or "darwin" in sys.platform:
    DEFINE_MACROS += (("HAVE_PTHREAD", 1),)


def package_data():
    tools_path = GRPC_PYTHON_TOOLS_PACKAGE.replace(".", os.path.sep)
    proto_resources_path = os.path.join(
        tools_path, GRPC_PYTHON_PROTO_RESOURCES_NAME
    )
    proto_files = []
    for proto_file in PROTO_FILES:
        source = os.path.join(PROTO_INCLUDE, proto_file)
        target = os.path.join(proto_resources_path, proto_file)
        relative_target = os.path.join(
            GRPC_PYTHON_PROTO_RESOURCES_NAME, proto_file
        )
        try:
            os.makedirs(os.path.dirname(target))
        except OSError as error:
            if error.errno == errno.EEXIST:
                pass
            else:
                raise
        shutil.copy(source, target)
        proto_files.append(relative_target)
    return {GRPC_PYTHON_TOOLS_PACKAGE: proto_files}


def extension_modules():
    if BUILD_WITH_CYTHON:
        plugin_sources = [os.path.join("grpc_tools", "_protoc_compiler.pyx")]
    else:
        plugin_sources = [os.path.join("grpc_tools", "_protoc_compiler.cpp")]

    plugin_sources += [
        os.path.join("grpc_tools", "main.cc"),
        os.path.join("grpc_root", "src", "compiler", "python_generator.cc"),
        os.path.join("grpc_root", "src", "compiler", "proto_parser_helper.cc"),
    ] + CC_FILES

    plugin_ext = Extension(
        name="grpc_tools._protoc_compiler",
        sources=plugin_sources,
        include_dirs=[
            ".",
            "grpc_root",
            os.path.join("grpc_root", "include"),
        ]
        + CC_INCLUDES,
        define_macros=list(DEFINE_MACROS),
        extra_compile_args=list(EXTRA_COMPILE_ARGS),
        extra_link_args=list(EXTRA_LINK_ARGS),
    )
    extensions = [plugin_ext]
    if BUILD_WITH_CYTHON:
        from Cython import Build

        return Build.cythonize(extensions)
    else:
        return extensions


setuptools.setup(
    name="grpcio-tools",
    version=grpc_version.VERSION,
    description="Protobuf code generator for gRPC",
    long_description_content_type="text/x-rst",
    long_description=open(_README_PATH, "r").read(),
    author="The gRPC Authors",
    author_email="grpc-io@googlegroups.com",
    url="https://grpc.io",
    project_urls={
        "Source Code": "https://github.com/grpc/grpc/tree/master/tools/distrib/python/grpcio_tools",
        "Bug Tracker": "https://github.com/grpc/grpc/issues",
    },
    license="Apache License 2.0",
    classifiers=CLASSIFIERS,
    ext_modules=extension_modules(),
    packages=setuptools.find_packages("."),
    python_requires=f">={python_version.MIN_PYTHON_VERSION}",
    install_requires=[
        "protobuf>=6.30.0,<7.0dev",
        "grpcio>={version}".format(version=grpc_version.VERSION),
        "setuptools",
    ],
    package_data=package_data(),
    cmdclass={
        "build_ext": BuildExt,
    },
    entry_points={
        "console_scripts": [
            "python-grpc-tools-protoc = grpc_tools.protoc:entrypoint",
        ],
    },
)
