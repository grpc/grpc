cdef gpr_timespec _GPR_INF_FUTURE = gpr_inf_future(GPR_CLOCK_REALTIME)

cdef class BaseCompletionQueue:

    cdef grpc_completion_queue* c_ptr(self)

cdef class PollerCompletionQueue(BaseCompletionQueue):
    cdef grpc_completion_queue *_cq
    cdef bint _shutdown
    cdef object _shutdown_completed
    cdef object _poller
    cdef object _poller_running

    cdef _polling(self)


cdef class CallbackCompletionQueue(BaseCompletionQueue):
    cdef grpc_completion_queue *_cq
    cdef object _shutdown_completed  # asyncio.Future
    cdef CallbackWrapper _wrapper
