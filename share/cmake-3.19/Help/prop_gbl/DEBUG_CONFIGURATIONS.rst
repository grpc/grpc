DEBUG_CONFIGURATIONS
--------------------

Specify which configurations are for debugging.

The value must be a semi-colon separated list of configuration names.
Currently this property is used only by the :command:`target_link_libraries`
command.  Additional uses may be defined in the future.

This property must be set at the top level of the project and before
the first :command:`target_link_libraries` command invocation.  If any entry in
the list does not match a valid configuration for the project the
behavior is undefined.
