OpenSSL Native Nuget package
-------------------------

Uses [CoApp](http://coapp.org/) project to build the zlib package.

Prerequisites
-------------
Multiple versions of VS installed to be able to build all the targets:
* Visual Studio 2015
* Visual Studio 2013
* Visual Studio 2010 (you might need SP1 to prevent LNK1123 error)

ActivePerl

CoApp toolkit: http://downloads.coapp.org/files/CoApp.Tools.Powershell.msi

More details on installation: http://coapp.org/tutorials/installation.html

Building
--------

Build all flavors of openssl library using the provided batch file.
```
buildall.bat
```

Then, create NuGet package using powershell (you'll need the CoApp toolkit installed):
```
[THIS_DIRECTORY]> Write-NuGetPackage grpc.dependencies.openssl.autopkg
```

This will create three NuGet packages:
* the main dev package
* the redistributable package that contains just the binaries and no headers
* the symbols package (debug symbols)

Later, you can push the package to NuGet.org repo.
Attention: before pusing the resulting nuget package to public nuget repo, you have to be 100% sure it works correctly - there’s no way how to delete or update an already existing package.