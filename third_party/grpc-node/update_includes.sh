#!/usr/bin/env bash

# Rewrites C++ #include statements so external files could be used

COMPILER_PACKAGE="src/compiler"
THIRD_PARTY_PACKAGE="third_party/grpc-node"

sed -i -e 's|#include "config.h"|#include "'$COMPILER_PACKAGE'/config.h"|g' $1
sed -i -e 's|#include "generator_helpers.h"|#include "'$COMPILER_PACKAGE'/generator_helpers.h"|g' $1
sed -i -e 's|#include "node_generator.h"|#include "'$THIRD_PARTY_PACKAGE'/node_generator.h"|g' $1
sed -i -e 's|#include "node_generator_helpers.h"|#include "'$THIRD_PARTY_PACKAGE'/node_generator_helpers.h"|g' $1
cp $1 $2
