# Copyright 2026 The gRPC Authors
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
"""Root Noxfile for gRPC Python aggregating developer and CI workflows."""

import glob
import os
import shutil
import nox

ROOT_DIR = os.path.abspath(os.path.dirname(__file__))
TESTS_NOXFILE = os.path.join(ROOT_DIR, "src", "python", "grpcio_tests", "noxfile.py")


@nox.session(python="3.11")
def lint(session: nox.Session):
    """Run code linters (pylint and isort) across Python packages."""
    session.install("astroid==2.15.8", "pylint==2.17.7", "isort~=5.11")

    session.log("Running pylint on library source code...")
    dirs = [
        "src/python/grpcio/grpc",
        "src/python/grpcio_channelz/grpc_channelz",
        "src/python/grpcio_health_checking/grpc_health",
        "src/python/grpcio_reflection/grpc_reflection",
        "src/python/grpcio_testing/grpc_testing",
        "src/python/grpcio_status/grpc_status",
        "src/python/grpcio_observability/grpc_observability",
        "src/python/grpcio_csm_observability/grpc_csm_observability",
    ]
    for target_dir in dirs:
        session.run("pylint", "--rcfile=.pylintrc", "-rn", target_dir)

    session.log("Running pylint on test code...")
    test_dirs = [
        "src/python/grpcio_tests/tests",
        "src/python/grpcio_tests/tests_gevent",
    ]
    for target_dir in test_dirs:
        session.run("pylint", "--rcfile=.pylintrc-tests", "-rn", target_dir)


@nox.session(venv_params=["--system-site-packages"])
def doc(session: nox.Session):
    """Build Sphinx API reference documentation."""
    session.install(
        "Sphinx",
        "pydata_sphinx_theme==0.16.1",
        "googleapis-common-protos",
    )
    
    import sphinx.cmd.build

    session.log("Building gRPC Python documentation via Sphinx...")
    source_dir = os.path.join(ROOT_DIR, "doc", "python", "sphinx")
    target_dir = os.path.join(ROOT_DIR, "doc", "build")

    exit_code = sphinx.cmd.build.build_main(
        ["-b", "html", "-W", "--keep-going", source_dir, target_dir]
    )
    if exit_code != 0:
        session.error("Documentation generation failed with warnings or errors.")


@nox.session
def clean_all(session: nox.Session):
    """Clean all build artifacts, temporary directories, and compiled extensions."""
    session.log("Cleaning build artifacts across all gRPC Python packages...")

    clean_patterns = [
        "pyb",
        "doc/build",
        "src/python/**/__pycache__",
        "src/python/**/*.egg-info",
        "src/python/**/build",
        "src/python/**/.nox",
        "src/python/grpcio/grpc/_cython/cygrpc.cpp",
        "src/python/grpcio/grpc/_cython/*.so",
        "src/python/grpcio/grpc/_cython/*.pyd",
        "tools/distrib/python/**/build",
        "tools/distrib/python/**/*.egg-info",
    ]

    for pattern in clean_patterns:
        full_pattern = os.path.join(ROOT_DIR, pattern)
        for path in glob.glob(full_pattern, recursive=True):
            if os.path.isfile(path):
                session.log(f"Removing file: {os.path.relpath(path, ROOT_DIR)}")
                os.remove(path)
            elif os.path.isdir(path):
                session.log(f"Removing directory: {os.path.relpath(path, ROOT_DIR)}")
                shutil.rmtree(path, ignore_errors=True)


@nox.session(venv_backend="none")
def test_all(session: nox.Session):
    """Run all unit test suites in src/python/grpcio_tests/noxfile.py sequentially."""
    for test_session in ["test_lite", "test_py3_only", "test_aio"]:
        session.log(f"Running {test_session} from {TESTS_NOXFILE}...")
        session.run(
            "nox",
            "--no-venv",
            "-s",
            test_session,
            "-f",
            TESTS_NOXFILE,
            external=True,
        )
