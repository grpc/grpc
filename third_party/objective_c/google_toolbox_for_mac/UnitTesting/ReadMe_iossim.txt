The iossim binary here is a build of a tool found in the Chromium project:

  http://src.chromium.org/viewvc/chrome/trunk/src/testing/iossim/iossim.mm?view=markup

It is used to run the unittests for iOS. The old way that directly ran the
executables with some environment variables set no longer works, so this
provides a way that more closely imitates how Xcode itself invokes the
Simulator.
