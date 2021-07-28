LINK_WHAT_YOU_USE
---------------------------

.. versionadded:: 3.7

This is a boolean option that when set to ``TRUE`` will automatically run
``ldd -r -u`` on the target after it is linked. In addition, the linker flag
``-Wl,--no-as-needed`` will be passed to the target with the link command so
that all libraries specified on the command line will be linked into the
target. This will result in the link producing a list of libraries that
provide no symbols used by this target but are being linked to it.
This is only applicable to executable and shared library targets and
will only work when ld and ldd accept the flags used.

This property is initialized by the value of
the :variable:`CMAKE_LINK_WHAT_YOU_USE` variable if it is set
when a target is created.
