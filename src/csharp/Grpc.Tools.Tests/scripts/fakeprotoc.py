#!/usr/bin/env python3
# Copyright 2022 The gRPC Authors
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

# Fake protobuf compiler for use in the Grpc.Tools MSBuild integration
# unit tests.  Its purpose is to be called from the Grpc.Tools
# Google.Protobuf.Tools.targets MSBuild file instead of the actual protoc
# compiler. This script:
# - parses the command line arguments
# - generates expected dependencies file
# - generates dummy .cs files that are expected by the tests
# - writes a JSON results file containing the arguments passed in

# Configuration is done via environment variables as it is not possible
# to pass additional argument when called from the MSBuild scripts under test.
#
# Environment variables:
# FAKEPROTOC_PROJECTDIR - project directory
# FAKEPROTOC_OUTDIR - output directory for generated files and output file
# FAKEPROTOC_GENERATE_EXPECTED - list of expected generated files in format:
#         file1.proto:csfile1.cs;csfile2.cs|file2.proto:csfile3.cs;csfile4.cs|...

import datetime
import hashlib
import json
import os
import sys

# Set to True to write out debug messages from this script
_dbg = True
# file to which write the debug log
_dbgfile = None


def _open_debug_log(filename):
    """Create debug file for this script."""
    global _dbgfile
    if _dbg:
        # append mode since this script may be called multiple times
        # during one build/test
        _dbgfile = open(filename, "a")


def _close_debug_log():
    """Close the debug log file."""
    if _dbgfile:
        _dbgfile.close()


def _write_debug(msg):
    """Write to the debug log file if debug is enabled."""
    if _dbg and _dbgfile:
        print(msg, file=_dbgfile, flush=True)


def _read_protoc_arguments():
    """
    Get the protoc argument from the command line and
    any response files specified on the command line.

    Returns the list of arguments.
    """
    _write_debug("\nread_protoc_arguments")
    result = []
    for arg in sys.argv[1:]:
        _write_debug("  arg: " + arg)
        if arg.startswith("@"):
            # TODO(jtattermusch): inserting a "commented out" argument feels hacky
            result.append("# RSP file: %s" % arg)
            rsp_file_name = arg[1:]
            result.extend(_read_rsp_file(rsp_file_name))
        else:
            result.append(arg)
    return result


def _read_rsp_file(rspfile):
    """
    Returns list of arguments from a response file.
    """
    _write_debug("\nread_rsp_file: " + rspfile)
    result = []
    with open(rspfile, "r") as rsp:
        for line in rsp:
            line = line.strip()
            _write_debug("    line: " + line)
            result.append(line)
    return result


def _parse_protoc_arguments(protoc_args, projectdir):
    """
    Parse the protoc arguments from the provided list
    """

    _write_debug("\nparse_protoc_arguments")
    arg_dict = {}
    for arg in protoc_args:
        _write_debug("Parsing: %s" % arg)

        # All arguments containing file or directory paths are
        # normalized by converting all '\' and changed to '/'
        if arg.startswith("--"):
            # Assumes that cmdline arguments are always passed in the
            # "--somearg=argvalue", which happens to be the form that
            # msbuild integration uses, but it's not the only way.
            (name, value) = arg.split("=", 1)

            if (
                name == "--dependency_out"
                or name == "--grpc_out"
                or name == "--csharp_out"
            ):
                # For args that contain a path, make the path absolute and normalize it
                # to make it easier to assert equality in tests.
                value = _normalized_absolute_path(value)

            if name == "--proto_path":
                # for simplicity keep this one as relative path rather than absolute path
                # since it is an input file that is always be near the project file
                value = _normalized_relative_to_projectdir(value, projectdir)

            _add_protoc_arg_to_dict(arg_dict, name, value)

        elif arg.startswith("#"):
            pass  # ignore
        else:
            # arg represents a proto file name
            arg = _normalized_relative_to_projectdir(arg, projectdir)
            _add_protoc_arg_to_dict(arg_dict, "protofile", arg)
    return arg_dict


def _add_protoc_arg_to_dict(arg_dict, name, value):
    """
    Add the arguments with name/value to a multi-dictionary of arguments
    """
    if name not in arg_dict:
        arg_dict[name] = []

    arg_dict[name].append(value)


def _normalized_relative_to_projectdir(file, projectdir):
    """Convert a file path to one relative to the project directory."""
    try:
        return _normalize_slashes(
            os.path.relpath(os.path.abspath(file), projectdir)
        )
    except ValueError:
        # On Windows if the paths are on different drives then we get this error
        # Just return the absolute path
        return _normalize_slashes(os.path.abspath(file))


def _normalized_absolute_path(file):
    """Returns normalized absolute path to file."""
    return _normalize_slashes(os.path.abspath(file))


def _normalize_slashes(path):
    """Change all backslashes to forward slashes to make comparing path strings easier."""
    return path.replace("\\", "/")


def _write_or_update_results_json(log_dir, protofile, protoc_arg_dict):
    """Write or update the results JSON file"""

    # Read existing json.
    # Since protoc may be called more than once each build/test if there is
    # more than one protoc file, we read the existing data to add to it.
    fname = os.path.abspath("%s/results.json" % log_dir)
    if os.path.isfile(fname):
        # Load the original contents.
        with open(fname, "r") as forig:
            results_json = json.load(forig)
    else:
        results_json = {}
        results_json["Files"] = {}

    results_json["Files"][protofile] = protoc_arg_dict
    results_json["Metadata"] = {"timestamp": str(datetime.datetime.now())}

    with open(fname, "w") as fout:
        json.dump(results_json, fout, indent=4)


def _parse_generate_expected(generate_expected_str):
    """
    Parse FAKEPROTOC_GENERATE_EXPECTED that specifies the proto files
    and the cs files to generate. We rely on the test to say what is
    expected rather than trying to work it out in this script.

    The format of the input is:
        file1.proto:csfile1.cs;csfile2.cs|file2.proto:csfile3.cs;csfile4.cs|...
    """
    _write_debug("\nparse_generate_expected")

    result = {}
    entries = generate_expected_str.split("|")
    for entry in entries:
        parts = entry.split(":")
        pfile = _normalize_slashes(parts[0])
        csfiles = parts[1].split(";")
        result[pfile] = csfiles
        _write_debug(pfile + " : " + str(csfiles))
    return result


def _get_cs_files_to_generate(protofile, proto_to_generated):
    """Returns list of .cs files to generated based on FAKEPROTOC_GENERATE_EXPECTED env."""
    protoname_normalized = _normalize_slashes(protofile)
    cs_files_to_generate = proto_to_generated.get(protoname_normalized)
    return cs_files_to_generate


def _is_grpc_out_file(csfile):
    """Return true if the file is one that would be generated by gRPC plugin"""
    # This is using the heuristics of checking that the name of the file
    # matches *Grpc.cs which is the name that the gRPC plugin would produce.
    return csfile.endswith("Grpc.cs")


def _generate_cs_files(
    protofile, cs_files_to_generate, grpc_out_dir, csharp_out_dir, projectdir
):
    """Create expected cs files."""
    _write_debug("\ngenerate_cs_files")

    if not cs_files_to_generate:
        _write_debug("No .cs files matching proto file name %s" % protofile)
        return

    if not os.path.isabs(grpc_out_dir):
        # if not absolute, it is relative to project directory
        grpc_out_dir = os.path.abspath("%s/%s" % (projectdir, grpc_out_dir))

    if not os.path.isabs(csharp_out_dir):
        # if not absolute, it is relative to project directory
        csharp_out_dir = os.path.abspath("%s/%s" % (projectdir, csharp_out_dir))

    # Ensure directories exist
    if not os.path.isdir(grpc_out_dir):
        os.makedirs(grpc_out_dir)

    if not os.path.isdir(csharp_out_dir):
        os.makedirs(csharp_out_dir)

    timestamp = str(datetime.datetime.now())
    for csfile in cs_files_to_generate:
        if csfile.endswith("Grpc.cs"):
            csfile_fullpath = "%s/%s" % (grpc_out_dir, csfile)
        else:
            csfile_fullpath = "%s/%s" % (csharp_out_dir, csfile)
        _write_debug("Creating: %s" % csfile_fullpath)
        with open(csfile_fullpath, "w") as fout:
            print("// Generated by fake protoc: %s" % timestamp, file=fout)


def _create_dependency_file(
    protofile,
    cs_files_to_generate,
    dependencyfile,
    grpc_out_dir,
    csharp_out_dir,
):
    """Create the expected dependency file."""
    _write_debug("\ncreate_dependency_file")

    if not dependencyfile:
        _write_debug("dependencyfile is not set.")
        return

    if not cs_files_to_generate:
        _write_debug("No .cs files matching proto file name %s" % protofile)
        return

    _write_debug("Creating dependency file: %s" % dependencyfile)
    with open(dependencyfile, "w") as out:
        nfiles = len(cs_files_to_generate)
        for i in range(0, nfiles):
            csfile = cs_files_to_generate[i]
            if csfile.endswith("Grpc.cs"):
                cs_filename = os.path.join(grpc_out_dir, csfile)
            else:
                cs_filename = os.path.join(csharp_out_dir, csfile)
            if i == nfiles - 1:
                print("%s: %s" % (cs_filename, protofile), file=out)
            else:
                print("%s \\" % cs_filename, file=out)


def _getenv(name):
    # Note there is a bug in .NET core 3.x that lowercases the environment
    # variable names when they are added via Process.StartInfo, so we need to
    # check both cases here (only an issue on Linux which is case sensitive)
    value = os.getenv(name)
    if value is None:
        value = os.getenv(name.lower())
    return value


def _get_argument_last_occurrence_or_none(protoc_arg_dict, name):
    # If argument was passed multiple times, take the last occurrence.
    # If the value does not exist then return None
    values = protoc_arg_dict.get(name)
    if values is not None:
        return values[-1]
    return None


def main():
    # Check environment variables for the additional arguments used in the tests.

    projectdir = _getenv("FAKEPROTOC_PROJECTDIR")
    if not projectdir:
        print("FAKEPROTOC_PROJECTDIR not set")
        sys.exit(1)
    projectdir = os.path.abspath(projectdir)

    # Output directory for generated files and output file
    protoc_outdir = _getenv("FAKEPROTOC_OUTDIR")
    if not protoc_outdir:
        print("FAKEPROTOC_OUTDIR not set")
        sys.exit(1)
    protoc_outdir = os.path.abspath(protoc_outdir)

    # Get list of expected generated files from env variable
    generate_expected = _getenv("FAKEPROTOC_GENERATE_EXPECTED")
    if not generate_expected:
        print("FAKEPROTOC_GENERATE_EXPECTED not set")
        sys.exit(1)

    # Prepare the debug log
    log_dir = os.path.join(protoc_outdir, "log")
    if not os.path.isdir(log_dir):
        os.makedirs(log_dir)
    _open_debug_log("%s/fakeprotoc_log.txt" % log_dir)

    _write_debug(
        (
            "##### fakeprotoc called at %s\n"
            + "FAKEPROTOC_PROJECTDIR = %s\n"
            + "FAKEPROTOC_GENERATE_EXPECTED = %s\n"
        )
        % (datetime.datetime.now(), projectdir, generate_expected)
    )

    proto_to_generated = _parse_generate_expected(generate_expected)
    protoc_args = _read_protoc_arguments()
    protoc_arg_dict = _parse_protoc_arguments(protoc_args, projectdir)

    # If argument was passed multiple times, take the last occurrence of it.
    # TODO(jtattermusch): handle multiple occurrences of the same argument
    dependencyfile = _get_argument_last_occurrence_or_none(
        protoc_arg_dict, "--dependency_out"
    )
    grpcout = _get_argument_last_occurrence_or_none(
        protoc_arg_dict, "--grpc_out"
    )
    csharpout = _get_argument_last_occurrence_or_none(
        protoc_arg_dict, "--csharp_out"
    )

    # --grpc_out might not be set in which case use --csharp_out
    if grpcout is None:
        grpcout = csharpout

    if len(protoc_arg_dict.get("protofile")) != 1:
        # regular protoc can process multiple .proto files passed at once, but we know
        # the Grpc.Tools msbuild integration only ever passes one .proto file per invocation.
        print(
            "Expecting to get exactly one .proto file argument per fakeprotoc"
            " invocation."
        )
        sys.exit(1)
    protofile = protoc_arg_dict.get("protofile")[0]

    cs_files_to_generate = _get_cs_files_to_generate(
        protofile=protofile, proto_to_generated=proto_to_generated
    )

    _create_dependency_file(
        protofile=protofile,
        cs_files_to_generate=cs_files_to_generate,
        dependencyfile=dependencyfile,
        grpc_out_dir=grpcout,
        csharp_out_dir=csharpout,
    )

    _generate_cs_files(
        protofile=protofile,
        cs_files_to_generate=cs_files_to_generate,
        grpc_out_dir=grpcout,
        csharp_out_dir=csharpout,
        projectdir=projectdir,
    )

    _write_or_update_results_json(
        log_dir=log_dir, protofile=protofile, protoc_arg_dict=protoc_arg_dict
    )

    _close_debug_log()


if __name__ == "__main__":
    main()
