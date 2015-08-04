/* FIXME: "posix" files shouldn't be depending on _GNU_SOURCE */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <grpc/support/port_platform.h>

#ifdef GPR_POSIX_SOCKET

#include "src/core/iomgr/udp_server.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include "src/core/iomgr/fd_posix.h"
#include "src/core/iomgr/pollset_posix.h"
#include "src/core/iomgr/resolve_address.h"
#include "src/core/iomgr/sockaddr_utils.h"
#include "src/core/iomgr/socket_utils_posix.h"
#include "src/core/support/string.h"
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/string_util.h>
#include <grpc/support/time.h>

#define INIT_PORT_CAP 2

/* one listening port */
typedef struct {
  int fd;
  grpc_fd *emfd;
  grpc_udp_server *server;
  union {
    gpr_uint8 untyped[GRPC_MAX_SOCKADDR_SIZE];
    struct sockaddr sockaddr;
    struct sockaddr_un un;
  } addr;
  int addr_len;
  grpc_iomgr_closure read_closure;
  grpc_iomgr_closure destroyed_closure;
  grpc_udp_server_read_cb read_cb;
} server_port;

static void unlink_if_unix_domain_socket(const struct sockaddr_un *un) {
  struct stat st;

  if (stat(un->sun_path, &st) == 0 && (st.st_mode & S_IFMT) == S_IFSOCK) {
    unlink(un->sun_path);
  }
}

/* the overall server */
struct grpc_udp_server {
  grpc_udp_server_cb cb;
  void *cb_arg;

  gpr_mu mu;
  gpr_cv cv;

  /* active port count: how many ports are actually still listening */
  size_t active_ports;
  /* destroyed port count: how many ports are completely destroyed */
  size_t destroyed_ports;

  /* is this server shutting down? (boolean) */
  int shutdown;

  /* all listening ports */
  server_port *ports;
  size_t nports;
  size_t port_capacity;

  /* shutdown callback */
  void (*shutdown_complete)(void *);
  void *shutdown_complete_arg;

  /* all pollsets interested in new connections */
  grpc_pollset **pollsets;
  /* number of pollsets in the pollsets array */
  size_t pollset_count;
};

grpc_udp_server *grpc_udp_server_create(void) {
  grpc_udp_server *s = gpr_malloc(sizeof(grpc_udp_server));
  gpr_mu_init(&s->mu);
  gpr_cv_init(&s->cv);
  s->active_ports = 0;
  s->destroyed_ports = 0;
  s->shutdown = 0;
  s->cb = NULL;
  s->cb_arg = NULL;
  s->ports = gpr_malloc(sizeof(server_port) * INIT_PORT_CAP);
  s->nports = 0;
  s->port_capacity = INIT_PORT_CAP;

  fprintf(stderr, "grpc_udp_server_create Created UDP server\n");
  return s;
}

static void finish_shutdown(grpc_udp_server *s) {
  s->shutdown_complete(s->shutdown_complete_arg);

  gpr_mu_destroy(&s->mu);
  gpr_cv_destroy(&s->cv);

  gpr_free(s->ports);
  gpr_free(s);
}

static void destroyed_port(void *server, int success) {
  grpc_udp_server *s = server;
  gpr_mu_lock(&s->mu);
  s->destroyed_ports++;
  if (s->destroyed_ports == s->nports) {
    gpr_mu_unlock(&s->mu);
    finish_shutdown(s);
  } else {
    gpr_mu_unlock(&s->mu);
  }
}

static void dont_care_about_shutdown_completion(void *ignored) {}

/* called when all listening endpoints have been shutdown, so no further
   events will be received on them - at this point it's safe to destroy
   things */
static void deactivated_all_ports(grpc_udp_server *s) {
  size_t i;

  /* delete ALL the things */
  gpr_mu_lock(&s->mu);

  if (!s->shutdown) {
    gpr_mu_unlock(&s->mu);
    return;
  }

  if (s->nports) {
    for (i = 0; i < s->nports; i++) {
      server_port *sp = &s->ports[i];
      if (sp->addr.sockaddr.sa_family == AF_UNIX) {
        unlink_if_unix_domain_socket(&sp->addr.un);
      }
      sp->destroyed_closure.cb = destroyed_port;
      sp->destroyed_closure.cb_arg = s;
      grpc_fd_orphan(sp->emfd, &sp->destroyed_closure, "udp_listener_shutdown");
    }
    gpr_mu_unlock(&s->mu);
  } else {
    gpr_mu_unlock(&s->mu);
    finish_shutdown(s);
  }
}

void grpc_udp_server_destroy(
    grpc_udp_server *s, void (*shutdown_complete)(void *shutdown_complete_arg),
    void *shutdown_complete_arg) {
  size_t i;
  gpr_mu_lock(&s->mu);

  GPR_ASSERT(!s->shutdown);
  s->shutdown = 1;

  s->shutdown_complete = shutdown_complete
                             ? shutdown_complete
                             : dont_care_about_shutdown_completion;
  s->shutdown_complete_arg = shutdown_complete_arg;

  /* shutdown all fd's */
  if (s->active_ports) {
    for (i = 0; i < s->nports; i++) {
      grpc_fd_shutdown(s->ports[i].emfd);
    }
    gpr_mu_unlock(&s->mu);
  } else {
    gpr_mu_unlock(&s->mu);
    deactivated_all_ports(s);
  }
}

/* Prepare a recently-created socket for listening. */
static int prepare_socket(int fd, const struct sockaddr *addr, int addr_len) {
  struct sockaddr_storage sockname_temp;
  socklen_t sockname_len;
  int get_local_ip;
  int rc;

  if (fd < 0) {
    goto error;
  }

  get_local_ip = 1;
  rc = setsockopt(fd, IPPROTO_IP, IP_PKTINFO,
                      &get_local_ip, sizeof(get_local_ip));
  if (rc == 0 && addr->sa_family == AF_INET6) {
    rc = setsockopt(fd, IPPROTO_IPV6, IPV6_RECVPKTINFO,
                    &get_local_ip, sizeof(get_local_ip));
  }

  if (bind(fd, addr, addr_len) < 0) {
    char *addr_str;
    grpc_sockaddr_to_string(&addr_str, addr, 0);
    gpr_log(GPR_ERROR, "bind addr=%s: %s", addr_str, strerror(errno));
    gpr_free(addr_str);
    goto error;
  }

  sockname_len = sizeof(sockname_temp);
  if (getsockname(fd, (struct sockaddr *)&sockname_temp, &sockname_len) < 0) {
    goto error;
  }

  return grpc_sockaddr_get_port((struct sockaddr *)&sockname_temp);

error:
  if (fd >= 0) {
    close(fd);
  }
  return -1;
}

/* event manager callback when reads are ready */
static void on_read(void *arg, int success) {
  server_port *sp = arg;

  if (success == 0) {
    gpr_mu_lock(&sp->server->mu);
    if (0 == --sp->server->active_ports) {
      gpr_mu_unlock(&sp->server->mu);
      deactivated_all_ports(sp->server);
    } else {
      gpr_mu_unlock(&sp->server->mu);
    }
    return;
  }

  /* Tell the registered callback that data is available to read. */
  GPR_ASSERT(sp->read_cb);
  sp->read_cb(sp->fd, sp->server->cb, sp->server->cb_arg);

  /* Re-arm the notification event so we get another chance to read. */
  grpc_fd_notify_on_read(sp->emfd, &sp->read_closure);
}

static int add_socket_to_server(grpc_udp_server *s, int fd,
                                const struct sockaddr *addr, int addr_len,
                                grpc_udp_server_read_cb read_cb) {
  server_port *sp;
  int port;
  char *addr_str;
  char *name;

  port = prepare_socket(fd, addr, addr_len);
  if (port >= 0) {
    grpc_sockaddr_to_string(&addr_str, (struct sockaddr *)&addr, 1);
    gpr_asprintf(&name, "udp-server-listener:%s", addr_str);
    gpr_mu_lock(&s->mu);
    GPR_ASSERT(!s->cb && "must add ports before starting server");
    /* append it to the list under a lock */
    if (s->nports == s->port_capacity) {
      s->port_capacity *= 2;
      s->ports = gpr_realloc(s->ports, sizeof(server_port) * s->port_capacity);
    }
    sp = &s->ports[s->nports++];
    sp->server = s;
    sp->fd = fd;
    sp->emfd = grpc_fd_create(fd, name);
    memcpy(sp->addr.untyped, addr, addr_len);
    sp->addr_len = addr_len;
    sp->read_cb = read_cb;
    GPR_ASSERT(sp->emfd);
    gpr_mu_unlock(&s->mu);
  }

  return port;
}

int grpc_udp_server_add_port(grpc_udp_server *s, const void *addr,
                             int addr_len, grpc_udp_server_read_cb read_cb) {
  int allocated_port1 = -1;
  int allocated_port2 = -1;
  unsigned i;
  int fd;
  grpc_dualstack_mode dsmode;
  struct sockaddr_in6 addr6_v4mapped;
  struct sockaddr_in wild4;
  struct sockaddr_in6 wild6;
  struct sockaddr_in addr4_copy;
  struct sockaddr *allocated_addr = NULL;
  struct sockaddr_storage sockname_temp;
  socklen_t sockname_len;
  int port;

  if (((struct sockaddr *)addr)->sa_family == AF_UNIX) {
    unlink_if_unix_domain_socket(addr);
  }

  /* Check if this is a wildcard port, and if so, try to keep the port the same
     as some previously created listener. */
  if (grpc_sockaddr_get_port(addr) == 0) {
    for (i = 0; i < s->nports; i++) {
      sockname_len = sizeof(sockname_temp);
      if (0 == getsockname(s->ports[i].fd, (struct sockaddr *)&sockname_temp,
                           &sockname_len)) {
        port = grpc_sockaddr_get_port((struct sockaddr *)&sockname_temp);
        if (port > 0) {
          allocated_addr = malloc(addr_len);
          memcpy(allocated_addr, addr, addr_len);
          grpc_sockaddr_set_port(allocated_addr, port);
          addr = allocated_addr;
          break;
        }
      }
    }
  }

  if (grpc_sockaddr_to_v4mapped(addr, &addr6_v4mapped)) {
    addr = (const struct sockaddr *)&addr6_v4mapped;
    addr_len = sizeof(addr6_v4mapped);
  }

  /* Treat :: or 0.0.0.0 as a family-agnostic wildcard. */
  if (grpc_sockaddr_is_wildcard(addr, &port)) {
    grpc_sockaddr_make_wildcards(port, &wild4, &wild6);

    /* Try listening on IPv6 first. */
    addr = (struct sockaddr *)&wild6;
    addr_len = sizeof(wild6);
    fd = grpc_create_dualstack_socket(addr, SOCK_DGRAM, IPPROTO_UDP, &dsmode);
    allocated_port1 = add_socket_to_server(s, fd, addr, addr_len, read_cb);
    if (fd >= 0 && dsmode == GRPC_DSMODE_DUALSTACK) {
      goto done;
    }

    /* If we didn't get a dualstack socket, also listen on 0.0.0.0. */
    if (port == 0 && allocated_port1 > 0) {
      grpc_sockaddr_set_port((struct sockaddr *)&wild4, allocated_port1);
    }
    addr = (struct sockaddr *)&wild4;
    addr_len = sizeof(wild4);
  }

  fd = grpc_create_dualstack_socket(addr, SOCK_DGRAM, IPPROTO_UDP, &dsmode);
  if (fd < 0) {
    gpr_log(GPR_ERROR, "Unable to create socket: %s", strerror(errno));
  }
  if (dsmode == GRPC_DSMODE_IPV4 &&
      grpc_sockaddr_is_v4mapped(addr, &addr4_copy)) {
    addr = (struct sockaddr *)&addr4_copy;
    addr_len = sizeof(addr4_copy);
  }
  allocated_port2 = add_socket_to_server(s, fd, addr, addr_len, read_cb);

done:
  fprintf(stderr,
          "grpc_udp_server_add_port created FD: %d, listening port: %d\n", fd,
          allocated_port1);
  gpr_free(allocated_addr);
  return allocated_port1 >= 0 ? allocated_port1 : allocated_port2;
}

int grpc_udp_server_get_fd(grpc_udp_server *s, unsigned index) {
  return (index < s->nports) ? s->ports[index].fd : -1;
}

void grpc_udp_server_start(grpc_udp_server *s, grpc_pollset **pollsets,
                           size_t pollset_count,
                           grpc_udp_server_cb new_transport_cb, void *cb_arg) {
  size_t i, j;
  GPR_ASSERT(new_transport_cb);
  gpr_mu_lock(&s->mu);
  GPR_ASSERT(!s->cb);
  GPR_ASSERT(s->active_ports == 0);
  s->cb = new_transport_cb;
  s->cb_arg = cb_arg;
  s->pollsets = pollsets;
  for (i = 0; i < s->nports; i++) {
    for (j = 0; j < pollset_count; j++) {
      grpc_pollset_add_fd(pollsets[j], s->ports[i].emfd);
    }
    s->ports[i].read_closure.cb = on_read;
    s->ports[i].read_closure.cb_arg = &s->ports[i];
    grpc_fd_notify_on_read(s->ports[i].emfd, &s->ports[i].read_closure);
    s->active_ports++;
  }
  gpr_mu_unlock(&s->mu);

  fprintf(stderr, "grpc_udp_server_start Started UDP server\n");
}

/* TODO(rjshade): Add a test for this method. */
void grpc_udp_server_write(server_port *sp, const char *buffer, size_t buf_len,
                           const struct sockaddr *peer_address) {
  int rc;
  rc = sendto(sp->fd, buffer, buf_len, 0, peer_address, sizeof(peer_address));
  if (rc < 0) {
    gpr_log(GPR_ERROR, "Unable to send data: %s", strerror(errno));
  }
}

#endif
