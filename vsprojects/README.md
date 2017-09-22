# Pre-generated MS Visual Studio project & solution files: DELETED

**The pre-generated MS Visual Studio project & solution files are no longer available, please use cmake instead (it can generate Visual Studio projects for you).**

**Pre-generated MS Visual Studio projects used to be the recommended way to build on Windows, but there were some limitations:**
- **hard to build dependencies, expecially boringssl (deps usually support cmake quite well)**
- **the nuget-based openssl & zlib dependencies are hard to maintain and update. We've received issues indicating that they are flawed.**
- **.proto codegen is hard to support in Visual Studio directly (but we have a pretty decent support in cmake)**
- **It's a LOT of generated files. We prefer not to have too much generated code in our github repo.**

See [INSTALL.md](/INSTALL.md) for detailed instructions how to build using cmake on Windows.
