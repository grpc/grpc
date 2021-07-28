LANGUAGE
--------

What programming language is the file.

A property that can be set to indicate what programming language the
source file is.  If it is not set the language is determined based on
the file extension.  Typical values are ``CXX`` (i.e.  C++), ``C``,
``CSharp``, ``CUDA``, ``Fortran``, ``ISPC``, and ``ASM``.  Setting this
property for a file means this file will be compiled.  Do not set this
for headers or files that should not be compiled.
