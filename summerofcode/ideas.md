# gRPC Summer of Code Project Ideas

Hello students!

We want gRPC to be the universal remote procedure call protocol for all
computing platforms and paradigms, so while these are our ideas of what we
think would make good projects for the summer, we're eager to hear your ideas
and proposals as well.
[Try us out](https://github.com/grpc/grpc/blob/master/CONTRIBUTING.md) and get
to know the gRPC code and team!

**Required skills for all projects:** git version control, collaborative
software development on github.com, and software development in at least one
of gRPC's ten languages on at least one of Linux, Mac OS X, and Windows.

-------------------------------------

gRPC C Core:

1. Port gRPC to  one of the major BSD platforms ([FreeBSD](https://freebsd.org), [NetBSD](https://netbsd.org), and [OpenBSD](https://openbsd.org)) and create packages for them. Add [kqueue](https://www.freebsd.org/cgi/man.cgi?query=kqueue) support in the process.
 * **Required skills:** C programming language, BSD operating system.
 * **Likely mentors:** [Craig Tiller](https://github.com/ctiller),
 [Nicolas Noble](https://github.com/nicolasnoble),
 [Vijay Pai](https://github.com/vjpai).
1. Fix gRPC C-core's URI parser. The current parser does not qualify as a standard parser according to [RFC3986]( https://tools.ietf.org/html/rfc3986). Write test suites to verify this and make changes necessary to make the URI parser compliant.
 * **Required skills:** C programming language, HTTP standard compliance.
 * **Likely mentors:** [Craig Tiller](https://github.com/ctiller).
1. HPACK compression efficiency evaluation - Figure out how to benchmark gRPC's compression efficiency (both in terms of bytes on the wire and cpu cycles). Implement benchmarks. Potentially extend this to other full-stack gRPC implementations (Java and Go).
 * **Required skills:** C programming language, software performance benchmarking, potentially Java and Go.
 * **Likely mentors:** [Craig Tiller](https://github.com/ctiller).


gRPC Python:

1. Port gRPC Python to [PyPy](http://pypy.org). Investigate the state of [Cython support](http://docs.cython.org/src/userguide/pypy.html) to do this or potentially explore [cffi](https://cffi.readthedocs.org/en/latest/).
 * **Required skills:** Python programming language, PyPy Python interpreter.
 * **Likely mentors:** [Nathaniel Manista](https://github.com/nathanielmanistaatgoogle), [Masood Malekghassemi](https://github.com/soltanmm).
1. Develop and test Python 3.5 Support for gRPC. Make necessary changes to port gRPC and package it for supported platforms.
 * **Required skills:** Python programming language, Python 3.5 interpreter.
 * **Likely mentors:** [Nathaniel Manista](https://github.com/nathanielmanistaatgoogle), [Masood Malekghassemi](https://github.com/soltanmm).
 
gRPC Ruby/Java:

1. [jRuby](http://jruby.org) support for gRPC. Develop a jRuby wrapper for gRPC based on grpc-java and ensure that it is API compatible with the existing Ruby implementation and passes all tests.
 * **Required skills:** Java programming language, Ruby programming language.
 * **Likely mentors:** [Michael Lumish](https://github.com/murgatroid99), [Eric Anderson](https://github.com/ejona86).


gRPC Wire Protocol:

1. Develop a [Wireshark](https://wireshark.org) plugin for the gRPC protocol. Provide documentation and tutorials for this plugin.
 * **Bonus:** consider set-up and use with mobile clients.
 * **Required skills:** Wireshark software.
 * **Likely mentors:** [Nicolas Noble](https://github.com/nicolasnoble).
