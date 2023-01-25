from libcpp cimport bool


cdef extern from "absl/debugging/failure_signal_handler.h" namespace "absl":
    ctypedef struct FailureSignalHandlerOptions "::absl::FailureSignalHandlerOptions":
        bool symbolize_stacktrace
        bool use_alternate_stack
        int alarm_on_failure_secs
        bool call_previous_handler
        void (*writerfn)(const char*)

    void InstallFailureSignalHandler(const FailureSignalHandlerOptions& options)

def install_failure_signal_handler(**kwargs):
    cdef FailureSignalHandlerOptions opts = FailureSignalHandlerOptions(
        True, False, -1, False, NULL
    )

    InstallFailureSignalHandler(opts)
