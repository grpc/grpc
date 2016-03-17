gRPC Python
===========

Package for gRPC Python.

Installation
------------

gRPC Python is available for Linux, Mac OS X, and Windows running Python 2.7.

From PyPI
~~~~~~~~~

If you are installing locally...

::

  $ pip install grpcio

Else system wide (on Ubuntu)...

::

  $ sudo pip install grpcio

n.b. On Windows and on Mac OS X one *must* have a recent release of :code:`pip`
to retrieve the proper wheel from PyPI. Be sure to upgrade to the latest
version!

From Source
~~~~~~~~~~~

Building from source requires that you have the Python headers (usually a
package named :code:`python-dev`).

::

  $ export REPO_ROOT=grpc  # REPO_ROOT can be any directory of your choice
  $ git clone https://github.com/grpc/grpc.git $REPO_ROOT
  $ cd $REPO_ROOT

  # For the next two commands do `sudo pip install` if you get permission-denied errors
  $ pip install -rrequirements.txt
  $ GRPC_PYTHON_BUILD_WITH_CYTHON=1 pip install .


Troubleshooting
~~~~~~~~~~~~~~~

Help, I ...

* **... see a** :code:`pkg_resources.VersionConflict` **when I try to install
  grpc!**

  This is likely because :code:`pip` doesn't own the offending dependency,
  which in turn is likely because your operating system's package manager owns
  it. You'll need to force the installation of the dependency:

  :code:`pip install --ignore-installed $OFFENDING_DEPENDENCY`
