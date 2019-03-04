# Kokoro CI job configurations and testing scripts

gRPC uses a continuous integration tool called "Kokoro" (a.k.a "internal CI")
for running majority of its open source tests.
This directory contains the external part of kokoro test job configurations
(the actual job definitions live in an internal repository) and the shell
scripts that act as entry points to execute the actual tests.
