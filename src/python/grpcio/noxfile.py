# Copyright 2025 The gRPC Authors
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
"""Provides nox command classes for the GRPC Python setup process."""

import glob
import os
import os.path
import shutil

import nox

PYTHON_STEM = os.path.dirname(os.path.abspath(__file__))
GRPC_STEM = os.path.abspath(PYTHON_STEM + "../../../../")
GRPC_ROOT = os.path.relpath(GRPC_STEM, start=GRPC_STEM)
PYTHON_REL_PATH = os.path.relpath(PYTHON_STEM, start=GRPC_STEM)
CYTHON_STEM = os.path.join(PYTHON_REL_PATH, "grpc", "_cython")

class CommandError(Exception):
    """Simple exception class for GRPC custom commands."""


@nox.session
def doc(session: nox.Session):
    """Session to generate documentation via sphinx."""

    # We import here to ensure that setup.py has had a chance to install the
    # relevant package eggs first.
    import sphinx.cmd.build

    session.log("Running doc gen via sphinx...")

    source_dir = os.path.join(GRPC_ROOT, "doc", "python", "sphinx")
    target_dir = os.path.join(GRPC_ROOT, "doc", "build")
    exit_code = sphinx.cmd.build.build_main(
        ["-b", "html", "-W", "--keep-going", source_dir, target_dir]
    )
    if exit_code != 0:
        raise CommandError("Documentation generation has warnings or errors")


@nox.session
def clean(session: nox.Session):
    """Session to clean build artifacts."""

    _FILE_PATTERNS = (
        "pyb",
        "src/python/grpcio/__pycache__/",
        "src/python/grpcio/grpc/_cython/cygrpc.cpp",
        "src/python/grpcio/grpc/_cython/*.so",
        "src/python/grpcio/grpcio.egg-info/",
    )
    _CURRENT_DIRECTORY = os.path.normpath(
        os.path.join(os.path.dirname(os.path.realpath(__file__)), "../../..")
    )

    for path_spec in _FILE_PATTERNS:
        this_glob = os.path.normpath(
            os.path.join(_CURRENT_DIRECTORY, path_spec)
        )
        abs_paths = glob.glob(this_glob)
        for path in abs_paths:
            if not str(path).startswith(_CURRENT_DIRECTORY):
                raise ValueError("Cowardly refusing to delete {}.".format(path))
            print("Removing {}".format(os.path.relpath(path)))
            if os.path.isfile(path):
                os.remove(str(path))
            else:
                shutil.rmtree(str(path))

@nox.session
def build_project_metadata(session: nox.Session):
    """Session to generate project metadata in a module."""
    
    session.log("Running build_project_metadata for grpcio")

    import sys
    sys.path.insert(0, PYTHON_STEM)
    import grpc_version

    metadata_dir = os.path.join(PYTHON_STEM, "grpc")
    module_file_path = os.path.join(metadata_dir, "_grpcio_metadata.py")

    version = grpc_version.VERSION
    # TODO(sergiitk): sometime in Nov 2025 - consider removing the env var
    # and making this the default behavior.
    skip_metadata_update_on_match = os.environ.get(
        "GRPC_PYTHON_BUILD_SKIP_METADATA_ON_VERSION_MATCH", "0"
    )

    if skip_metadata_update_on_match == "1" and os.path.exists(module_file_path):
        import ast
        try:
            with open(module_file_path, "r") as module_file:
                tree = ast.parse(module_file.read())
            current_version = "-1"
            for node in ast.walk(tree):
                if isinstance(node, ast.Assign):
                    for target in node.targets:
                        if isinstance(target, ast.Name) and target.id == "__version__":
                            if isinstance(node.value, ast.Constant):
                                current_version = node.value.value
            if current_version == version:
                session.log(f"Version match in _grpcio_metadata.py: {version}, skipping the update")
                return
        except Exception as e:
            session.log(f"Error checking existing metadata file: {e}")

    os.makedirs(metadata_dir, exist_ok=True)
    with open(module_file_path, "w") as module_file:
        module_file.write(f'__version__ = """{version}"""\n')


@nox.session(venv_params=["--system-site-packages"])
def build_py(session: nox.Session):
    """Session for custom project build command"""

    session.log("Running build_py for grpcio")

    # 1. Generate project metadata first
    build_project_metadata(session)

    # 2. Replicate standard build_py by copying Python files to the build directory
    src_dir = os.path.join(PYTHON_STEM, "grpc")
    build_dir = os.path.join(GRPC_STEM, "build", "lib", "grpc")

    if os.path.exists(build_dir):
        shutil.rmtree(build_dir)

    def ignore_patterns(path, names):
        return [
            name
            for name in names
            if name == "__pycache__"
            or name.endswith((".c", ".cpp", ".pyx", ".so", ".pyd"))
        ]

    shutil.copytree(src_dir, build_dir, ignore=ignore_patterns)
    session.log(f"Successfully copied pure Python packages to {build_dir}")

@nox.session
def build_ext(session: nox.Session):
    """Session to custom build_ext command to enable compiler-specific flags."""

    session.log("Custom build_ext command to enable compiler-specific flags.")

    import sys
    sys.path.insert(0, GRPC_STEM)
    import os
    os.chdir(GRPC_STEM)
    import setup
    import Cython.Build
    import sysconfig
    import tempfile
    import traceback
    import support

    cython_compiler_directives = {}

    extensions = Cython.Build.cythonize(
        setup.CYTHON_EXTENSION_MODULES,
        include_path=[
            include_dir
            for extension in setup.CYTHON_EXTENSION_MODULES
            for include_dir in extension.include_dirs
        ]
        + [CYTHON_STEM],
        compiler_directives=cython_compiler_directives,
    )    

    from setuptools.command.build_ext import build_ext as _build_ext

    class NoxBuildExt(_build_ext):

        C_OPTIONS = {
            "unix": ("-pthread",),
            "msvc": (),
        }

        LINK_OPTIONS = {}

        def get_ext_filename(self, ext_name):
            # since python3.5, python extensions' shared libraries use a suffix that corresponds to the value
            # of sysconfig.get_config_var('EXT_SUFFIX') and contains info about the architecture the library targets.
            # E.g. on x64 linux the suffix is ".cpython-XYZ-x86_64-linux-gnu.so"
            # When crosscompiling python wheels, we need to be able to override this suffix
            # so that the resulting file name matches the target architecture and we end up with a well-formed
            # wheel.
            filename = _build_ext.get_ext_filename(self, ext_name)
            orig_ext_suffix = sysconfig.get_config_var("EXT_SUFFIX")
            new_ext_suffix = os.getenv("GRPC_PYTHON_OVERRIDE_EXT_SUFFIX")
            if new_ext_suffix and filename.endswith(orig_ext_suffix):
                filename = filename[: -len(orig_ext_suffix)] + new_ext_suffix
            return filename

        def build_extensions(self):
            old_compile = self.compiler._compile

            # use short temp directory to avoid linker command file errors caused by
            # exceeding 131071 characters in Windows.
            # TODO(ssreenithi): Remove once we have a better solution: b/454497076
            use_short_temp = os.environ.get(
                "GRPC_PYTHON_BUILD_USE_SHORT_TEMP_DIR_NAME", 0
            )
            if use_short_temp == "1":
                if not os.path.exists("pyb"):
                    os.mkdir("pyb")

                self.build_temp = tempfile.mkdtemp(dir="pyb")
                print(f"Using temp build directory: {self.build_temp}")


            def new_compile(obj, src, ext, cc_args, extra_postargs, pp_opts):
                # NOTE: keep in sync with setup.py EXTRA_ENV_COMPILE_ARGS.
                cpp_specific_args = {"-std=c++17", "-stdlib=libc++"}
                c_specific_args = {"-std=c11"}

                args_to_remove = set()
                if src.endswith(".c"):
                    # Remove cpp-specific args when compiling c.
                    args_to_remove = cpp_specific_args
                elif src.endswith((".cc", ".cpp")):
                    # Remove c-specific args when compiling c++.
                    args_to_remove = c_specific_args

                if args_to_remove:
                    extra_postargs = [
                        arg for arg in extra_postargs if arg not in args_to_remove
                    ]

                return old_compile(obj, src, ext, cc_args, extra_postargs, pp_opts)

            self.compiler._compile = new_compile

            compiler = self.compiler.compiler_type
            if compiler in self.C_OPTIONS:
                for extension in self.extensions:
                    extension.extra_compile_args += list(self.C_OPTIONS[compiler])

            if compiler in self.LINK_OPTIONS:
                for extension in self.extensions:
                    extension.extra_link_args += list(
                        self.LINK_OPTIONS[compiler]
                    )

            try:
                _build_ext.build_extensions(self)
            except Exception as error:
                formatted_exception = traceback.format_exc()
                support.diagnose_build_ext_error(self, error, formatted_exception)
                raise CommandError(
                    f"Failed `build_ext` step:\n{formatted_exception}"
                )

    from setuptools.dist import Distribution
    from setuptools.command import build_ext
    
    dist = Distribution({"ext_modules": extensions})     
    cmd = NoxBuildExt(dist)
    cmd.ensure_finalized()
    cmd.run()

