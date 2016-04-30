# How to contribute

This is a place for various components in the gRPC ecosystem that aren't part of the gRPC core. We welcome contributions in this repo which either build extensions around gRPC or showcase how to use gRPC in various use cases and/or with other technologies.
Here is some guideline and information about how to do so.


## Getting started

### Legal requirements

In order to protect both you and ourselves, you will need to sign the
[Contributor License Agreement](https://cla.developers.google.com/clas).

### Guidelines to contribute

Each contribution needs to have a) top level readme explaining what the contribution does, how to use it with gRPC, how to build and test it and what are its external technical dependencies.
Have at least a top level readme.md describing overview, how to use, dependencies, and how to build and test.
Third party libraries: Note that no third party libraries with AGPL license etc should not be used in the codebases.
Automated tests - will have a badge called “Verified” for tested contributions. Contributors should have automated tests present in every contribution and they should run on commit. We (gRPC team) will set up travis CI to facilitate this. Tests must return green before we merge them.

### How contributions will be accepted?

gRPC core team members will accept PRs and merge. Code reviews will be done on a best effort basis. It is however expected that the community will address the comments from core team members. As long as contribution meets the two above mentioned guidelines and CLA is signed, PRs will be merged. The team will try and take care of outstanding requests weekly (ie during office hours). If people want a faster dev cycle, we'd recommend doing this in a fork, per github flow anyways. 

