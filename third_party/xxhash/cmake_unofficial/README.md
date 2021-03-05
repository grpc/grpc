
## Usage

### Way 1: import targets
Build xxHash targets:

    cd </path/to/xxHash/>
    mkdir build
    cd build
    cmake ../cmake_unofficial [options]
    cmake --build .
    cmake --build . --target install #optional

Where possible options are:
- `-DXXHASH_BUILD_ENABLE_INLINE_API=<ON|OFF>`: adds xxhash.c for the `-DXXH_INLINE_ALL` api. ON by default.
- `-DXXHASH_BUILD_XXHSUM=<ON|OFF>`: build the command line binary. ON by default
- `-DBUILD_SHARED_LIBS=<ON|OFF>`: build dynamic library. ON by default.
- `-DCMAKE_INSTALL_PREFIX=<path>`: use custom install prefix path.

Add lines into downstream CMakeLists.txt:

    find_package(xxHash 0.7 CONFIG REQUIRED)
    ...
    target_link_libraries(MyTarget PRIVATE xxHash::xxhash)

### Way 2: Add subdirectory
Add lines into downstream CMakeLists.txt:

    option(BUILD_SHARED_LIBS "Build shared libs" OFF) #optional
    ...
    set(XXHASH_BUILD_ENABLE_INLINE_API OFF) #optional
    set(XXHASH_BUILD_XXHSUM OFF) #optional
    add_subdirectory(</path/to/xxHash/cmake_unofficial/> </path/to/xxHash/build/> EXCLUDE_FROM_ALL)
    ...
    target_link_libraries(MyTarget PRIVATE xxHash::xxhash)

