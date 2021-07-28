DEFINE_SYMBOL
-------------

Define a symbol when compiling this target's sources.

``DEFINE_SYMBOL`` sets the name of the preprocessor symbol defined when
compiling sources in a shared library.  If not set here then it is set
to ``target_EXPORTS`` by default (with some substitutions if the target is
not a valid C identifier).  This is useful for headers to know whether
they are being included from inside their library or outside to
properly setup dllexport/dllimport decorations.
