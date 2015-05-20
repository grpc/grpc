#!/bin/bash
cp -R /var/local/git-clone/grpc /var/local/git

make clean -C /var/local/git/grpc

make install_c -j12 -C /var/local/git/grpc

/bin/bash -l -c 'cd /var/local/git/grpc/src/ruby && gem update bundler && bundle && rake'
