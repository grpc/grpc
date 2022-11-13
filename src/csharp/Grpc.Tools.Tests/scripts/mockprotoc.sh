#!/bin/bash
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

# Mock protobuf compiler script for use in unit tests
#  - writes arguments to a file
#  - creates fake generated cs files
#  - creates dependencies file
#
# Configuration is done via environment variables as it is not possible
# to pass additional argument when called from the MSBuild scripts under test.
#
# Environment variables:
# MOCKPROTOC_PROJECTDIR     output directory for generated files and output file
# MOCKPROTOC_GENERATE_EXPECTED  list of expected generated files in format:
#               proto.file:file;file|proto.file:file;file|...

dbg=1

[[ "$MOCKPROTOC_PROJECTDIR" == "" ]] && (echo "MOCKPROTOC_PROJECTDIR not set"; exit 1)
[[ "$MOCKPROTOC_GENERATE_EXPECTED" == "" ]] && (echo "MOCKPROTOC_GENERATE_EXPECTED not set"; exit 1)

timestamp=`date "+%Y%m%d-%H%M%S.%3N"`
dbgfile="$MOCKPROTOC_PROJECTDIR/log/mockprotoc-dbg.txt"
if [ $dbg -eq 1 ]
then
    echo "##### mockprotoc called $timestamp" > $dbgfile
    echo "MOCKPROTOC_PROJECTDIR = $MOCKPROTOC_PROJECTDIR"  >> $dbgfile
    echo "MOCKPROTOC_GENERATE_EXPECTED = $MOCKPROTOC_GENERATE_EXPECTED"  >> $dbgfile
fi

# read arguments and expand arguments files (response files)
protocArgs=()
for arg in "$@"
do
    [ $dbg -eq 1] && echo "Argument: $arg" >> $dbgfile
    if [[ $arg == @* ]]
    then
        protocArgs+=( "# RSP file: $arg" )
        file=${arg:1}
        while IFS= read -r line
        do
            [ $dbg -eq 1] && echo "Argument: $line" >> $dbgfile
            protocArgs+=( "# RSP file: $line" )
        done < "$file"
    else
        protocArgs+=( "$arg" )
    fi
done

# parse the expanded arguments
dependencyfile=""
grpcout=""
protocFile=""

for arg in "$protoArgs[@]"
do
    if [[ $arg =~ ^"--dependency_out=" ]]
    then
        dependencyfile=${arg:17}
        [ $dbg -eq 1] && echo "Dependency file: $dependencyfile" >> $dbgfile
    elif [[ $arg =~ ^"--grpc_out=" ]]
    then
        grpcout=${arg:11}
        [ $dbg -eq 1] && echo "Generated dir: $grpcout" >> $dbgfile
    elif [[ $arg =~ ^"--" ]]
    then
        # ignore
        :
    elif [[ $arg =~ ^"#" ]]
    then
        # ignore
        :
    else
        # protoc file name
        protocFile=$arg
        [ $dbg -eq 1 ] && echo "Protoc file: $arg" >> $dbgfile
    fi
done

# write file with arguments
cleanFilename=${protocFile//\\/-}
cleanFilename=${cleanFilename//\//-}
outfile="$MOCKPROTOC_PROJECTDIR/log/args-$cleanFilename.log"
timestamp=`date "+%Y%m%d-%H%M%S.%3N"`

echo "##### mockprotoc called $timestamp" >> $outfile

for arg in "$protoArgs[@]"
do
    echo "$arg" >> $outfile
done

# create expected generated files
IFS='|' read -r -a generatedProtosLists <<< "$MOCKPROTOC_GENERATE_EXPECTED"
toMatch=${protocFile//\\/\/}

for generatedProtoFiles in "$generatedProtosLists[@]"
do
    IFS=':' read -r -a parts <<< "$generatedProtoFiles"
    pFile=${parts[0]//\\/\/}
    if [[ "$pFile" == "$toMatch" ]]
    then
        IFS=';' read -r -a generatedfiles <<< "${parts[1]}"
    fi
done

for filename in "$generatedfiles[@]"
do
    file="$MOCKPROTOC_PROJECTDIR/$grpcout/$filename"
    [ $dbg -eq 1 ] && echo "Generated file: $file" >> $dbgfile
    echo "// Generated my mock protoc: $timestamp" > $file
done

# create dependency file
if [[ ! -z "$dependencyfile" ]]
then
    [ $dbg -eq 1 ] && echo "Writing dependency file: $dependencyfile" >> $dbgfile
    touch $dependencyfile
    len=${#generatedfiles[@]}
    last=$((len-1))
    i=0
    while [ $i -lt $len ]
    do
        file="$MOCKPROTOC_PROJECTDIR/$grpcout/$generatedfiles[$i]"
        if [ $i -eq $last ]
        then
            [ $dbg -eq 1 ] && echo "DEP: ${file}: $protocFile" >> $dbgfile
            echo "${file}: $protocFile" >> $dependencyfile
        else
            [ $dbg -eq 1 ] && echo "DEP: ${file} \\" >> $dbgfile
            echo "${file} \\" >> $dependencyfile
        fi
    done
fi

