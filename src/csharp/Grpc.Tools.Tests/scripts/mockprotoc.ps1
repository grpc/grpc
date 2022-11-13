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

# For debugging this script set this to $true and debug output will be 
# written to $TMP/mockprotoc-dbg.txt
$dbg = $true

if ( "$env:MOCKPROTOC_PROJECTDIR" -eq "")
{
    Write-Output "MOCKPROTOC_PROJECTDIR not set"
    Exit 1
}

if ( "$env:MOCKPROTOC_GENERATE_EXPECTED" -eq "")
{
    Write-Output "MOCKPROTOC_GENERATE_EXPECTED not set"
    Exit 1
}

$timestamp = Get-Date -Format "yyyy-MM-dd HH:mm:ss.FFF"
$dbgfile = "$env:MOCKPROTOC_PROJECTDIR/log/mockprotoc-dbg.txt"
if ($dbg)
{
    if ( -not ( Test-Path $dbgfile ))
    {
        $null = New-Item -Force -Type File $dbgfile
    }
    Write-Output "##### mockprotoc called $timestamp" >> $dbgfile
    Write-Output "MOCKPROTOC_PROJECTDIR = $env:MOCKPROTOC_PROJECTDIR"  >> $dbgfile
    Write-Output "MOCKPROTOC_GENERATE_EXPECTED = $env:MOCKPROTOC_GENERATE_EXPECTED"  >> $dbgfile
}

# read arguments and expand arguments files (response files)
$protocArgs = New-Object System.Collections.Generic.List[string]
foreach ($arg in $args)
{
    if ($dbg) { Write-Output "Argument: $arg" >> $dbgfile }
    if ( $arg.StartsWith("@") )
    {
        $protocArgs.Add("# RSP file: $arg")
        $file = $arg.Substring(1)
        foreach($line in Get-Content "$file")
        {
            if ($dbg) { Write-Output "Argument: $line" >> $dbgfile }
            $protocArgs.Add($line)
        }
    }
    else
    {
        $protocArgs.Add($arg)
    }
}

# parse the expanded arguments
$dependencyfile = ""
$grpcout = ""
$protocFile = ""

foreach ($arg in $protocArgs)
{
    if ( $arg.StartsWith("--dependency_out=") )
    {
        $dependencyfile = $arg.Substring(17)
        if ($dbg) { Write-Output "Dependency file: $dependencyfile" >> $dbgfile }
    }
    elseif ( $arg.StartsWith("--grpc_out=") )
    {
        $grpcout = $arg.Substring(11)
        if ($dbg) { Write-Output "Generated dir: $grpcout" >> $dbgfile }
    }
    elseif ( $arg.StartsWith("--"))
    {
        # ignore
    }
    elseif ( $arg.StartsWith("#"))
    {
        # ignore
    }
    else
    {
        # proto file name
        $protocFile = $arg
        if ($dbg) { Write-Output "Protoc file:  $arg" >> $dbgfile }
    }
}

# write file with arguments
$cleanFilename = $protocFile.replace("\","-").replace("/","-")
$outfile = "$env:MOCKPROTOC_PROJECTDIR/log/args-$cleanFilename.log"
$timestamp = Get-Date -Format "yyyy-MM-dd HH:mm:ss.FFF"

if ( -not ( Test-Path $outfile ))
{
    $null = New-Item -Force -Type File $outfile
}

Write-Output "##### mockprotoc called $timestamp" >> $outfile

foreach ($arg in $protocArgs)
{
    Write-Output "$arg" >> $outfile
}

# create expected generated files
$generatedProtosLists = $env:MOCKPROTOC_GENERATE_EXPECTED.split("|")
$toMatch = $protocFile.replace("\","/")

foreach ($generatedProtoFiles in $generatedProtosLists)
{
    $parts = $generatedProtoFiles.split(":")
    $pFile = $parts[0].replace("\","/")
    if ( $pFile -eq $toMatch)
    {
        $generatedfiles = $parts[1].split(";")
    }
}

foreach ($filename in $generatedfiles )
{
    $file = [IO.Path]::Combine("$Env:MOCKPROTOC_PROJECTDIR", "$grpcout", "$filename")
    if ($dbg) { Write-Output "Generated file: $file" >> $dbgfile }
    $null = New-Item -Force -Type File $file
    Write-Output "// Generated my mock protoc: $timestamp" >> $file
}

# create dependency file
if ( $dependencyfile -ne "" )
{
    if ($dbg) { Write-Output "Writing dependency file: $dependencyfile" >> $dbgfile }
    $null = New-Item -Force -Type File $dependencyfile
    $len = $generatedfiles.Length
    for ( $i = 0; $i -lt $len ; $i++)
    {
        $file = [IO.Path]::Combine($Env:MOCKPROTOC_PROJECTDIR, $grpcout, $generatedfiles[$i])
        if ($i -eq $len - 1)
        {
            if ($dbg) { Write-Output "DEP: ${file}: $protocFile" >> $dbgfile }
            Write-Output "${file}: $protocFile" >> $dependencyfile
        } else {
            if ($dbg) { Write-Output "DEP: ${file} \" >> $dbgfile }
            Write-Output "${file} \" >> $dependencyfile
        }
    }
}
