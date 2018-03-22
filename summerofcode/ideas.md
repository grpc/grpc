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
of gRPC's ten languages on at least one of Linux, macOS, and Windows.

-------------------------------------

gRPC Core:

1. Implement ["early OK" semantics](https://github.com/grpc/grpc/issues/7032). The gRPC wire protocol allows servers to complete an RPC with OK status without having processed all requests ever sent to the client; it's the gRPC Core that currently restricts applications from so behaving. This behavioral gap in the gRPC Core should be filled in.
    * **Required skills:** C programming language, C++ programming language.
    * **Likely mentors:** [Nathaniel Manista](https://github.com/nathanielmanistaatgoogle), [Nicolas Noble](https://github.com/nicolasnoble).

1. [Make channel-connectivity-watching cancellable](https://github.com/grpc/grpc/issues/3064). Anything worth waiting for is worth cancelling. The fact that channel connectivity is currently poll-based means that clean shutdown of gRPC channels can take as long as the poll interval. No one should have to wait two hundred milliseconds to garbage-collect an object.
    * **Required skills:** C programming language, C++ programming language, Python programming language.
    * **Likely mentors:** [Nathaniel Manista](https://github.com/nathanielmanistaatgoogle), [Vijay Pai](https://github.com/vjpai).

gRPC Python:

1. Support static type-checking of both gRPC Python itself and of code that uses gRPC Python. No one likes dynamic typing and Python is finally outgrowing it! There are probably errors in the implementation of gRPC Python that [pytype](https://github.com/google/pytype) or [mypy](http://mypy-lang.org/) could detect. There are certainly errors in other code that uses gRPC Python that they could detect.
    * **Required skills:** Python programming language, open source development across multiple repositories and projects.
    * **Likely mentors:** [Nathaniel Manista](https://github.com/nathanielmanistaatgoogle), [Kailash Sethuraman](https://github.com/hsaliak).

1. [Enable building of gRPC Python with Bazel](https://github.com/grpc/grpc/issues/8079). Bazel is the designated replacement for our constellation of crufty build scripts, but it's still under active development itself. Up for a challenge? gRPC Python could easily be the most complex codebase to be built with Bazel.
    * **Required skills:** Python programming language, Bazel toolchain, Cython, open source development across multiple repositories and projects.
    * **Likely mentors:** [Nathaniel Manista](https://github.com/nathanielmanistaatgoogle).
