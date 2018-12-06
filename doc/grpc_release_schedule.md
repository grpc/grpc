# gRPC Release Schedule

Below is the release schedule for gRPC [Java](https://github.com/grpc/grpc-java/releases), [Go](https://github.com/grpc/grpc-go/releases) and [Core](https://github.com/grpc/grpc/releases) and its  dependent languages C++, C#, Objective-C, PHP, Python and Ruby.

Releases are scheduled every six weeks on Tuesdays on a best effort basis. In some unavoidable situations a release may be delayed or a language may skip a release altogether and do the next release to catch up with other languages. See the past releases in the links above. A six-week cycle gives us a good balance between delivering new features/fixes quickly and keeping the release overhead low.

Releases are cut from release branches. For Core and Java repos, the release branch is cut two weeks before the scheduled release date. For Go, the branch is cut just before the release. An RC (release candidate) is published for Core and its dependent languages just after the branch cut. This RC is later promoted to release version if no further changes are made to the release branch. We do our best to keep head of master branch stable at all times regardless of release schedule. Daily build packages from master branch for C#, PHP, Python, Ruby and Protoc plugins are published on [packages.grpc.io](https://packages.grpc.io/). If you depend on gRPC in production we recommend to set up your CI system to test the RCs and, if possible, the daily builds.

Names of gRPC releases are [here](https://github.com/grpc/grpc/blob/master/doc/g_stands_for.md).

Release |Scheduled Branch Cut|Scheduled Release Date
--------|--------------------|-------------
v1.17.0 |Nov 19, 2018   |Dec 4, 2018
v1.18.0 |Jan 2, 2019   |Jan 15, 2019
v1.19.0 |Feb 12, 2019   |Feb 26, 2019
v1.20.0 |Mar 26, 2019   |Apr 9, 2019
v1.21.0 |May 7, 2019   |May 21, 2019
v1.22.0 |Jun 18, 2019   |Jul 2, 2019
v1.23.0 |Jul 30, 2019   |Aug 13, 2019
v1.24.0 |Sept 10, 2019   |Sept 24, 2019
v1.25.0 |Oct 22, 2019   |Nov 5, 2019
v1.26.0 |Dec 3, 2019   |Dec 17, 2019
