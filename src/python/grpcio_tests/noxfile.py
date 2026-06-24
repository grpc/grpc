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

import argparse
import os
import shutil

import nox

PYTHON_STEM = os.path.dirname(os.path.abspath(__file__))
GRPC_STEM = os.path.abspath(PYTHON_STEM + "../../../../")
PYTHON_REL_PATH = os.path.relpath(PYTHON_STEM, start=GRPC_STEM)
GRPC_PROTO_STEM = os.path.join("src", "proto")
PROTO_STEM = os.path.join(PYTHON_REL_PATH, "src", "proto")
PYTHON_PROTO_TOP_LEVEL = os.path.join(PYTHON_REL_PATH, "src")


@nox.session
def preprocess(session: nox.Session):
    """Session to gather proto dependencies"""

    session.cd(GRPC_STEM)
    try:
        shutil.rmtree(PROTO_STEM)
    except Exception as error:
        # We don't care if this command fails
        pass
    shutil.copytree(GRPC_PROTO_STEM, PROTO_STEM)
    for root, _, _ in os.walk(PYTHON_PROTO_TOP_LEVEL):
        path = os.path.join(root, "__init__.py")
        open(path, "a").close()


@nox.session(venv_params=["--system-site-packages"])
def build_package_protos(session: nox.Session):
    """Command to generate project *_pb2.py modules from proto files."""
    session.log("Running build_package_protos for grpcio-tools...")

    # Note: We use exit_on_error=False to play nice with Nox
    parser = argparse.ArgumentParser(exit_on_error=False)

    parser.add_argument(
        "-s",
        "--strict-mode",
        action="store_true",
        help="exit with non-zero value if the proto compiling fails.",
    )

    # Parse the arguments from session.posargs
    # Any unknown args will be collected in 'unknown'
    args, unknown = parser.parse_known_args(session.posargs)

    # due to limitations of the proto generator, we require that only *one*
    # directory is provided as an 'include' directory.
    # We provide the value of `distribution.package_dir` set in pyproject.toml
    # which is the same directory as the pyproject.toml file
    from grpc_tools import command

    command.build_package_protos(PYTHON_STEM, args.strict_mode)

@nox.session(venv_params=["--system-site-packages"])
def test_lite(session: nox.Session):
    """Command to run tests without fetching or building anything."""
    session.log("Running test_lite for grpcio-tools...")

    session.install("--index-url", "https://pypi.org/simple", "coverage")
    # Install the current package locally without trying to satisfy dependencies
    session.install("--no-build-isolation", "--no-deps", ".")

    # Run the python interpreter inside the virtual environment ot execute the tests
    session.run(
        "python",
        "-c",
        "import sys; "
        "import tests; "
        "loader = tests.Loader(); "
        "loader.loadTestsFromNames(['tests']); "
        "runner = tests.Runner(dedicated_threads=True); "
        "result = runner.run(loader.suite); "
        "sys.exit(0 if result.wasSuccessful() else 'Test failure')",
    )

@nox.session(venv_params=["--system-site-packages"])
def test_py3_only(session: nox.Session):
    """Command to run tests for Python 3+ features.

    This does not include asyncio tests, which are housed in a separate
    directory.
    """
    session.log("Running test_py3_only for grpcio-tools...")

    session.install("--index-url", "https://pypi.org/simple", "coverage")
    session.install("--no-build-isolation", "--no-deps", ".")

    session.run(
        "python",
        "-c",
        "import sys; "
        "import tests; "
        "loader = tests.Loader(); "
        "loader.loadTestsFromNames(['tests_py3_only']); "
        "runner = tests.Runner(); "
        "result = runner.run(loader.suite); "
        "sys.exit(0 if result.wasSuccessful() else 'Test failure')",
    )

@nox.session(venv_params=["--system-site-packages"])
def test_aio(session: nox.Session):
    """Command to run aio tests without fetching or building anything."""

    session.log("Running test_ail for grpcio-tools...")
    session.install("--index-url", "https://pypi.org/simple", "coverage")
    session.install("--no-build-isolation", "--no-deps", ".")

    session.run(
        "python",
        "-c",
        "import sys; "
        "import tests; "
        "loader = tests.Loader(); "
        "loader.loadTestsFromNames(['tests_aio']); "
        # Even without dedicated threads, the framework will somehow spawn a
        # new thread for tests to run upon. New thread doesn't have event loop
        # attached by default, so initialization is needed.
        "runner = tests.Runner(dedicated_threads=False); "
        "result = runner.run(loader.suite); "
        "sys.exit(0 if result.wasSuccessful() else 'Test failure')",
    )

@nox.session(venv_params=["--system-site-packages"])
def run_interop(session: nox.Session):
    """Run interop test client/server."""
    session.log("Running run_interop for grpcio-tests...")
    session.install(
        "--index-url",
        "https://pypi.org/simple",
        "coverage",
        "absl-py",
        "oauth2client",
        "google-auth",
        "requests",
        "protobuf",
    )

    # Note: We use exit_on_error=False to play nice with Nox
    parser = argparse.ArgumentParser(exit_on_error=False)
    parser.add_argument(
        "--client",
        action="store_true",
        help="flag indicating to run the client",
    )
    parser.add_argument(
        "--server",
        action="store_true",
        help="flag indicating to run the server",
    )
    parser.add_argument(
        "--use-asyncio",
        action="store_true",
        help="flag indicating to run the asyncio stack",
    )

    # Parse arguments from session.posargs.
    # Any unknown/additional args (like --port, --server_host, etc.)
    # will be collected in 'unknown' and passed directly to the client/server script.
    args, unknown = parser.parse_known_args(session.posargs)

    if args.client and args.server:
        session.error("You may only specify one of --client or --server")
    if not args.client and not args.server:
        session.error("You must specify either --client or --server")

    # Install the package
    session.install("--no-build-isolation", "--no-deps", ".")

    if args.client:
        session.run(
            "python",
            "-c",
            "import sys; "
            "from tests.interop import client; "
            "client.test_interoperability(client.parse_interop_client_args(sys.argv))",
            # Pass all extra/unknown arguments down to the client
            *unknown,
        )
    elif args.server:
        if args.use_asyncio:
            session.run(
                "python",
                "-c",
                "import sys; "
                "import asyncio; "
                "from tests_aio.interop import server; "
                "args = server.parse_interop_server_arguments(sys.argv); "
                "asyncio.get_event_loop().run_until_complete(server.serve(args))",
                *unknown,
            )
        else:
            session.run(
                "python",
                "-c",
                "import sys; "
                "from tests.interop import server; "
                "server.serve(server.parse_interop_server_arguments(sys.argv))",
                *unknown,
            )
