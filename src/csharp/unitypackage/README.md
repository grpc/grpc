# Scripts for building gRPC C# package for Unity

Scripts in this directory are of no interest for end users. They are part
of internal tooling to automate building of the gRPC C# package for Unity.

- `unitypackage_skeleton` - preconfigured `.meta` files for the unity package
  layout. The actual assemblies and native libraries will be added into
  this hierarchy while building the package.
  Note: The `.meta` file were created by the Unity IDE by manually adding the assemblies/native libraries
  to a Unity project and configuring their target plaform/architecture in the UI (these setting get recorded in
  `.meta` files). The `.meta` format is not very well documented and there seems to be no easy way to generate them
  automatically.
  