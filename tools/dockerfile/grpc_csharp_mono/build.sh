#!/bin/bash

cp -R /var/local/git-clone/grpc /var/local/git

make install_grpc_csharp_ext -j12 -C /var/local/git/grpc

cd /var/local/git/grpc/src/csharp && mono /var/local/NuGet.exe restore Grpc.sln

cd /var/local/git/grpc/src/csharp && xbuild Grpc.sln

