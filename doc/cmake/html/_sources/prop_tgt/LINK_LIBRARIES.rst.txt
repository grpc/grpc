LINK_LIBRARIES
--------------

List of direct link dependencies.

This property specifies the list of libraries or targets which will be
used for linking.  In addition to accepting values from the
:command:`target_link_libraries` command, values may be set directly on
any target using the :command:`set_property` command.

The value of this property is used by the generators to set the link
libraries for the compiler.

Contents of ``LINK_LIBRARIES`` may use "generator expressions" with the
syntax ``$<...>``.  See the :manual:`cmake-generator-expressions(7)` manual
for available expressions.  See the :manual:`cmake-buildsystem(7)` manual
for more on defining buildsystem properties.

.. include:: LINK_LIBRARIES_INDIRECTION.txt
