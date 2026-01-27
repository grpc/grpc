#!/usr/bin/env python3

# Copyright 2025 gRPC authors.
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

import json
import os
import re
import subprocess
import sys

import requests

### This script verifies that the Git commit hashes of third-party dependencies
### managed as Git submodules are the same as the versions declared in MODULE.bazel
### to ensures that when a developer updates a dependency, they update it
### consistently in both Git submodules and Bazel modules.
###
### Please update `DEP_MAPS` below when you add or remove dependencies.

### Map from bzlmod to submodule
DEP_MAPS = {
    "abseil-cpp": "abseil-cpp",
    # "boringssl": "boringssl-with-bazel",               # third_party/boringssl-with-bazel is a special branch so they cannot be compared.
    # 'c-ares': 'cares',                                 # third_party/cares is vendored so there is no commit hash.
    "envoy_api": "envoy-api",
    "google_benchmark": "benchmark",
    "googleapis": "googleapis",
    "googletest": "googletest",
    "opentelemetry-cpp": "opentelemetry-cpp",
    "protobuf": "protobuf",
    "protoc-gen-validate": "protoc-gen-validate",
    # "re2": "re2",                                      # third_party/re2 cannot be upgraded due to their cmake issue.
    # "xds": "xds",                                      # This can be resolved with https://github.com/grpc/grpc/pull/39908.
    # "zlib": "zlib",                                    # This can be resolved when zlib has a new release later than 1.3.1. (https://github.com/grpc/grpc/pull/40165)
}

GITHUB_TOKEN = None


def init_global_github_token():
    global GITHUB_TOKEN
    if "GITHUB_TOKEN" in os.environ:
        GITHUB_TOKEN = os.environ["GITHUB_TOKEN"]
    else:
        print("WARNING: Environment variable GITHUB_TOKEN is not defined.")


def get_submodule_map():
    """Return a map of submodule directory & its hash."""
    git_submodules = (
        subprocess.check_output("git submodule", shell=True)
        .decode()
        .strip()
        .split("\n")
    )
    return dict((s.split()[1], s.split()[0].strip("-")) for s in git_submodules)


def get_bzlmod_deps():
    """Return a map of name and version of bzlmod dependencies."""
    with open("MODULE.bazel") as f:
        bzlmod = f.read()
    matches = re.finditer(
        r'bazel_dep\(name\s*=\s*"([^"]+)",\s*version\s*=\s*"([^"]+)"', bzlmod
    )
    return dict((m.group(1), m.group(2)) for m in matches)


def get_bcr_source_info(name, version):
    """Return the source info from the bzlmod name via BCR."""
    url = f"https://raw.githubusercontent.com/bazelbuild/bazel-central-registry/refs/heads/main/modules/{name}/{version}/source.json"
    try:
        response = requests.get(url)
        response.raise_for_status()  # Raise an exception for bad status codes
        return response.json()
    except requests.exceptions.RequestException as e:
        print(f"Error downloading the file: {e}")
        return None
    except json.JSONDecodeError as e:
        print(f"Error parsing JSON: {e}")
        return None


def get_object_from_github(url):
    """Return the object from the github tag"""
    try:
        headers = {
            "Accept": "application/vnd.github+json",
            "X-GitHub-Api-Version": "2022-11-28",
        }
        if GITHUB_TOKEN:
            headers["Authorization"] = f"Bearer {GITHUB_TOKEN}"
        response = requests.get(url, headers=headers)
        response.raise_for_status()
        data = response.json()
        return data.get("object", None)
    except requests.exceptions.RequestException as e:
        print(f"An error occurred: {e}")
        return None


def get_commit_hash_from_github_tag(owner: str, repo: str, tag: str):
    """Return the commit hash from the github tag"""
    url = f"https://api.github.com/repos/{owner}/{repo}/git/ref/tags/{tag}"
    obj = get_object_from_github(url)
    if obj:
        if obj["type"] == "commit":
            return obj["sha"]
        elif obj["type"] == "tag":
            obj2 = get_object_from_github(obj["url"])
            if obj2:
                return obj2["sha"]
    return None


def get_commit_hash_from_github_url(url):
    """ "Return the commit hash from the github download URL."""
    # Something like https://github.com/envoyproxy/data-plane-api/archive/4de3c74cf21a9958c1cf26d8993c55c6e0d28b49.tar.gz
    mo = re.search(r"archive/([a-z0-9]{40})\.", url)
    if mo:
        return mo.group(1)
    # Something like https://github.com/google/benchmark/archive/refs/tags/v1.9.0.tar.gz
    mo = re.search(r"/([^/]+)/([^/]+)/archive/refs/tags/(.*)\.tar\.gz", url)
    if mo:
        return get_commit_hash_from_github_tag(
            mo.group(1), mo.group(2), mo.group(3)
        )
    # Something like https://github.com/abseil/abseil-cpp/releases/download/20250814.1/abseil-cpp-20250814.1.tar.gz
    mo = re.search(r"/([^/]+)/([^/]+)/releases/download/([^/]+)/", url)
    if mo:
        return get_commit_hash_from_github_tag(
            mo.group(1), mo.group(2), mo.group(3)
        )
    return None


def get_bcr_commit_hash(name, version):
    """Return the commit hash from the BCR module name and version."""
    info = get_bcr_source_info(name, version)
    if info is None:
        return None
    url = info["url"]
    return get_commit_hash_from_github_url(url)


def main():
    init_global_github_token()
    exit_code = 0
    bzlmod_deps = get_bzlmod_deps()
    submodule_map = get_submodule_map()
    for bzl_name, submodule_name in DEP_MAPS.items():
        bzl_version = bzlmod_deps[bzl_name]
        submodule_hash = submodule_map["third_party/" + submodule_name]
        bzl_commit = get_bcr_commit_hash(bzl_name, bzl_version)
        if bzl_commit is None:
            print(
                f"# Cannot find the commit hash from BCR for name={bzl_name} version={bzl_version}"
            )
            exit_code = 1
        if bzl_commit != submodule_hash:
            print(
                f"# Mistmatched commit hash for name={bzl_name} version={bzl_version} bzl_commit={bzl_commit} submodule_commit={submodule_hash}"
            )
            exit_code = 1
    sys.exit(exit_code)


if __name__ == "__main__":
    main()
