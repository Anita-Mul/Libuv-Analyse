/* Copyright Joyent, Inc. and other Node contributors. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "uv.h"
#include "internal.h"

#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>

/*
  1 获取一个新的 socket fd
  2 把 fd 保存到 handle 里，并根据 flag 进行相关设置
  3 绑定到本机随意的地址（如果设置了该标记的话）
*/
static int new_socket(uv_tcp_t* handle, int domain, unsigned long flags) {
  struct sockaddr_storage saddr;
  socklen_t slen;
  int sockfd;
  int err;

  // 获取一个 socket
  err = uv__socket(domain, SOCK_STREAM, 0);
  if (err < 0)
    return err;
  // 申请的 fd
  sockfd = err;

  // 设置选项和保存 socket 的文件描述符到 io 观察者中
  err = uv__stream_open((uv_stream_t*) handle, sockfd, flags);
  if (err) {
    uv__close(sockfd);
    return err;
  }

  // 设置了需要绑定标记 UV_HANDLE_BOUND 
  if (flags & UV_HANDLE_BOUND) {
    /* Bind this new socket to an arbitrary port */
    slen = sizeof(saddr);
    memset(&saddr, 0, sizeof(saddr));
    // 获取 fd 对应的 socket 信息，比如 ip，端口，可能没有
    if (getsockname(uv__stream_fd(handle), (struct sockaddr*) &saddr, &slen)) {
      uv__close(sockfd);
      return UV__ERR(errno);
    }

    // 绑定到 socket 中，如果没有则绑定到系统随机选择的地址
    if (bind(uv__stream_fd(handle), (struct sockaddr*) &saddr, slen)) {
      uv__close(sockfd);
      return UV__ERR(errno);
    }
  }

  return 0;
}

// 如果流还没有对应的 fd，则申请一个新的，如果有则修改流的配置
static int maybe_new_socket(uv_tcp_t* handle, int domain, unsigned long flags) {
  struct sockaddr_storage saddr;
  socklen_t slen;

  if (domain == AF_UNSPEC) {
    handle->flags |= flags;
    return 0;
  }

  // 已经有 socket fd 了
  if (uv__stream_fd(handle) != -1) {
    // 该流需要绑定到一个地址
    if (flags & UV_HANDLE_BOUND) {
      /*
        流是否已经绑定到一个地址了。handle 的 flag 是在 new_socket 里设置的，
        如果有这个标记说明已经执行过绑定了，直接更新 flags 就行。
      */
      if (handle->flags & UV_HANDLE_BOUND) {
        /* It is already bound to a port. */
        handle->flags |= flags;
        return 0;
      }

      // 有 socket fd，但是可能还没绑定到一个地址
      slen = sizeof(saddr);
      memset(&saddr, 0, sizeof(saddr));
      // 获取 socket 绑定到的地址
      if (getsockname(uv__stream_fd(handle), (struct sockaddr*) &saddr, &slen))
        return UV__ERR(errno);

      // 绑定过了 socket 地址，则更新 flags 就行
      if ((saddr.ss_family == AF_INET6 &&
          ((struct sockaddr_in6*) &saddr)->sin6_port != 0) ||
          (saddr.ss_family == AF_INET &&
          ((struct sockaddr_in*) &saddr)->sin_port != 0)) {
        /* Handle is already bound to a port. */
        handle->flags |= flags;
        return 0;
      }

      // 没绑定则绑定到随机地址，bind 中实现
      if (bind(uv__stream_fd(handle), (struct sockaddr*) &saddr, slen))
        return UV__ERR(errno);
    }

    handle->flags |= flags;
    return 0;
  }

  // 申请一个新的 fd 关联到流
  return new_socket(handle, domain, flags);
}


int uv_tcp_init_ex(uv_loop_t* loop, uv_tcp_t* tcp, unsigned int flags) {
  int domain;

  /* Use the lower 8 bits for the domain */
  domain = flags & 0xFF;
  if (domain != AF_INET && domain != AF_INET6 && domain != AF_UNSPEC)
    return UV_EINVAL;

  if (flags & ~0xFF)
    return UV_EINVAL;

  // 初始化流部分
  uv__stream_init(loop, (uv_stream_t*)tcp, UV_TCP);

  /* If anything fails beyond this point we need to remove the handle from
   * the handle queue, since it was added by uv__handle_init in uv_stream_init.
   */

  if (domain != AF_UNSPEC) {
    int err = maybe_new_socket(tcp, domain, 0);
    if (err) {
      QUEUE_REMOVE(&tcp->handle_queue);
      return err;
    }
  }

  return 0;
}

// 初始化 uv_tcp_t 结构体
int uv_tcp_init(uv_loop_t* loop, uv_tcp_t* tcp) {
  return uv_tcp_init_ex(loop, tcp, AF_UNSPEC);
}


int uv__tcp_bind(uv_tcp_t* tcp,
                 const struct sockaddr* addr,
                 unsigned int addrlen,
                 unsigned int flags) {
  int err;
  int on;

  /* Cannot set IPv6-only mode on non-IPv6 socket. */
  if ((flags & UV_TCP_IPV6ONLY) && addr->sa_family != AF_INET6)
    return UV_EINVAL;

  // 设置流可读写
  err = maybe_new_socket(tcp, addr->sa_family, 0);
  if (err)
    return err;

  // 设置 SO_REUSEADDR 属性
  on = 1;
  if (setsockopt(tcp->io_watcher.fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)))
    return UV__ERR(errno);

#ifndef __OpenBSD__
#ifdef IPV6_V6ONLY
  if (addr->sa_family == AF_INET6) {
    on = (flags & UV_TCP_IPV6ONLY) != 0;
    if (setsockopt(tcp->io_watcher.fd,
                   IPPROTO_IPV6,
                   IPV6_V6ONLY,
                   &on,
                   sizeof on) == -1) {
#if defined(__MVS__)
      if (errno == EOPNOTSUPP)
        return UV_EINVAL;
#endif
      return UV__ERR(errno);
    }
  }
#endif
#endif

  errno = 0;
  // 绑定地址到 socket
  if (bind(tcp->io_watcher.fd, addr, addrlen) && errno != EADDRINUSE) {
    if (errno == EAFNOSUPPORT)
      /* OSX, other BSDs and SunoS fail with EAFNOSUPPORT when binding a
       * socket created with AF_INET to an AF_INET6 address or vice versa. */
      return UV_EINVAL;
    return UV__ERR(errno);
  }
  // 设置已经绑定标记
  tcp->delayed_error = UV__ERR(errno);

  tcp->flags |= UV_HANDLE_BOUND;
  if (addr->sa_family == AF_INET6)
    tcp->flags |= UV_HANDLE_IPV6;

  return 0;
}


int uv__tcp_connect(uv_connect_t* req,
                    uv_tcp_t* handle,
                    const struct sockaddr* addr,
                    unsigned int addrlen,
                    uv_connect_cb cb) {
  int err;
  int r;

  assert(handle->type == UV_TCP);

  // 已经发起了 connect 了
  if (handle->connect_req != NULL)
    return UV_EALREADY;  /* FIXME(bnoordhuis) UV_EINVAL or maybe UV_EBUSY. */

  if (handle->delayed_error != 0)
    goto out;

  // 申请一个 socket 和绑定一个地址，如果还没有的话
  err = maybe_new_socket(handle,
                         addr->sa_family,
                         UV_HANDLE_READABLE | UV_HANDLE_WRITABLE);
  if (err)
    return err;

  do {
    // 清除全局错误变量的值
    errno = 0;
    // 发起三次握手
    r = connect(uv__stream_fd(handle), addr, addrlen);
  } while (r == -1 && errno == EINTR);

  // 三次握手还没有完成
  if (r == -1 && errno != 0) {
    if (errno == EINPROGRESS)
      ; /* not an error */
    else if (errno == ECONNREFUSED
      #if defined(__OpenBSD__)
            || errno == EINVAL
      #endif
    )
      // 对方拒绝建立连接，延迟报错
      handle->delayed_error = UV__ERR(ECONNREFUSED);
    else
      // 直接报错
      return UV__ERR(errno);
  }

out:
  // 初始化一个连接型 request，并设置某些字段
  uv__req_init(handle->loop, req, UV_CONNECT);
  req->cb = cb;
  req->handle = (uv_stream_t*) handle;
  QUEUE_INIT(&req->queue);
  handle->connect_req = req;

  // 注册到 libuv 观察者队列
  uv__io_start(handle->loop, &handle->io_watcher, POLLOUT);

  // 连接出错，插入 pending 队尾
  if (handle->delayed_error)
    uv__io_feed(handle->loop, &handle->io_watcher);

  return 0;
}


int uv_tcp_open(uv_tcp_t* handle, uv_os_sock_t sock) {
  int err;

  if (uv__fd_exists(handle->loop, sock))
    return UV_EEXIST;

  err = uv__nonblock(sock, 1);
  if (err)
    return err;

  return uv__stream_open((uv_stream_t*)handle,
                         sock,
                         UV_HANDLE_READABLE | UV_HANDLE_WRITABLE);
}


int uv_tcp_getsockname(const uv_tcp_t* handle,
                       struct sockaddr* name,
                       int* namelen) {

  if (handle->delayed_error)
    return handle->delayed_error;

  return uv__getsockpeername((const uv_handle_t*) handle,
                             getsockname,
                             name,
                             namelen);
}


int uv_tcp_getpeername(const uv_tcp_t* handle,
                       struct sockaddr* name,
                       int* namelen) {

  if (handle->delayed_error)
    return handle->delayed_error;

  return uv__getsockpeername((const uv_handle_t*) handle,
                             getpeername,
                             name,
                             namelen);
}


int uv_tcp_close_reset(uv_tcp_t* handle, uv_close_cb close_cb) {
  int fd;
  struct linger l = { 1, 0 };

  /* Disallow setting SO_LINGER to zero due to some platform inconsistencies */
  if (handle->flags & UV_HANDLE_SHUTTING)
    return UV_EINVAL;

  fd = uv__stream_fd(handle);
  if (0 != setsockopt(fd, SOL_SOCKET, SO_LINGER, &l, sizeof(l)))
    return UV__ERR(errno);

  uv_close((uv_handle_t*) handle, close_cb);
  return 0;
}

/*
    1 设置 socket 为监听状态，即可以等待建立连接
    2 封装 io 观察者，然后注册到事件循环
    3 保存相关上下文，比如业务回调函数
*/
int uv__tcp_listen(uv_tcp_t* tcp, int backlog, uv_connection_cb cb) {
  static int single_accept_cached = -1;
  unsigned long flags;
  int single_accept;
  int err;

  if (tcp->delayed_error)
    return tcp->delayed_error;

  single_accept = uv__load_relaxed(&single_accept_cached);
  // 是否 accept 后，等待一段时间再 accept，否则就是连续 accept
  if (single_accept == -1) {
    const char* val = getenv("UV_TCP_SINGLE_ACCEPT");
    single_accept = (val != NULL && atoi(val) != 0);  /* Off by default. */
    uv__store_relaxed(&single_accept_cached, single_accept);
  }

  // 设置不连续 accept
  if (single_accept)
    tcp->flags |= UV_HANDLE_TCP_SINGLE_ACCEPT;

  flags = 0;
#if defined(__MVS__)
  flags |= UV_HANDLE_BOUND;
#endif
  /*
    可能还没有用于 listen 的 fd，socket 地址等。
    这里申请一个 socket 和绑定到一个地址（如果调 listen 之前没有调 bind 则绑定
    到随机地址）
  */
  err = maybe_new_socket(tcp, AF_INET, flags);
  if (err)
    return err;

  // 设置 fd 为 listen 状态
  if (listen(tcp->io_watcher.fd, backlog))
    return UV__ERR(errno);

  // 建立连接后的业务回调
  tcp->connection_cb = cb;
  tcp->flags |= UV_HANDLE_BOUND;

  // 有连接到来时的 libuv 层回调
  tcp->io_watcher.cb = uv__server_io;
  // 注册 io 观察者到事件循环的 io 观察者队列，设置等待读事件
  uv__io_start(tcp->loop, &tcp->io_watcher, POLLIN);

  return 0;
}


int uv__tcp_nodelay(int fd, int on) {
  if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on)))
    return UV__ERR(errno);
  return 0;
}


int uv__tcp_keepalive(int fd, int on, unsigned int delay) {
  if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &on, sizeof(on)))
    return UV__ERR(errno);

#ifdef TCP_KEEPIDLE
  if (on) {
    int intvl = 1;  /*  1 second; same as default on Win32 */
    int cnt = 10;  /* 10 retries; same as hardcoded on Win32 */
    if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &delay, sizeof(delay)))
      return UV__ERR(errno);
    if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &intvl, sizeof(intvl)))
      return UV__ERR(errno);
    if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &cnt, sizeof(cnt)))
      return UV__ERR(errno);
  }
#endif

  /* Solaris/SmartOS, if you don't support keep-alive,
   * then don't advertise it in your system headers...
   */
  /* FIXME(bnoordhuis) That's possibly because sizeof(delay) should be 1. */
#if defined(TCP_KEEPALIVE) && !defined(__sun)
  if (on && setsockopt(fd, IPPROTO_TCP, TCP_KEEPALIVE, &delay, sizeof(delay)))
    return UV__ERR(errno);
#endif

  return 0;
}


int uv_tcp_nodelay(uv_tcp_t* handle, int on) {
  int err;

  if (uv__stream_fd(handle) != -1) {
    err = uv__tcp_nodelay(uv__stream_fd(handle), on);
    if (err)
      return err;
  }

  if (on)
    handle->flags |= UV_HANDLE_TCP_NODELAY;
  else
    handle->flags &= ~UV_HANDLE_TCP_NODELAY;

  return 0;
}


int uv_tcp_keepalive(uv_tcp_t* handle, int on, unsigned int delay) {
  int err;

  if (uv__stream_fd(handle) != -1) {
    err =uv__tcp_keepalive(uv__stream_fd(handle), on, delay);
    if (err)
      return err;
  }

  if (on)
    handle->flags |= UV_HANDLE_TCP_KEEPALIVE;
  else
    handle->flags &= ~UV_HANDLE_TCP_KEEPALIVE;

  /* TODO Store delay if uv__stream_fd(handle) == -1 but don't want to enlarge
   *      uv_tcp_t with an int that's almost never used...
   */

  return 0;
}


int uv_tcp_simultaneous_accepts(uv_tcp_t* handle, int enable) {
  if (enable)
    handle->flags &= ~UV_HANDLE_TCP_SINGLE_ACCEPT;
  else
    handle->flags |= UV_HANDLE_TCP_SINGLE_ACCEPT;
  return 0;
}


void uv__tcp_close(uv_tcp_t* handle) {
  uv__stream_close((uv_stream_t*)handle);
}


int uv_socketpair(int type, int protocol, uv_os_sock_t fds[2], int flags0, int flags1) {
  uv_os_sock_t temp[2];
  int err;
#if defined(__FreeBSD__) || defined(__linux__)
  int flags;

  flags = type | SOCK_CLOEXEC;
  if ((flags0 & UV_NONBLOCK_PIPE) && (flags1 & UV_NONBLOCK_PIPE))
    flags |= SOCK_NONBLOCK;

  if (socketpair(AF_UNIX, flags, protocol, temp))
    return UV__ERR(errno);

  if (flags & UV_FS_O_NONBLOCK) {
    fds[0] = temp[0];
    fds[1] = temp[1];
    return 0;
  }
#else
  if (socketpair(AF_UNIX, type, protocol, temp))
    return UV__ERR(errno);

  if ((err = uv__cloexec(temp[0], 1)))
    goto fail;
  if ((err = uv__cloexec(temp[1], 1)))
    goto fail;
#endif

  if (flags0 & UV_NONBLOCK_PIPE)
    if ((err = uv__nonblock(temp[0], 1)))
        goto fail;
  if (flags1 & UV_NONBLOCK_PIPE)
    if ((err = uv__nonblock(temp[1], 1)))
      goto fail;

  fds[0] = temp[0];
  fds[1] = temp[1];
  return 0;

fail:
  uv__close(temp[0]);
  uv__close(temp[1]);
  return err;
}
