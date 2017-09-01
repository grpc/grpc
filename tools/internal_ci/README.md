# Internal continuous integration

gRPC's externally facing testing is managed by Jenkins CI (see `tools/jenkins`
directory). Nevertheless, some of the tests are better suited for being run
on internal infrastructure and using an internal CI system. Configuration for
such tests is under this directory.
