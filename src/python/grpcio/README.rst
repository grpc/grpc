gRPC Python
===========

Package for gRPC Python.

Installation
------------

gRPC Python is available for Linux and Mac OS X running Python 2.7.

From PyPI
~~~~~~~~~

If you are installing locally...

::

  $ pip install grpcio

Else system wide (on Ubuntu)...

::

  $ sudo pip install grpcio

From Source
~~~~~~~~~~~

Building from source requires that you have the Python headers (usually a
package named `python-dev`).

::

  $ export REPO_ROOT=grpc
  $ git clone https://github.com/grpc/grpc.git $REPO_ROOT
  $ cd $REPO_ROOT
  $ pip install .

Note that `$REPO_ROOT` can be assigned to whatever directory name floats your
fancy.

Troubleshooting
~~~~~~~~~~~~~~~

Help, I ...

* **... see a** :code:`pkg_resources.VersionConflict` **when I try to install
  grpc!**

  This is likely because :code:`pip` doesn't own the offending dependency,
  which in turn is likely because your operating system's package manager owns
  it. You'll need to force the installation of the dependency:

  :code:`pip install --ignore-installed $OFFENDING_DEPENDENCY`
