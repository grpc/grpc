cdef gpr_timespec _GPR_INF_FUTURE = gpr_inf_future(GPR_CLOCK_REALTIME)

cdef class BackgroundCompletionQueue:
    cdef grpc_completion_queue *_cq
    cdef bint _shutdown
    cdef object _shutdown_completed
    cdef object _poller
    cdef object _poller_running

    cdef _polling(self)
    cdef grpc_completion_queue* c_ptr(self)
