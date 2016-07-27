#!/bin/sh

#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements. See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership. The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License. You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied. See the License for the
# specific language governing permissions and limitations
# under the License.
#

../../../../compiler/cpp/thrift --gen csharp  ../../../../contrib/async-test/aggr.thrift
../../../../compiler/cpp/thrift --gen csharp  ../../../rb/benchmark/Benchmark.thrift
gmcs /t:library /out:./ThriftImpl.dll /recurse:./gen-csharp/* /reference:../../Thrift.dll Multiplex.Test.Common.cs
gmcs  /out:MultiplexClient.exe /reference:../../Thrift.dll /reference:ThriftImpl.dll Client/Multiplex.Test.Client.cs
gmcs  /out:MultiplexServer.exe /reference:../../Thrift.dll /reference:ThriftImpl.dll Server/Multiplex.Test.Server.cs



export MONO_PATH=../../

timeout 120 ./MultiplexServer.exe &
sleep 3;
./MultiplexClient.exe
