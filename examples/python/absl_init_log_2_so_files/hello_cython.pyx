# cython: language_level=3

cdef extern from "absl/log/initialize.h" namespace "absl":
    void InitializeLog() nogil

cdef extern from "hello.h":
    void hello_from_cpp(const char* name)

def init_logging():
    InitializeLog()

def say_hello(name: str):
    hello_from_cpp(name.encode('utf-8'))
