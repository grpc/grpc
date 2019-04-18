Project Overview
================
The project, titled 'GRPC Python compatibility support', involved 
collaborating with the GRPC team to improve the library compatibility 
for the GRPC Python library.

Python is, originally, a specification for a programming language. This
specification has been implemented differently in different
implementations of the [language specification](https://docs.python.org/3/reference/). 

A small, and by no means exhaustive, list of some major python implementations
is:

- [CPython](https://www.python.org/): The reference implementation
- [Jython](http://www.jython.org/): Python implemented in Java
- [Python for .NET](http://pythonnet.sourceforge.net/): CPython implementation that enables .NET library usage
- [IronPython](http://ironpython.net/): Python implemented in .NET
- [PyPy](http://pypy.org/): Python implemented completely in Python
- [Stackless](https://bitbucket.org/stackless-dev/stackless/wiki/Home): Replaces the dependency for the C call stack with it's own stack

The development in this project revolved around
introducing changes to the codebase that enable support for latest 
stable as well as development releases of the reference implementation 
(CPython) of the Python programming language namely `Python 3.4`,
`Python 3.5`,and `Python 3.6` as well as the stable releases of the 
PyPy implementation. Special changes were required to enable PyPy 
support because PyPy has a non-deterministic garbage collector that does
not rely on reference counting unlike the CPython garbage collector.

The changes to the codebase involved changes to the library code as well
as changes to the tests and scripts in the test infrastructure which
resulted in both the library as well as the testing infrastructure being
Python 3.x and PyPy compatible.

The list of merged commits, as of 22.08.2016 23:59 CEST,  is summarized 
here for the perusal of those interested:

- [Enable py35 and py36 testing](https://github.com/grpc/grpc/commit/c478214e475e103c5cdf477f0adc18bba2c03903)
- [Make testing toolchain python 3.x compliant](https://github.com/grpc/grpc/commit/0589e533cd65a2ca9e0e610cc1b284d016986572)
- [Add .idea folder to .gitignore](https://github.com/grpc/grpc/commit/365ef40947e22b5438a63f123679ae9a5474c47c)
- [Fix the ThreadPoolExecutor: max_workers can't be 0](https://github.com/grpc/grpc/commit/de84d566b8fad6808e5263a25a17fa231cb5713c)
- [Add PyPy to testing toolchain](https://github.com/grpc/grpc/commit/2135a1b557f8b992186d5317cb767ac4dbcdfe5c)
- [Switch init/shutdown: lib-wide -> per-object](https://github.com/grpc/grpc/commit/9eedb4ffd74aed8d246a07f8007960b2bc167f55)
- [Skip test run if running with pypy](https://github.com/grpc/grpc/commit/f0f58e68738abbc317f7f449c5104f7fbbff26bd)

The list of unmerged pull requests is as follows:

- [Add PyPy 5.3.1 to dockerfile and template](https://github.com/grpc/grpc/pull/7763)
- [remove skipIf from TypeSmokeTest (issue 7672)](https://github.com/grpc/grpc/pull/7831)
  
The list of tasks that have pending unsubmitted pull requests is as follows:

- Modify run_tests.py to enable testing of new languages without
  affecting old branches.


Project Details
===============
- Title: GRPC Python compatibility support
- Student: [Siddharth Shukla](https://github.com/thunderboltsid)
- Mentors: [Nathaniel Manista](https://github.com/nathanielmanistaatgoogle), [Masood Malekghassemi](https://github.com/soltanmm)
- Duration: May 23 - August 23
- Hat tip: [Ken Payson](https://github.com/kpayson64), [Jan Tattermusch](https://github.com/jtattermusch), [Nicolas Noble](https://github.com/nicolasnoble)


