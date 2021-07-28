CMAKE_XCODE_ATTRIBUTE_<an-attribute>
------------------------------------

.. versionadded:: 3.1

Set Xcode target attributes directly.

Tell the :generator:`Xcode` generator to set '<an-attribute>' to a given value
in the generated Xcode project.  Ignored on other generators.

See the :prop_tgt:`XCODE_ATTRIBUTE_<an-attribute>` target property
to set attributes on a specific target.

Contents of ``CMAKE_XCODE_ATTRIBUTE_<an-attribute>`` may use
"generator expressions" with the syntax ``$<...>``.  See the
:manual:`cmake-generator-expressions(7)` manual for available
expressions.  See the :manual:`cmake-buildsystem(7)` manual
for more on defining buildsystem properties.
