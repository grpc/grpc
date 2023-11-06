# Copyright 2015 gRPC authors.
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
"""A setup module for the GRPC Python package."""

# NOTE(https://github.com/grpc/grpc/issues/24028): allow setuptools to monkey
# patch distutils
import setuptools  # isort:skip

# Monkey Patch the unix compiler to accept ASM
# files used by boring SSL.
from distutils.unixccompiler import UnixCCompiler

UnixCCompiler.src_extensions.append(".S")
del UnixCCompiler

import os
import os.path
import pathlib
import platform
import re
import shlex
import shutil
import subprocess
from subprocess import PIPE
import sys
import sysconfig

import _metadata
import pkg_resources
from setuptools import Extension
from setuptools.command import egg_info

# Redirect the manifest template from MANIFEST.in to PYTHON-MANIFEST.in.
egg_info.manifest_maker.template = "PYTHON-MANIFEST.in"

PY3 = sys.version_info.major == 3
PYTHON_STEM = os.path.join("src", "python", "grpcio")
CORE_INCLUDE = (
    "include",
    ".",
)
ABSL_INCLUDE = (os.path.join("third_party", "abseil-cpp"),)
ADDRESS_SORTING_INCLUDE = (
    os.path.join("third_party", "address_sorting", "include"),
)
CARES_INCLUDE = (
    os.path.join("third_party", "cares", "cares", "include"),
    os.path.join("third_party", "cares"),
    os.path.join("third_party", "cares", "cares"),
)
if "darwin" in sys.platform:
    CARES_INCLUDE += (os.path.join("third_party", "cares", "config_darwin"),)
if "freebsd" in sys.platform:
    CARES_INCLUDE += (os.path.join("third_party", "cares", "config_freebsd"),)
if "linux" in sys.platform:
    CARES_INCLUDE += (os.path.join("third_party", "cares", "config_linux"),)
if "openbsd" in sys.platform:
    CARES_INCLUDE += (os.path.join("third_party", "cares", "config_openbsd"),)
RE2_INCLUDE = (os.path.join("third_party", "re2"),)
SSL_INCLUDE = (
    os.path.join("third_party", "boringssl-with-bazel", "src", "include"),
)
UPB_INCLUDE = (os.path.join("third_party", "upb"),)
UPB_GRPC_GENERATED_INCLUDE = (
    os.path.join("src", "core", "ext", "upb-generated"),
)
UPBDEFS_GRPC_GENERATED_INCLUDE = (
    os.path.join("src", "core", "ext", "upbdefs-generated"),
)
UTF8_RANGE_INCLUDE = (os.path.join("third_party", "utf8_range"),)
XXHASH_INCLUDE = (os.path.join("third_party", "xxhash"),)
ZLIB_INCLUDE = (os.path.join("third_party", "zlib"),)
README = os.path.join(PYTHON_STEM, "README.rst")

# Ensure we're in the proper directory whether or not we're being used by pip.
os.chdir(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.abspath(PYTHON_STEM))

# Break import-style to ensure we can actually find our in-repo dependencies.
import _parallel_compile_patch
import _spawn_patch
import grpc_core_dependencies

import commands
import grpc_version

_parallel_compile_patch.monkeypatch_compile_maybe()
_spawn_patch.monkeypatch_spawn()

LICENSE = "Apache License 2.0"

CLASSIFIERS = [
    "Development Status :: 5 - Production/Stable",
    "Programming Language :: Python",
    "Programming Language :: Python :: 3",
    "Programming Language :: Python :: 3.7",
    "Programming Language :: Python :: 3.8",
    "Programming Language :: Python :: 3.9",
    "Programming Language :: Python :: 3.10",
    "Programming Language :: Python :: 3.11",
    "Programming Language :: Python :: 3.12",
    "License :: OSI Approved :: Apache Software License",
]


def _env_bool_value(env_name, default):
    """Parses a bool option from an environment variable"""
    return os.environ.get(env_name, default).upper() not in ["FALSE", "0", ""]


BUILD_WITH_BORING_SSL_ASM = _env_bool_value(
    "GRPC_BUILD_WITH_BORING_SSL_ASM", "True"
)

# Export this environment variable to override the platform variant that will
# be chosen for boringssl assembly optimizations. This option is useful when
# crosscompiling and the host platform as obtained by sysconfig.get_platform()
# doesn't match the platform we are targetting.
# Example value: "linux-aarch64"
BUILD_OVERRIDE_BORING_SSL_ASM_PLATFORM = os.environ.get(
    "GRPC_BUILD_OVERRIDE_BORING_SSL_ASM_PLATFORM", ""
)

# Environment variable to determine whether or not the Cython extension should
# *use* Cython or use the generated C files. Note that this requires the C files
# to have been generated by building first *with* Cython support. Even if this
# is set to false, if the script detects that the generated `.c` file isn't
# present, then it will still attempt to use Cython.
BUILD_WITH_CYTHON = _env_bool_value("GRPC_PYTHON_BUILD_WITH_CYTHON", "False")

# Export this variable to use the system installation of openssl. You need to
# have the header files installed (in /usr/include/openssl) and during
# runtime, the shared library must be installed
BUILD_WITH_SYSTEM_OPENSSL = _env_bool_value(
    "GRPC_PYTHON_BUILD_SYSTEM_OPENSSL", "False"
)

# Export this variable to use the system installation of zlib. You need to
# have the header files installed (in /usr/include/) and during
# runtime, the shared library must be installed
BUILD_WITH_SYSTEM_ZLIB = _env_bool_value(
    "GRPC_PYTHON_BUILD_SYSTEM_ZLIB", "False"
)

# Export this variable to use the system installation of cares. You need to
# have the header files installed (in /usr/include/) and during
# runtime, the shared library must be installed
BUILD_WITH_SYSTEM_CARES = _env_bool_value(
    "GRPC_PYTHON_BUILD_SYSTEM_CARES", "False"
)

# Export this variable to use the system installation of re2. You need to
# have the header files installed (in /usr/include/re2) and during
# runtime, the shared library must be installed
BUILD_WITH_SYSTEM_RE2 = _env_bool_value("GRPC_PYTHON_BUILD_SYSTEM_RE2", "False")

# Export this variable to use the system installation of abseil. You need to
# have the header files installed (in /usr/include/absl) and during
# runtime, the shared library must be installed
BUILD_WITH_SYSTEM_ABSL = os.environ.get("GRPC_PYTHON_BUILD_SYSTEM_ABSL", False)

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

# For local development use only: This skips building gRPC Core and its
# dependencies, including protobuf and boringssl. This allows "incremental"
# compilation by first building gRPC Core using make, then building only the
# Python/Cython layers here.
#
# Note that this requires libboringssl.a in the libs/{dbg,opt}/ directory, which
# may require configuring make to not use the system openssl implementation:
#
#    make HAS_SYSTEM_OPENSSL_ALPN=0
#
# TODO(ericgribkoff) Respect the BUILD_WITH_SYSTEM_* flags alongside this option
USE_PREBUILT_GRPC_CORE = _env_bool_value(
    "GRPC_PYTHON_USE_PREBUILT_GRPC_CORE", "False"
)

# If this environmental variable is set, GRPC will not try to be compatible with
# libc versions old than the one it was compiled against.
DISABLE_LIBC_COMPATIBILITY = _env_bool_value(
    "GRPC_PYTHON_DISABLE_LIBC_COMPATIBILITY", "False"
)

# Environment variable to determine whether or not to enable coverage analysis
# in Cython modules.
ENABLE_CYTHON_TRACING = _env_bool_value(
    "GRPC_PYTHON_ENABLE_CYTHON_TRACING", "False"
)

# Environment variable specifying whether or not there's interest in setting up
# documentation building.
ENABLE_DOCUMENTATION_BUILD = _env_bool_value(
    "GRPC_PYTHON_ENABLE_DOCUMENTATION_BUILD", "False"
)


def check_linker_need_libatomic():
    """Test if linker on system needs libatomic."""
    code_test = (
        b"#include <atomic>\n"
        + b"int main() { return std::atomic<int64_t>{}; }"
    )
    cxx = shlex.split(os.environ.get("CXX", "c++"))
    cpp_test = subprocess.Popen(
        cxx + ["-x", "c++", "-std=c++14", "-"],
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
        cxx + ["-x", "c++", "-std=c++14", "-", "-latomic"],
        stdin=PIPE,
        stdout=PIPE,
        stderr=PIPE,
    )
    cpp_test.communicate(input=code_test)
    return cpp_test.returncode == 0


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
    EXTRA_ENV_COMPILE_ARGS = " -std=c++14"
    if "win32" in sys.platform:
        if sys.version_info < (3, 5):
            EXTRA_ENV_COMPILE_ARGS += " -D_hypot=hypot"
            # We use define flags here and don't directly add to DEFINE_MACROS below to
            # ensure that the expert user/builder has a way of turning it off (via the
            # envvars) without adding yet more GRPC-specific envvars.
            # See https://sourceforge.net/p/mingw-w64/bugs/363/
            if "32" in platform.architecture()[0]:
                EXTRA_ENV_COMPILE_ARGS += (
                    " -D_ftime=_ftime32 -D_timeb=__timeb32"
                    " -D_ftime_s=_ftime32_s"
                )
            else:
                EXTRA_ENV_COMPILE_ARGS += (
                    " -D_ftime=_ftime64 -D_timeb=__timeb64"
                )
        else:
            # We need to statically link the C++ Runtime, only the C runtime is
            # available dynamically
            EXTRA_ENV_COMPILE_ARGS += " /MT"
    elif "linux" in sys.platform:
        EXTRA_ENV_COMPILE_ARGS += (
            " -fvisibility=hidden -fno-wrapv -fno-exceptions"
        )
    elif "darwin" in sys.platform:
        EXTRA_ENV_COMPILE_ARGS += (
            " -stdlib=libc++ -fvisibility=hidden -fno-wrapv -fno-exceptions"
            " -DHAVE_UNISTD_H"
        )

if EXTRA_ENV_LINK_ARGS is None:
    EXTRA_ENV_LINK_ARGS = ""
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

CYTHON_EXTENSION_PACKAGE_NAMES = ()

CYTHON_EXTENSION_MODULE_NAMES = ("grpc._cython.cygrpc",)

CYTHON_HELPER_C_FILES = ()

CORE_C_FILES = tuple(grpc_core_dependencies.CORE_SOURCE_FILES)
if "win32" in sys.platform:
    CORE_C_FILES = filter(lambda x: "third_party/cares" not in x, CORE_C_FILES)

if BUILD_WITH_SYSTEM_OPENSSL:
    CORE_C_FILES = filter(
        lambda x: "third_party/boringssl" not in x, CORE_C_FILES
    )
    CORE_C_FILES = filter(lambda x: "src/boringssl" not in x, CORE_C_FILES)
    SSL_INCLUDE = (os.path.join("/usr", "include", "openssl"),)

if BUILD_WITH_SYSTEM_ZLIB:
    CORE_C_FILES = filter(lambda x: "third_party/zlib" not in x, CORE_C_FILES)
    ZLIB_INCLUDE = (os.path.join("/usr", "include"),)

if BUILD_WITH_SYSTEM_CARES:
    CORE_C_FILES = filter(lambda x: "third_party/cares" not in x, CORE_C_FILES)
    CARES_INCLUDE = (os.path.join("/usr", "include"),)

if BUILD_WITH_SYSTEM_RE2:
    CORE_C_FILES = filter(lambda x: "third_party/re2" not in x, CORE_C_FILES)
    RE2_INCLUDE = (os.path.join("/usr", "include", "re2"),)

if BUILD_WITH_SYSTEM_ABSL:
    CORE_C_FILES = filter(
        lambda x: "third_party/abseil-cpp" not in x, CORE_C_FILES
    )
    ABSL_INCLUDE = (os.path.join("/usr", "include"),)

EXTENSION_INCLUDE_DIRECTORIES = (
    (PYTHON_STEM,)
    + CORE_INCLUDE
    + ABSL_INCLUDE
    + ADDRESS_SORTING_INCLUDE
    + CARES_INCLUDE
    + RE2_INCLUDE
    + SSL_INCLUDE
    + UPB_INCLUDE
    + UPB_GRPC_GENERATED_INCLUDE
    + UPBDEFS_GRPC_GENERATED_INCLUDE
    + UTF8_RANGE_INCLUDE
    + XXHASH_INCLUDE
    + ZLIB_INCLUDE
)

EXTENSION_LIBRARIES = ()
if "linux" in sys.platform:
    EXTENSION_LIBRARIES += ("rt",)
if not "win32" in sys.platform:
    EXTENSION_LIBRARIES += ("m",)
if "win32" in sys.platform:
    EXTENSION_LIBRARIES += (
        "advapi32",
        "bcrypt",
        "dbghelp",
        "ws2_32",
    )
if BUILD_WITH_SYSTEM_OPENSSL:
    EXTENSION_LIBRARIES += (
        "ssl",
        "crypto",
    )
if BUILD_WITH_SYSTEM_ZLIB:
    EXTENSION_LIBRARIES += ("z",)
if BUILD_WITH_SYSTEM_CARES:
    EXTENSION_LIBRARIES += ("cares",)
if BUILD_WITH_SYSTEM_RE2:
    EXTENSION_LIBRARIES += ("re2",)
if BUILD_WITH_SYSTEM_ABSL:
    EXTENSION_LIBRARIES += tuple(
        lib.stem[3:] for lib in pathlib.Path("/usr").glob("lib*/libabsl_*.so")
    )

DEFINE_MACROS = (("_WIN32_WINNT", 0x600),)
asm_files = []


# Quotes on Windows build macros are evaluated differently from other platforms,
# so we must apply quotes asymmetrically in order to yield the proper result in
# the binary.
def _quote_build_define(argument):
    if "win32" in sys.platform:
        return '"\\"{}\\""'.format(argument)
    return '"{}"'.format(argument)


DEFINE_MACROS += (
    ("GRPC_XDS_USER_AGENT_NAME_SUFFIX", _quote_build_define("Python")),
    (
        "GRPC_XDS_USER_AGENT_VERSION_SUFFIX",
        _quote_build_define(_metadata.__version__),
    ),
)

asm_key = ""
if BUILD_WITH_BORING_SSL_ASM and not BUILD_WITH_SYSTEM_OPENSSL:
    boringssl_asm_platform = (
        BUILD_OVERRIDE_BORING_SSL_ASM_PLATFORM
        if BUILD_OVERRIDE_BORING_SSL_ASM_PLATFORM
        else sysconfig.get_platform()
    )
    # BoringSSL's gas-compatible assembly files are all internally conditioned
    # by the preprocessor. Provided the platform has a gas-compatible assembler
    # (i.e. not Windows), we can include the assembly files and let BoringSSL
    # decide which ones should and shouldn't be used for the build.
    if not boringssl_asm_platform.startswith("win"):
        asm_key = "crypto_asm"
    else:
        print(
            "ASM Builds for BoringSSL currently not supported on:",
            boringssl_asm_platform,
        )
if asm_key:
    asm_files = grpc_core_dependencies.ASM_SOURCE_FILES[asm_key]
else:
    DEFINE_MACROS += (("OPENSSL_NO_ASM", 1),)

if not DISABLE_LIBC_COMPATIBILITY:
    DEFINE_MACROS += (("GPR_BACKWARDS_COMPATIBILITY_MODE", 1),)

if "win32" in sys.platform:
    # TODO(zyc): Re-enable c-ares on x64 and x86 windows after fixing the
    # ares_library_init compilation issue
    DEFINE_MACROS += (
        ("WIN32_LEAN_AND_MEAN", 1),
        ("CARES_STATICLIB", 1),
        ("GRPC_ARES", 0),
        ("NTDDI_VERSION", 0x06000000),
        ("NOMINMAX", 1),
    )
    if "64bit" in platform.architecture()[0]:
        DEFINE_MACROS += (("MS_WIN64", 1),)
    elif sys.version_info >= (3, 5):
        # For some reason, this is needed to get access to inet_pton/inet_ntop
        # on msvc, but only for 32 bits
        DEFINE_MACROS += (("NTDDI_VERSION", 0x06000000),)
else:
    DEFINE_MACROS += (
        ("HAVE_CONFIG_H", 1),
        ("GRPC_ENABLE_FORK_SUPPORT", 1),
    )

# Fix for multiprocessing support on Apple devices.
# TODO(vigneshbabu): Remove this once the poll poller gets fork support.
DEFINE_MACROS += (("GRPC_DO_NOT_INSTANTIATE_POSIX_POLLER", 1),)

# Fix for Cython build issue in aarch64.
# It's required to define this macro before include <inttypes.h>.
# <inttypes.h> was included in core/lib/channel/call_tracer.h.
# This macro should already be defined in grpc/grpc.h through port_platform.h,
# but we're still having issue in aarch64, so we manually define the macro here.
# TODO(xuanwn): Figure out what's going on in the aarch64 build so we can support
# gcc + Bazel.
DEFINE_MACROS += (("__STDC_FORMAT_MACROS", None),)

LDFLAGS = tuple(EXTRA_LINK_ARGS)
CFLAGS = tuple(EXTRA_COMPILE_ARGS)
if "linux" in sys.platform or "darwin" in sys.platform:
    pymodinit_type = "PyObject*" if PY3 else "void"
    pymodinit = 'extern "C" __attribute__((visibility ("default"))) {}'.format(
        pymodinit_type
    )
    DEFINE_MACROS += (("PyMODINIT_FUNC", pymodinit),)
    DEFINE_MACROS += (("GRPC_POSIX_FORK_ALLOW_PTHREAD_ATFORK", 1),)

# By default, Python3 distutils enforces compatibility of
# c plugins (.so files) with the OSX version Python was built with.
# We need OSX 10.10, the oldest which supports C++ thread_local.
# Python 3.9: Mac OS Big Sur sysconfig.get_config_var('MACOSX_DEPLOYMENT_TARGET') returns int (11)
if "darwin" in sys.platform:
    mac_target = sysconfig.get_config_var("MACOSX_DEPLOYMENT_TARGET")
    if mac_target:
        mac_target = pkg_resources.parse_version(str(mac_target))
        if mac_target < pkg_resources.parse_version("10.10.0"):
            os.environ["MACOSX_DEPLOYMENT_TARGET"] = "10.10"
            os.environ["_PYTHON_HOST_PLATFORM"] = re.sub(
                r"macosx-[0-9]+\.[0-9]+-(.+)",
                r"macosx-10.10-\1",
                sysconfig.get_platform(),
            )


def cython_extensions_and_necessity():
    cython_module_files = [
        os.path.join(PYTHON_STEM, name.replace(".", "/") + ".pyx")
        for name in CYTHON_EXTENSION_MODULE_NAMES
    ]
    config = os.environ.get("CONFIG", "opt")
    prefix = "libs/" + config + "/"
    if USE_PREBUILT_GRPC_CORE:
        extra_objects = [
            prefix + "libares.a",
            prefix + "libboringssl.a",
            prefix + "libgpr.a",
            prefix + "libgrpc.a",
        ]
        core_c_files = []
    else:
        core_c_files = list(CORE_C_FILES)
        extra_objects = []
    extensions = [
        Extension(
            name=module_name,
            sources=(
                [module_file]
                + list(CYTHON_HELPER_C_FILES)
                + core_c_files
                + asm_files
            ),
            include_dirs=list(EXTENSION_INCLUDE_DIRECTORIES),
            libraries=list(EXTENSION_LIBRARIES),
            define_macros=list(DEFINE_MACROS),
            extra_objects=extra_objects,
            extra_compile_args=list(CFLAGS),
            extra_link_args=list(LDFLAGS),
        )
        for (module_name, module_file) in zip(
            list(CYTHON_EXTENSION_MODULE_NAMES), cython_module_files
        )
    ]
    need_cython = BUILD_WITH_CYTHON
    if not BUILD_WITH_CYTHON:
        need_cython = (
            need_cython
            or not commands.check_and_update_cythonization(extensions)
        )
    # TODO: the strategy for conditional compiling and exposing the aio Cython
    # dependencies will be revisited by https://github.com/grpc/grpc/issues/19728
    return (
        commands.try_cythonize(
            extensions,
            linetracing=ENABLE_CYTHON_TRACING,
            mandatory=BUILD_WITH_CYTHON,
        ),
        need_cython,
    )


CYTHON_EXTENSION_MODULES, need_cython = cython_extensions_and_necessity()

PACKAGE_DIRECTORIES = {
    "": PYTHON_STEM,
}

INSTALL_REQUIRES = ()

EXTRAS_REQUIRES = {
    "protobuf": "grpcio-tools>={version}".format(version=grpc_version.VERSION),
}

SETUP_REQUIRES = (
    INSTALL_REQUIRES + ("Sphinx~=1.8.1",) if ENABLE_DOCUMENTATION_BUILD else ()
)

try:
    import Cython
except ImportError:
    if BUILD_WITH_CYTHON:
        sys.stderr.write(
            "You requested a Cython build via GRPC_PYTHON_BUILD_WITH_CYTHON, "
            "but do not have Cython installed. We won't stop you from using "
            "other commands, but the extension files will fail to build.\n"
        )
    elif need_cython:
        sys.stderr.write(
            "We could not find Cython. Setup may take 10-20 minutes.\n"
        )
        SETUP_REQUIRES += ("cython>=0.23,<3.0.0rc1",)

COMMAND_CLASS = {
    "doc": commands.SphinxDocumentation,
    "build_project_metadata": commands.BuildProjectMetadata,
    "build_py": commands.BuildPy,
    "build_ext": commands.BuildExt,
    "gather": commands.Gather,
    "clean": commands.Clean,
}

# Ensure that package data is copied over before any commands have been run:
credentials_dir = os.path.join(PYTHON_STEM, "grpc", "_cython", "_credentials")
try:
    os.mkdir(credentials_dir)
except OSError:
    pass
shutil.copyfile(
    os.path.join("etc", "roots.pem"), os.path.join(credentials_dir, "roots.pem")
)

PACKAGE_DATA = {
    # Binaries that may or may not be present in the final installation, but are
    # mentioned here for completeness.
    "grpc._cython": [
        "_credentials/roots.pem",
        "_windows/grpc_c.32.python",
        "_windows/grpc_c.64.python",
    ],
}
PACKAGES = setuptools.find_packages(PYTHON_STEM)

setuptools.setup(
    name="grpcio",
    version=grpc_version.VERSION,
    description="HTTP/2-based RPC framework",
    author="The gRPC Authors",
    author_email="grpc-io@googlegroups.com",
    url="https://grpc.io",
    project_urls={
        "Source Code": "https://github.com/grpc/grpc",
        "Bug Tracker": "https://github.com/grpc/grpc/issues",
        "Documentation": "https://grpc.github.io/grpc/python",
    },
    license=LICENSE,
    classifiers=CLASSIFIERS,
    long_description_content_type="text/x-rst",
    long_description=open(README).read(),
    ext_modules=CYTHON_EXTENSION_MODULES,
    packages=list(PACKAGES),
    package_dir=PACKAGE_DIRECTORIES,
    package_data=PACKAGE_DATA,
    python_requires=">=3.7",
    install_requires=INSTALL_REQUIRES,
    extras_require=EXTRAS_REQUIRES,
    setup_requires=SETUP_REQUIRES,
    cmdclass=COMMAND_CLASS,
)
