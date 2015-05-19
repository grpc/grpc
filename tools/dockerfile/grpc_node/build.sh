#!/bin/bash
cp -R /var/local/git-clone/grpc /var/local/git

make clean -C /var/local/git/grpc

make install_c -j12 -C /var/local/git/grpc

cd /var/local/git/grpc/src/node && npm install && node-gyp rebuild
