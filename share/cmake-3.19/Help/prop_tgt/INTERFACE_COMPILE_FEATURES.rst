INTERFACE_COMPILE_FEATURES
--------------------------

.. versionadded:: 3.1

.. |property_name| replace:: compile features
.. |command_name| replace:: :command:`target_compile_features`
.. |PROPERTY_INTERFACE_NAME| replace:: ``INTERFACE_COMPILE_FEATURES``
.. |PROPERTY_LINK| replace:: :prop_tgt:`COMPILE_FEATURES`
.. |PROPERTY_GENEX| replace:: ``$<TARGET_PROPERTY:foo,INTERFACE_COMPILE_FEATURES>``
.. include:: INTERFACE_BUILD_PROPERTY.txt

See the :manual:`cmake-compile-features(7)` manual for information on compile
features and a list of supported compilers.
