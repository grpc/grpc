VS_DEBUGGER_COMMAND_ARGUMENTS
-----------------------------

.. versionadded:: 3.13

Sets the local debugger command line arguments for Visual Studio C++ targets.
The property value may use
:manual:`generator expressions <cmake-generator-expressions(7)>`.
This is defined in ``<LocalDebuggerCommandArguments>`` in the Visual Studio
project file.

This property only works for Visual Studio 2010 and above;
it is ignored on other generators.
