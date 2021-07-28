DESTDIR
-------

.. include:: ENV_VAR.txt

On UNIX one can use the ``DESTDIR`` mechanism in order to relocate the
whole installation.  ``DESTDIR`` means DESTination DIRectory.  It is
commonly used by makefile users in order to install software at
non-default location.  It is usually invoked like this:

::

 make DESTDIR=/home/john install

which will install the concerned software using the installation
prefix, e.g.  ``/usr/local`` prepended with the ``DESTDIR`` value which
finally gives ``/home/john/usr/local``.

WARNING: ``DESTDIR`` may not be used on Windows because installation
prefix usually contains a drive letter like in ``C:/Program Files``
which cannot be prepended with some other prefix.
