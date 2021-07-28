VS_DEBUGGER_ENVIRONMENT
-----------------------

.. versionadded:: 3.13

Sets the local debugger environment for Visual Studio C++ targets.
The property value may use
:manual:`generator expressions <cmake-generator-expressions(7)>`.
This is defined in ``<LocalDebuggerEnvironment>`` in the Visual Studio
project file.

This property only works for Visual Studio 2010 and above;
it is ignored on other generators.
