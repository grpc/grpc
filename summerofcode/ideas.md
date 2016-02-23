# gRPC Summer of Code Project Ideas

C Core:

1. Port gRPC to  one of (Free, Net, Open) BSD platforms and create packages for them. Add kqueue support in the process.
2. Fix gRPC C-core's URI parser. The current parser does not qualify as a standard parser according to [RFC3986]( https://tools.ietf.org/html/rfc3986). Write test suites to verify this and make changes necessary to make the URI parser compliant.
3. HPACK compression efficiency evaluation - Figure out how to benchmark gRPC's compression efficiency (both in terms of bytes on the wire and cpu cycles). Implement benchmarks. Potentially extend this to other standalone implementations -- Java and Go.


gRPC Python:

 1. Evaluate the port of gRPC's Python implementation to PyPy. Investigate the state of [Cython support](http://docs.cython.org/src/userguide/pypy.html) to do this or potentially explore cffi
 2. Develop and test Python 3.5 Support for gRPC. Make necessary changes to port gRPC and package it for supported platforms.
 
gRPC Ruby/Java:

1. jRuby support for gRPC. Develop a jRuby wrapper for gRPC based on grpc-java and ensure that it is API compatible with the existing Ruby implementation and passes all tests.


Other:

1. Develop a Wireshark plugin for the gRPC protocol. Provide documentation and tutorials for this plugin. Bonus: consider set-up and use with the mobile clients.
