gRPC Python
===========

|compat_check_pypi|

Package for gRPC Python.

.. |compat_check_pypi| image:: https://python-compatibility-tools.appspot.com/one_badge_image?package=grpcio
   :target: https://python-compatibility-tools.appspot.com/one_badge_target?package=grpcio

Supported Python Versions
-------------------------
Python >= 3.8

Installation
------------

gRPC Python is available for Linux, macOS, and Windows.

Installing From PyPI
~~~~~~~~~~~~~~~~~~~~

If you are installing locally...

::

  $ pip install grpcio

Else system wide (on Ubuntu)...

::

  $ sudo pip install grpcio

If you're on Windows make sure that you installed the :code:`pip.exe` component
when you installed Python (if not go back and install it!) then invoke:

::

  $ pip.exe install grpcio

Windows users may need to invoke :code:`pip.exe` from a command line ran as
administrator.

n.b. On Windows and on Mac OS X one *must* have a recent release of :code:`pip`
to retrieve the proper wheel from PyPI. Be sure to upgrade to the latest
version!

Installing From Source
~~~~~~~~~~~~~~~~~~~~~~

Building from source requires that you have the Python headers (usually a
package named :code:`python-dev`).

::

  $ export REPO_ROOT=grpc  # REPO_ROOT can be any directory of your choice
  $ git clone -b RELEASE_TAG_HERE https://github.com/grpc/grpc $REPO_ROOT
  $ cd $REPO_ROOT
  $ git submodule update --init

  # For the next two commands do `sudo pip install` if you get permission-denied errors
  $ pip install -r requirements.txt
  $ GRPC_PYTHON_BUILD_WITH_CYTHON=1 pip install .

You cannot currently install Python from source on Windows. Things might work
out for you in MSYS2 (follow the Linux instructions), but it isn't officially
supported at the moment.

Troubleshooting
~~~~~~~~~~~~~~~

Help, I ...

* **... see the following error on some platforms**

  ::

    /tmp/pip-build-U8pSsr/cython/Cython/Plex/Scanners.c:4:20: fatal error: Python.h: No such file or directory
    #include "Python.h"
                    ^
    compilation terminated.

  You can fix it by installing `python-dev` package. i.e

  ::

    sudo apt-get install python-dev

