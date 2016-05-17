/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

/*
 * wakeup_fd abstracts the concept of a file descriptor for the purpose of
 * waking up a thread in select()/poll()/epoll_wait()/etc.

 * The poll() family of system calls provide a way for a thread to block until
 * there is activity on one (or more) of a set of file descriptors. An
 * application may wish to wake up this thread to do non file related work. The
 * typical way to do this is to add a pipe to the set of file descriptors, then
 * write to the pipe to wake up the thread in poll().
 *
 * Linux has a lighter weight eventfd specifically designed for this purpose.
 * wakeup_fd abstracts the difference between the two.
 *
 * Setup:
 * 1. Before calling anything, call global_init() at least once.
 * 1. Call grpc_wakeup_fd_create() to get a wakeup_fd.
 * 2. Add the result of GRPC_WAKEUP_FD_FD to the set of monitored file
 *    descriptors for the poll() style API you are using. Monitor the file
 *    descriptor for readability.
 * 3. To tear down, call grpc_wakeup_fd_destroy(). This closes the underlying
 *    file descriptor.
 *
 * Usage:
 * 1. To wake up a polling thread, call grpc_wakeup_fd_wakeup() on a wakeup_fd
 *    it is monitoring.
 * 2. If the polling thread was awakened by a wakeup_fd event, call
 *    grpc_wakeup_fd_consume_wakeup() on it.
 */
#ifndef GRPC_CORE_LIB_IOMGR_WAKEUP_FD_POSIX_H
#define GRPC_CORE_LIB_IOMGR_WAKEUP_FD_POSIX_H

void grpc_wakeup_fd_global_init(void);
void grpc_wakeup_fd_global_destroy(void);

/* Force using the fallback implementation. This is intended for testing
 * purposes only.*/
void grpc_wakeup_fd_global_init_force_fallback(void);

typedef struct grpc_wakeup_fd grpc_wakeup_fd;

typedef struct grpc_wakeup_fd_vtable {
  void (*init)(grpc_wakeup_fd* fd_info);
  void (*consume)(grpc_wakeup_fd* fd_info);
  void (*wakeup)(grpc_wakeup_fd* fd_info);
  void (*destroy)(grpc_wakeup_fd* fd_info);
  /* Must be called before calling any other functions */
  int (*check_availability)(void);
} grpc_wakeup_fd_vtable;

struct grpc_wakeup_fd {
  int read_fd;
  int write_fd;
};

extern int grpc_allow_specialized_wakeup_fd;

#define GRPC_WAKEUP_FD_GET_READ_FD(fd_info) ((fd_info)->read_fd)

void grpc_wakeup_fd_init(grpc_wakeup_fd* fd_info);
void grpc_wakeup_fd_consume_wakeup(grpc_wakeup_fd* fd_info);
void grpc_wakeup_fd_wakeup(grpc_wakeup_fd* fd_info);
void grpc_wakeup_fd_destroy(grpc_wakeup_fd* fd_info);

/* Defined in some specialized implementation's .c file, or by
 * wakeup_fd_nospecial.c if no such implementation exists. */
extern const grpc_wakeup_fd_vtable grpc_specialized_wakeup_fd_vtable;

#endif /* GRPC_CORE_LIB_IOMGR_WAKEUP_FD_POSIX_H */
