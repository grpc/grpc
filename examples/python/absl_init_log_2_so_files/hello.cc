#include "hello.h"
#include <iostream>
// #include "absl/log/log.h"

void hello_from_cpp(const char* name) {
    std::cout << "C++ Layer: Hello, " << name << "!";
}
