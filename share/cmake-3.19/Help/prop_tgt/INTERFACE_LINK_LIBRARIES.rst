INTERFACE_LINK_LIBRARIES
------------------------

List public interface libraries for a library.

This property contains the list of transitive link dependencies.  When
the target is linked into another target using the
:command:`target_link_libraries` command, the libraries listed (and
recursively their link interface libraries) will be provided to the
other target also.  This property is overridden by the
:prop_tgt:`LINK_INTERFACE_LIBRARIES` or
:prop_tgt:`LINK_INTERFACE_LIBRARIES_<CONFIG>` property if policy
:policy:`CMP0022` is ``OLD`` or unset.

Contents of ``INTERFACE_LINK_LIBRARIES`` may use "generator expressions"
with the syntax ``$<...>``.  See the :manual:`cmake-generator-expressions(7)`
manual for available expressions.  See the :manual:`cmake-buildsystem(7)`
manual for more on defining buildsystem properties.

.. include:: LINK_LIBRARIES_INDIRECTION.txt

Creating Relocatable Packages
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. |INTERFACE_PROPERTY_LINK| replace:: ``INTERFACE_LINK_LIBRARIES``
.. include:: /include/INTERFACE_LINK_LIBRARIES_WARNING.txt
