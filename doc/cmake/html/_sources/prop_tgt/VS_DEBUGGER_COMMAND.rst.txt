VS_DEBUGGER_COMMAND
-------------------

.. versionadded:: 3.12

Sets the local debugger command for Visual Studio C++ targets.
The property value may use
:manual:`generator expressions <cmake-generator-expressions(7)>`.
This is defined in ``<LocalDebuggerCommand>`` in the Visual Studio
project file.

This property only works for Visual Studio 2010 and above;
it is ignored on other generators.
