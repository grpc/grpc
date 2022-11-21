#!/usr/bin/env python3
# Copyright 2022 gRPC authors.
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
# FAKEPROTOC_PROJECTDIR - output directory for generated files and output file
# FAKEPROTOC_GENERATE_EXPECTED - list of expected generated files in format:
#         file1.proto:csfile1.cs;csfile2.cs|file2.proto:csfile3.cs;csfile4.cs|...
# FAKEPROTOC_TESTID - unique id for the test used as name for JSON results file

import datetime
import hashlib
import json
import os
import sys

# Set to True to write out debug messages from this script
dbg = True
dbgfile = None

# Env variable: output directory for generated files and output file
FAKEPROTOC_PROJECTDIR = None
# Env variable: list of expected generated files
FAKEPROTOC_GENERATE_EXPECTED = None
# Env variable: unique id for the test used as name for JSON results file
FAKEPROTOC_TESTID = None

protoc_args = []
protoc_args_dict = {}
results_json = {}

dependencyfile = None
grpcout = None
protofile = None
proto_to_generated = {}

def create_debug(filename):
    """ Create debug file for this script """
    global dbg
    global dbgfile
    if dbg:
        # append mode since this script may be called multiple times
        # during one build/test
        dbgfile = open(filename, "a")

def close_debug():
    """ Close the debug file """
    global dbgfile
    if not dbgfile is None:
        dbgfile.close()

def write_debug(msg):
    """ Write to the debug file if debug is enabled """
    global dbg
    global dbgfile
    if dbg and not dbgfile is None:
        print(msg, file=dbgfile, flush=True)

def read_protoc_arguments():
    """
    Read the protoc argument from the command line and
    any response files specified on the command line.

    Arguments are added to protoc_args for later parsing.
    """
    write_debug("\nread_protoc_arguments")
    global protoc_args
    for i in range(1, len(sys.argv), 1):
        arg = sys.argv[i]
        write_debug("  arg: "+arg)
        if arg.startswith("@"):
            protoc_args.append("# RSP file: "+arg)
            read_rsp_file(arg[1:])
        else:
            protoc_args(arg)

def read_rsp_file(rspfile):
    """
    Read arguments from a response file.

    Arguments are added to protoc_args for later parsing.
    """
    write_debug("\nread_rsp_file: "+rspfile)
    global protoc_args
    with open(rspfile, "r") as rsp:
        for line in rsp:
            line = line.strip()
            write_debug("    line: "+line)
            protoc_args.append(line)

def parse_protoc_arguments():
    """
    Parse the protoc arguments that are in protoc_args
    """
    global protoc_args
    global dependencyfile
    global grpcout
    global protofile

    write_debug("\nparse_protoc_arguments")
    for arg in protoc_args:
        if dbg:
            write_debug("Parsing: "+arg)

        # All arguments containing file or directory paths are
        # normalised by converting to relative paths to the
        # project directory, and all '\' and changed to '/'
        if arg.startswith("--"):
            (name, value) = arg.split("=",1)

            if name == "--dependency_out":
                value = relative_to_project(value)
                dependencyfile = value
            elif name == "--grpc_out":
                value = relative_to_project(value)
                grpcout = value
            elif name in [ "--grpc_out", "--proto_path", "--csharp_out" ]:
                value = relative_to_project(value)

            add_protoc_arg_to_dict(name, value)

        elif arg.startswith("#"):
            pass # ignore
        else:
            # proto file name
            protofile = relative_to_project(arg)
            add_protoc_arg_to_dict("protofile", protofile)

def add_protoc_arg_to_dict(name, value):
    """
    Add the arguments with name/value to protoc_args_dict

    protoc_args_dict is later used from writing out the JSON
    results file
    """
    global protoc_args_dict
    if name in protoc_args_dict:
        values = protoc_args_dict[name]
        values.append(value)
    else:
        protoc_args_dict[name] = [ value ]

def relative_to_project(file):
    """ Convert a file path to one relative to the project directory """
    return normalise_slashes(os.path.relpath(os.path.abspath(file), FAKEPROTOC_PROJECTDIR))

def normalise_slashes(path):
    """ Change all backslashes to forward slashes """
    return path.replace("\\","/")

def write_results_json(pf):
    """ Write out the results JSON file """
    global protoc_args_dict
    global results_json
    global FAKEPROTOC_PROJECTDIR

    # Read existing json.
    # Since protoc may be called more than once each build/test if there is
    # more than one protoc file, we read the existing data to add to it.
    fname = os.path.abspath(FAKEPROTOC_PROJECTDIR+"/log/"+FAKEPROTOC_TESTID+".json")
    if os.path.isfile(fname):
        results_json = json.load(open(fname,"r"))
        protoc_files_dict = results_json.get("Files")
    else:
        results_json = {}
        protoc_files_dict = {}
        results_json["Files"] = protoc_files_dict
    
    protofiles = protoc_args_dict.get("protofile")
    if protofiles is None:
        key = "NONE"
    else:
        key = protofiles[0]
    results_json["Metadata"] = { "timestamp": str(datetime.datetime.now()) }
    protoc_files_dict[key] = protoc_args_dict

    with open(fname, "w") as out:
        json.dump(results_json, out, indent=4)

def parse_generated_expected():
    """
    Parse FAKEPROTOC_GENERATE_EXPECTED that specifies the proto files
    and the cs files to generate. We rely on the test to say what is
    expected rather than trying to work it out in this script.

    The format of the input is:
        file1.proto:csfile1.cs;csfile2.cs|file2.proto:csfile3.cs;csfile4.cs|...
    """
    write_debug("\nparse_generated_expected")
    global FAKEPROTOC_GENERATE_EXPECTED
    global proto_to_generated

    protos = FAKEPROTOC_GENERATE_EXPECTED.split("|")
    for s in protos:
        parts = s.split(":")
        pfile = normalise_slashes(parts[0])
        csfiles = parts[1].split(";")
        proto_to_generated[pfile] = csfiles
        write_debug(pfile + " : " + str(csfiles))

def generate_cs_files(protoname):
    """
    Create expected cs files.
    """
    write_debug("\ngenerate_cs_files")
    global proto_to_generated

    to_match = normalise_slashes(protoname)
    to_generate = proto_to_generated.get(to_match)
    if to_generate is None:
        print("generate_cs_files: None matching proto file name "+protoname)
    else:
        if os.path.isabs(grpcout):
            dir = grpcout
        else:
            dir = os.path.abspath(FAKEPROTOC_PROJECTDIR + "/" + grpcout)
        timestamp = str(datetime.datetime.now())
        for csfile in to_generate:
            if not os.path.isdir(dir):
                os.makedirs(dir)
            file = dir + "/" + csfile
            write_debug("Creating: "+file)
            with open(file, "w") as out:
                print("// Generated my fake protoc: "+ timestamp, file=out)

def create_dependency_file(protoname):
    """ Create the expected dependecy file """
    write_debug("\ncreate_dependency_file")
    global proto_to_generated
    global FAKEPROTOC_PROJECTDIR

    if dependencyfile is None:
        write_debug("dependencyfile is None")
        return

    to_match = normalise_slashes(protoname)
    to_generate = proto_to_generated.get(to_match)
    
    if to_generate is None:
        write_debug("No matching proto file name "+protoname)
    else:
        write_debug("Creating dependency file: "+dependencyfile)
        with open(dependencyfile, "w") as out:
            nfiles = len(to_generate)
            for i in range(0, nfiles):
                file = os.path.join(FAKEPROTOC_PROJECTDIR, grpcout, to_generate[i])
                if i == nfiles-1:
                    print(file+": "+protoname, file=out)
                else:
                    print(file+" \\", file=out)

def main():
    global FAKEPROTOC_PROJECTDIR
    global FAKEPROTOC_GENERATE_EXPECTED
    global FAKEPROTOC_TESTID
    global dbg
    global protofile

    # Check environment variables for the additional arguments used in the tests.
    # Note there is a bug in .NET core 3.x that lowercases the environment
    # variable names when they are added via Process.StartInfo, so we need to
    # check both cases here (only an issue on Linux which is case sensitive)
    FAKEPROTOC_PROJECTDIR = os.getenv('FAKEPROTOC_PROJECTDIR')
    if FAKEPROTOC_PROJECTDIR is None:
        FAKEPROTOC_PROJECTDIR = os.getenv('fakeprotoc_projectdir')
    if FAKEPROTOC_PROJECTDIR is None:
        print("FAKEPROTOC_PROJECTDIR not set")
        sys.exit(1)
    FAKEPROTOC_PROJECTDIR = os.path.abspath(FAKEPROTOC_PROJECTDIR)

    FAKEPROTOC_GENERATE_EXPECTED = os.getenv('FAKEPROTOC_GENERATE_EXPECTED')
    if FAKEPROTOC_GENERATE_EXPECTED is None:
        FAKEPROTOC_GENERATE_EXPECTED = os.getenv('fakeprotoc_generate_expected')
    if FAKEPROTOC_GENERATE_EXPECTED is None:
        print("FAKEPROTOC_GENERATE_EXPECTED not set")
        sys.exit(1)

    FAKEPROTOC_TESTID = os.getenv('FAKEPROTOC_TESTID')
    if FAKEPROTOC_TESTID is None:
        FAKEPROTOC_TESTID = os.getenv('fakeprotoc_testid')
    if FAKEPROTOC_TESTID is None:
        print("FAKEPROTOC_TESTID not set")
        sys.exit(1)

    create_debug(FAKEPROTOC_PROJECTDIR+"/log/fakeprotoc-dbg.txt")

    timestamp = str(datetime.datetime.now())
    if dbg:
        write_debug("##### fakeprotoc called "+timestamp+"\n"
            + "FAKEPROTOC_PROJECTDIR = "+FAKEPROTOC_PROJECTDIR+"\n"
            + "FAKEPROTOC_GENERATE_EXPECTED = "+FAKEPROTOC_GENERATE_EXPECTED+"\n"
            + "FAKEPROTOC_TESTID = "+FAKEPROTOC_TESTID)
    
    parse_generated_expected()
    read_protoc_arguments()
    parse_protoc_arguments()
    create_dependency_file(protofile)
    generate_cs_files(protofile)
    write_results_json(protofile)

    close_debug()

if __name__ == "__main__":
    main()
