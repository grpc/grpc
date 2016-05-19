gRPC Python Tools
=================

Package for gRPC Python tools.

Installation
------------

The gRPC Python tools package is available for Linux, Mac OS X, and Windows
running Python 2.7.

From PyPI
~~~~~~~~~

If you are installing locally...

::

  $ pip install grpcio-tools

Else system wide (on Ubuntu)...

::

  $ sudo pip install grpcio-tools

If you're on Windows make sure that you installed the :code:`pip.exe` component
when you installed Python (if not go back and install it!) then invoke:

::

  $ pip.exe install grpcio-tools

Windows users may need to invoke :code:`pip.exe` from a command line ran as
administrator.

n.b. On Windows and on Mac OS X one *must* have a recent release of :code:`pip`
to retrieve the proper wheel from PyPI. Be sure to upgrade to the latest
version!

You might also need to install Cython to handle installation via the source
distribution if gRPC Python's system coverage with wheels does not happen to
include your system.

From Source
~~~~~~~~~~~

Building from source requires that you have the Python headers (usually a
package named :code:`python-dev`) and Cython installed. It further requires a
GCC-like compiler to go smoothly; you can probably get it to work without
GCC-like stuff, but you may end up having a bad time.

::

  $ export REPO_ROOT=grpc  # REPO_ROOT can be any directory of your choice
  $ git clone https://github.com/grpc/grpc.git $REPO_ROOT
  $ cd $REPO_ROOT
  $ git submodule update --init

  $ cd tools/distrib/python/grpcio_tools
  $ python ../make_grpcio_tools.py

  # For the next command do `sudo pip install` if you get permission-denied errors
  $ pip install .

You cannot currently install Python from source on Windows. Things might work
out for you in MSYS2 (follow the Linux instructions), but it isn't officially
supported at the moment.

Troubleshooting
~~~~~~~~~~~~~~~

Help, I ...

* **... see a** :code:`pkg_resources.VersionConflict` **when I try to install
  grpc**

  This is likely because :code:`pip` doesn't own the offending dependency,
  which in turn is likely because your operating system's package manager owns
  it. You'll need to force the installation of the dependency:

  :code:`pip install --ignore-installed $OFFENDING_DEPENDENCY`

  For example, if you get an error like the following:

  ::

    Traceback (most recent call last):
    File "<string>", line 17, in <module>
     ...
    File "/usr/lib/python2.7/dist-packages/pkg_resources.py", line 509, in find
      raise VersionConflict(dist, req)
    pkg_resources.VersionConflict: (six 1.8.0 (/usr/lib/python2.7/dist-packages), Requirement.parse('six>=1.10'))

  You can fix it by doing:

  ::

    sudo pip install --ignore-installed six

* **... see compiler errors on some platforms when either installing from source or from the source distribution**

  If you see

  ::

    /tmp/pip-build-U8pSsr/cython/Cython/Plex/Scanners.c:4:20: fatal error: Python.h: No such file or directory
    #include "Python.h"
                    ^
    compilation terminated.

  You can fix it by installing `python-dev` package. i.e

  ::

    sudo apt-get install python-dev

  If you see something similar to:

  ::

    third_party/protobuf/src/google/protobuf/stubs/mathlimits.h:173:31: note: in expansion of macro 'SIGNED_INT_MAX'
    static const Type kPosMax = SIGNED_INT_MAX(Type); \\
                               ^
  And your toolchain is GCC (at the time of this writing, up through at least
  GCC 6.0), this is probably a bug where GCC chokes on constant expressions
  when the :code:`-fwrapv` flag is specified. You should consider setting your
  environment with :code:`CFLAGS=-fno-wrapv` or using clang (:code:`CC=clang`).
