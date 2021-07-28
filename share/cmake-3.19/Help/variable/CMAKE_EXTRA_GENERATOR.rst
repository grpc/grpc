CMAKE_EXTRA_GENERATOR
---------------------

The extra generator used to build the project.  See
:manual:`cmake-generators(7)`.

When using the Eclipse, CodeBlocks, CodeLite, Kate or Sublime generators, CMake
generates Makefiles (:variable:`CMAKE_GENERATOR`) and additionally project
files for the respective IDE.  This IDE project file generator is stored in
``CMAKE_EXTRA_GENERATOR`` (e.g.  ``Eclipse CDT4``).
