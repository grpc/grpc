#!/usr/bin/env python3

#Copyright 2019 gRPC authors.
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
"""Verifies that all gRPC Python artifacts have been successfully published.

This script is intended to be run from a directory containing the artifacts
that have been uploaded and only the artifacts that have been uploaded. We use
PyPI's JSON API to verify that the proper filenames and checksums are present.

Note that PyPI may take several minutes to update its metadata. Don't have a
heart attack immediately.

This sanity check is a good first step, but ideally, we would automate the
entire release process.
"""

import argparse
import collections
import hashlib
import os
import requests
import sys

_DEFAULT_PACKAGES = [
    "grpcio",
    "grpcio-tools",
    "grpcio-status",
    "grpcio-health-checking",
    "grpcio-reflection",
    "grpcio-channelz",
    "grpcio-testing",
]

Artifact = collections.namedtuple("Artifact", ("filename", "checksum"))


def _get_md5_checksum(filename):
    """Calculate the md5sum for a file."""
    hash_md5 = hashlib.md5()
    with open(filename, 'rb') as f:
        for chunk in iter(lambda: f.read(4096), b""):
            hash_md5.update(chunk)
        return hash_md5.hexdigest()


def _get_local_artifacts():
    """Get a set of artifacts representing all files in the cwd."""
    return set(
        Artifact(f, _get_md5_checksum(f)) for f in os.listdir(os.getcwd()))


def _get_remote_artifacts_for_package(package, version):
    """Get a list of artifacts based on PyPi's json metadata.

    Note that this data will not updated immediately after upload. In my
    experience, it has taken a minute on average to be fresh.
    """
    artifacts = set()
    payload = requests.get("https://pypi.org/pypi/{}/{}/json".format(
        package, version)).json()
    for download_info in payload['releases'][version]:
        artifacts.add(
            Artifact(download_info['filename'], download_info['md5_digest']))
    return artifacts


def _get_remote_artifacts_for_packages(packages, version):
    artifacts = set()
    for package in packages:
        artifacts |= _get_remote_artifacts_for_package(package, version)
    return artifacts


def _verify_release(version, packages):
    """Compare the local artifacts to the packages uploaded to PyPI."""
    local_artifacts = _get_local_artifacts()
    remote_artifacts = _get_remote_artifacts_for_packages(packages, version)
    if local_artifacts != remote_artifacts:
        local_but_not_remote = local_artifacts - remote_artifacts
        remote_but_not_local = remote_artifacts - local_artifacts
        if local_but_not_remote:
            print("The following artifacts exist locally but not remotely.")
            for artifact in local_but_not_remote:
                print(artifact)
        if remote_but_not_local:
            print("The following artifacts exist remotely but not locally.")
            for artifact in remote_but_not_local:
                print(artifact)
        sys.exit(1)
    print("Release verified successfully.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        "Verify a release. Run this from a directory containing only the"
        "artifacts to be uploaded. Note that PyPI may take several minutes"
        "after the upload to reflect the proper metadata.")
    parser.add_argument("version")
    parser.add_argument("packages",
                        nargs='*',
                        type=str,
                        default=_DEFAULT_PACKAGES)
    args = parser.parse_args()
    _verify_release(args.version, args.packages)
