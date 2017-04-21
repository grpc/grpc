#Continuous integration

This directory contains configuration for running automated tests (continuous
integration). Currently, gRPC's externally facing testing is managed by
Jenkins CI. We run a comprehensive set of tests (unit, integration, interop,
performance, portability..) on each pull request and also periodically on
`master` and release branches. For the time being, the scripts run by Jenkins
are under the `tools/jenkins` directory.

Some of the tests are better suited for being run on internal infrastructure
and using an internal CI system. Configuration for these tests is under the
`internal` directory.