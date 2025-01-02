Yodel is a foundational test framework for unit testing parts of a call.

It provides infrastructure to write tests around some call actor (the various
frameworks built atop of it specify what that actor is). It also provides
utilities to fill in various call details, interact with promises in a
way that's convenient to debug, and run as either a unit test or a fuzzer.

Various frameworks are built atop it:
- transports use it as part of the transport test_suite

Planned:
- interceptors & filters should also use this
