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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <unistd.h>
#include <limits.h> /* IOV_MAX */

#if defined(__APPLE__)
# include <sys/event.h>
# include <sys/time.h>
# include <sys/select.h>

/* Forward declaration */
typedef struct uv__stream_select_s uv__stream_select_t;

struct uv__stream_select_s {
  uv_stream_t* stream;
  uv_thread_t thread;
  uv_sem_t close_sem;
  uv_sem_t async_sem;
  uv_async_t async;
  int events;
  int fake_fd;
  int int_fd;
  int fd;
  fd_set* sread;
  size_t sread_sz;
  fd_set* swrite;
  size_t swrite_sz;
};
#endif /* defined(__APPLE__) */

static void uv__stream_connect(uv_stream_t*);
static void uv__write(uv_stream_t* stream);
static void uv__read(uv_stream_t* stream);
static void uv__stream_io(uv_loop_t* loop, uv__io_t* w, unsigned int events);
static void uv__write_callbacks(uv_stream_t* stream);
static size_t uv__write_req_size(uv_write_t* req);


void uv__stream_init(uv_loop_t* loop,
                     uv_stream_t* stream,
                     uv_handle_type type) {
  int err;

  // 调用uv__handle_init()函数将stream handle初始化，主要设置loop、类型、以及UV_HANDLE_REF标记
  uv__handle_init(loop, (uv_handle_t*)stream, type);
  // 初始化stream handle中的成员变量
  stream->read_cb = NULL;
  stream->alloc_cb = NULL;
  stream->close_cb = NULL;
  stream->connection_cb = NULL;
  stream->connect_req = NULL;
  stream->shutdown_req = NULL;
  stream->accepted_fd = -1;
  stream->queued_fds = NULL;
  stream->delayed_error = 0;
  // 初始化write_queue与write_completed_queue队列
  QUEUE_INIT(&stream->write_queue);
  QUEUE_INIT(&stream->write_completed_queue);
  stream->write_queue_size = 0;

  if (loop->emfile_fd == -1) {
    err = uv__open_cloexec("/dev/null", O_RDONLY);
    if (err < 0)
        /* In the rare case that "/dev/null" isn't mounted open "/"
         * instead.
         */
        err = uv__open_cloexec("/", O_RDONLY);
    if (err >= 0)
      loop->emfile_fd = err;
  }

#if defined(__APPLE__)
  stream->select = NULL;
#endif /* defined(__APPLE_) */
  // 调用uv__io_init()函数去初始化io观察者，并设置stream的回调处理函数uv__stream_io()
  uv__io_init(&stream->io_watcher, uv__stream_io, -1);
}


static void uv__stream_osx_interrupt_select(uv_stream_t* stream) {
#if defined(__APPLE__)
  /* Notify select() thread about state change */
  uv__stream_select_t* s;
  int r;

  s = stream->select;
  if (s == NULL)
    return;

  /* Interrupt select() loop
   * NOTE: fake_fd and int_fd are socketpair(), thus writing to one will
   * emit read event on other side
   */
  do
    r = write(s->fake_fd, "x", 1);
  while (r == -1 && errno == EINTR);

  assert(r == 1);
#else  /* !defined(__APPLE__) */
  /* No-op on any other platform */
#endif  /* !defined(__APPLE__) */
}


#if defined(__APPLE__)
static void uv__stream_osx_select(void* arg) {
  uv_stream_t* stream;
  uv__stream_select_t* s;
  char buf[1024];
  int events;
  int fd;
  int r;
  int max_fd;

  stream = arg;
  s = stream->select;
  fd = s->fd;

  if (fd > s->int_fd)
    max_fd = fd;
  else
    max_fd = s->int_fd;

  for (;;) {
    /* Terminate on semaphore */
    if (uv_sem_trywait(&s->close_sem) == 0)
      break;

    /* Watch fd using select(2) */
    memset(s->sread, 0, s->sread_sz);
    memset(s->swrite, 0, s->swrite_sz);

    if (uv__io_active(&stream->io_watcher, POLLIN))
      FD_SET(fd, s->sread);
    if (uv__io_active(&stream->io_watcher, POLLOUT))
      FD_SET(fd, s->swrite);
    FD_SET(s->int_fd, s->sread);

    /* Wait indefinitely for fd events */
    r = select(max_fd + 1, s->sread, s->swrite, NULL, NULL);
    if (r == -1) {
      if (errno == EINTR)
        continue;

      /* XXX: Possible?! */
      abort();
    }

    /* Ignore timeouts */
    if (r == 0)
      continue;

    /* Empty socketpair's buffer in case of interruption */
    if (FD_ISSET(s->int_fd, s->sread))
      for (;;) {
        r = read(s->int_fd, buf, sizeof(buf));

        if (r == sizeof(buf))
          continue;

        if (r != -1)
          break;

        if (errno == EAGAIN || errno == EWOULDBLOCK)
          break;

        if (errno == EINTR)
          continue;

        abort();
      }

    /* Handle events */
    events = 0;
    if (FD_ISSET(fd, s->sread))
      events |= POLLIN;
    if (FD_ISSET(fd, s->swrite))
      events |= POLLOUT;

    assert(events != 0 || FD_ISSET(s->int_fd, s->sread));
    if (events != 0) {
      ACCESS_ONCE(int, s->events) = events;

      uv_async_send(&s->async);
      uv_sem_wait(&s->async_sem);

      /* Should be processed at this stage */
      assert((s->events == 0) || (stream->flags & UV_HANDLE_CLOSING));
    }
  }
}


static void uv__stream_osx_select_cb(uv_async_t* handle) {
  uv__stream_select_t* s;
  uv_stream_t* stream;
  int events;

  s = container_of(handle, uv__stream_select_t, async);
  stream = s->stream;

  /* Get and reset stream's events */
  events = s->events;
  ACCESS_ONCE(int, s->events) = 0;

  assert(events != 0);
  assert(events == (events & (POLLIN | POLLOUT)));

  /* Invoke callback on event-loop */
  if ((events & POLLIN) && uv__io_active(&stream->io_watcher, POLLIN))
    uv__stream_io(stream->loop, &stream->io_watcher, POLLIN);

  if ((events & POLLOUT) && uv__io_active(&stream->io_watcher, POLLOUT))
    uv__stream_io(stream->loop, &stream->io_watcher, POLLOUT);

  if (stream->flags & UV_HANDLE_CLOSING)
    return;

  /* NOTE: It is important to do it here, otherwise `select()` might be called
   * before the actual `uv__read()`, leading to the blocking syscall
   */
  uv_sem_post(&s->async_sem);
}


static void uv__stream_osx_cb_close(uv_handle_t* async) {
  uv__stream_select_t* s;

  s = container_of(async, uv__stream_select_t, async);
  uv__free(s);
}


int uv__stream_try_select(uv_stream_t* stream, int* fd) {
  /*
   * kqueue doesn't work with some files from /dev mount on osx.
   * select(2) in separate thread for those fds
   */

  struct kevent filter[1];
  struct kevent events[1];
  struct timespec timeout;
  uv__stream_select_t* s;
  int fds[2];
  int err;
  int ret;
  int kq;
  int old_fd;
  int max_fd;
  size_t sread_sz;
  size_t swrite_sz;

  kq = kqueue();
  if (kq == -1) {
    perror("(libuv) kqueue()");
    return UV__ERR(errno);
  }

  EV_SET(&filter[0], *fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, 0);

  /* Use small timeout, because we only want to capture EINVALs */
  timeout.tv_sec = 0;
  timeout.tv_nsec = 1;

  do
    ret = kevent(kq, filter, 1, events, 1, &timeout);
  while (ret == -1 && errno == EINTR);

  uv__close(kq);

  if (ret == -1)
    return UV__ERR(errno);

  if (ret == 0 || (events[0].flags & EV_ERROR) == 0 || events[0].data != EINVAL)
    return 0;

  /* At this point we definitely know that this fd won't work with kqueue */

  /*
   * Create fds for io watcher and to interrupt the select() loop.
   * NOTE: do it ahead of malloc below to allocate enough space for fd_sets
   */
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds))
    return UV__ERR(errno);

  max_fd = *fd;
  if (fds[1] > max_fd)
    max_fd = fds[1];

  sread_sz = ROUND_UP(max_fd + 1, sizeof(uint32_t) * NBBY) / NBBY;
  swrite_sz = sread_sz;

  s = uv__malloc(sizeof(*s) + sread_sz + swrite_sz);
  if (s == NULL) {
    err = UV_ENOMEM;
    goto failed_malloc;
  }

  s->events = 0;
  s->fd = *fd;
  s->sread = (fd_set*) ((char*) s + sizeof(*s));
  s->sread_sz = sread_sz;
  s->swrite = (fd_set*) ((char*) s->sread + sread_sz);
  s->swrite_sz = swrite_sz;

  err = uv_async_init(stream->loop, &s->async, uv__stream_osx_select_cb);
  if (err)
    goto failed_async_init;

  s->async.flags |= UV_HANDLE_INTERNAL;
  uv__handle_unref(&s->async);

  err = uv_sem_init(&s->close_sem, 0);
  if (err != 0)
    goto failed_close_sem_init;

  err = uv_sem_init(&s->async_sem, 0);
  if (err != 0)
    goto failed_async_sem_init;

  s->fake_fd = fds[0];
  s->int_fd = fds[1];

  old_fd = *fd;
  s->stream = stream;
  stream->select = s;
  *fd = s->fake_fd;

  err = uv_thread_create(&s->thread, uv__stream_osx_select, stream);
  if (err != 0)
    goto failed_thread_create;

  return 0;

failed_thread_create:
  s->stream = NULL;
  stream->select = NULL;
  *fd = old_fd;

  uv_sem_destroy(&s->async_sem);

failed_async_sem_init:
  uv_sem_destroy(&s->close_sem);

failed_close_sem_init:
  uv__close(fds[0]);
  uv__close(fds[1]);
  uv_close((uv_handle_t*) &s->async, uv__stream_osx_cb_close);
  return err;

failed_async_init:
  uv__free(s);

failed_malloc:
  uv__close(fds[0]);
  uv__close(fds[1]);

  return err;
}
#endif /* defined(__APPLE__) */


int uv__stream_open(uv_stream_t* stream, int fd, int flags) {
#if defined(__APPLE__)
  int enable;
#endif

  if (!(stream->io_watcher.fd == -1 || stream->io_watcher.fd == fd))
    return UV_EBUSY;

  assert(fd >= 0);
  stream->flags |= flags;

  if (stream->type == UV_TCP) {
    if ((stream->flags & UV_HANDLE_TCP_NODELAY) && uv__tcp_nodelay(fd, 1))
      return UV__ERR(errno);

    /* TODO Use delay the user passed in. */
    if ((stream->flags & UV_HANDLE_TCP_KEEPALIVE) &&
        uv__tcp_keepalive(fd, 1, 60)) {
      return UV__ERR(errno);
    }
  }

#if defined(__APPLE__)
  enable = 1;
  if (setsockopt(fd, SOL_SOCKET, SO_OOBINLINE, &enable, sizeof(enable)) &&
      errno != ENOTSOCK &&
      errno != EINVAL) {
    return UV__ERR(errno);
  }
#endif

  stream->io_watcher.fd = fd;

  return 0;
}


void uv__stream_flush_write_queue(uv_stream_t* stream, int error) {
  uv_write_t* req;
  QUEUE* q;
  while (!QUEUE_EMPTY(&stream->write_queue)) {
    q = QUEUE_HEAD(&stream->write_queue);
    QUEUE_REMOVE(q);

    req = QUEUE_DATA(q, uv_write_t, queue);
    req->error = error;

    QUEUE_INSERT_TAIL(&stream->write_completed_queue, &req->queue);
  }
}


void uv__stream_destroy(uv_stream_t* stream) {
  assert(!uv__io_active(&stream->io_watcher, POLLIN | POLLOUT));
  assert(stream->flags & UV_HANDLE_CLOSED);

  if (stream->connect_req) {
    uv__req_unregister(stream->loop, stream->connect_req);
    stream->connect_req->cb(stream->connect_req, UV_ECANCELED);
    stream->connect_req = NULL;
  }

  uv__stream_flush_write_queue(stream, UV_ECANCELED);
  uv__write_callbacks(stream);

  if (stream->shutdown_req) {
    /* The ECANCELED error code is a lie, the shutdown(2) syscall is a
     * fait accompli at this point. Maybe we should revisit this in v0.11.
     * A possible reason for leaving it unchanged is that it informs the
     * callee that the handle has been destroyed.
     */
    uv__req_unregister(stream->loop, stream->shutdown_req);
    stream->shutdown_req->cb(stream->shutdown_req, UV_ECANCELED);
    stream->shutdown_req = NULL;
  }

  assert(stream->write_queue_size == 0);
}


/* Implements a best effort approach to mitigating accept() EMFILE errors.
 * We have a spare file descriptor stashed away that we close to get below
 * the EMFILE limit. Next, we accept all pending connections and close them
 * immediately to signal the clients that we're overloaded - and we are, but
 * we still keep on trucking.
 *
 * There is one caveat: it's not reliable in a multi-threaded environment.
 * The file descriptor limit is per process. Our party trick fails if another
 * thread opens a file or creates a socket in the time window between us
 * calling close() and accept().
 */
static int uv__emfile_trick(uv_loop_t* loop, int accept_fd) {
  int err;
  int emfile_fd;

  if (loop->emfile_fd == -1)
    return UV_EMFILE;

  uv__close(loop->emfile_fd);
  loop->emfile_fd = -1;

  do {
    err = uv__accept(accept_fd);
    if (err >= 0)
      uv__close(err);
  } while (err >= 0 || err == UV_EINTR);

  emfile_fd = uv__open_cloexec("/", O_RDONLY);
  if (emfile_fd >= 0)
    loop->emfile_fd = emfile_fd;

  return err;
}


#if defined(UV_HAVE_KQUEUE)
# define UV_DEC_BACKLOG(w) w->rcount--;
#else
# define UV_DEC_BACKLOG(w) /* no-op */
#endif /* defined(UV_HAVE_KQUEUE) */


void uv__server_io(uv_loop_t* loop, uv__io_t* w, unsigned int events) {
  uv_stream_t* stream;
  int err;

  stream = container_of(w, uv_stream_t, io_watcher);
  assert(events & POLLIN);
  assert(stream->accepted_fd == -1);
  assert(!(stream->flags & UV_HANDLE_CLOSING));

  uv__io_start(stream->loop, &stream->io_watcher, POLLIN);

  /* connection_cb can close the server socket while we're
   * in the loop so check it on each iteration.
   */
  while (uv__stream_fd(stream) != -1) {
    assert(stream->accepted_fd == -1);

#if defined(UV_HAVE_KQUEUE)
    if (w->rcount <= 0)
      return;
#endif /* defined(UV_HAVE_KQUEUE) */

    err = uv__accept(uv__stream_fd(stream));
    if (err < 0) {
      if (err == UV_EAGAIN || err == UV__ERR(EWOULDBLOCK))
        return;  /* Not an error. */

      if (err == UV_ECONNABORTED)
        continue;  /* Ignore. Nothing we can do about that. */

      if (err == UV_EMFILE || err == UV_ENFILE) {
        err = uv__emfile_trick(loop, uv__stream_fd(stream));
        if (err == UV_EAGAIN || err == UV__ERR(EWOULDBLOCK))
          break;
      }

      stream->connection_cb(stream, err);
      continue;
    }

    UV_DEC_BACKLOG(w)
    stream->accepted_fd = err;
    stream->connection_cb(stream, 0);

    if (stream->accepted_fd != -1) {
      /* The user hasn't yet accepted called uv_accept() */
      uv__io_stop(loop, &stream->io_watcher, POLLIN);
      return;
    }

    if (stream->type == UV_TCP &&
        (stream->flags & UV_HANDLE_TCP_SINGLE_ACCEPT)) {
      /* Give other processes a chance to accept connections. */
      struct timespec timeout = { 0, 1 };
      nanosleep(&timeout, NULL);
    }
  }
}


#undef UV_DEC_BACKLOG


// 配合 uv_listen() 接受新来的连接
// 在调用这个函数前，客户端句柄必须被初始化
int uv_accept(uv_stream_t* server, uv_stream_t* client) {
  int err;

  assert(server->loop == client->loop);

  if (server->accepted_fd == -1)
    return UV_EAGAIN;

  switch (client->type) {
    case UV_NAMED_PIPE:
    case UV_TCP:
      /* 获取文件描述符，并赋值给 client->io_watcher.fd */
      err = uv__stream_open(client,
                            server->accepted_fd,
                            UV_HANDLE_READABLE | UV_HANDLE_WRITABLE);
      if (err) {
        /* 出现错误就关闭 */
        uv__close(server->accepted_fd);
        goto done;
      }
      break;

    case UV_UDP:
      /* 对于udp协议，通过uv_udp_open去获取文件描述符 */
      err = uv_udp_open((uv_udp_t*) client, server->accepted_fd);
      if (err) {
        uv__close(server->accepted_fd);
        goto done;
      }
      break;

    default:
      return UV_EINVAL;
  }

  client->flags |= UV_HANDLE_BOUND;

done:
  /* 处理在排队的连接请求 */
  if (server->queued_fds != NULL) {
    uv__stream_queued_fds_t* queued_fds;

    queued_fds = server->queued_fds;

    /* 处理第一个排队的 */
    server->accepted_fd = queued_fds->fds[0];

    /* All read, free */
    assert(queued_fds->offset > 0);
    if (--queued_fds->offset == 0) {
      uv__free(queued_fds);
      server->queued_fds = NULL;
    } else {
      /* Shift rest */
      memmove(queued_fds->fds,
              queued_fds->fds + 1,
              queued_fds->offset * sizeof(*queued_fds->fds));
    }
  } else {
    server->accepted_fd = -1;
    if (err == 0)
      uv__io_start(server->loop, &server->io_watcher, POLLIN);
  }
  return err;
}


// 监听连接的请求
// 这个函数只有tcp或者pipe类型的stream handle才可使用
int uv_listen(uv_stream_t* stream, int backlog, uv_connection_cb cb) {
  int err;

  /* 根据类型处理，只有UV_TCP  UV_NAMED_PIPE 才可以 */
  switch (stream->type) {
  case UV_TCP:
    /* tcp连接，调用tcp的listen去处理 */
    err = uv__tcp_listen((uv_tcp_t*)stream, backlog, cb);
    break;

  case UV_NAMED_PIPE:
    /* pipe连接，调用pipe的listen去处理 */
    err = uv__pipe_listen((uv_pipe_t*)stream, backlog, cb);
    break;

  default:
    err = UV_EINVAL;
  }

  if (err == 0)
    uv__handle_start(stream);

  return err;
}


static void uv__drain(uv_stream_t* stream) {
  uv_shutdown_t* req;
  int err;

  assert(QUEUE_EMPTY(&stream->write_queue));
  uv__io_stop(stream->loop, &stream->io_watcher, POLLOUT);
  uv__stream_osx_interrupt_select(stream);

  /* Shutdown? */
  if ((stream->flags & UV_HANDLE_SHUTTING) &&
      !(stream->flags & UV_HANDLE_CLOSING) &&
      !(stream->flags & UV_HANDLE_SHUT)) {
    assert(stream->shutdown_req);

    req = stream->shutdown_req;
    stream->shutdown_req = NULL;
    stream->flags &= ~UV_HANDLE_SHUTTING;
    uv__req_unregister(stream->loop, req);

    err = 0;
    if (shutdown(uv__stream_fd(stream), SHUT_WR))
      err = UV__ERR(errno);

    if (err == 0)
      stream->flags |= UV_HANDLE_SHUT;

    if (req->cb != NULL)
      req->cb(req, err);
  }
}


static ssize_t uv__writev(int fd, struct iovec* vec, size_t n) {
  if (n == 1)
    return write(fd, vec->iov_base, vec->iov_len);
  else
    return writev(fd, vec, n);
}


static size_t uv__write_req_size(uv_write_t* req) {
  size_t size;

  assert(req->bufs != NULL);
  size = uv__count_bufs(req->bufs + req->write_index,
                        req->nbufs - req->write_index);
  assert(req->handle->write_queue_size >= size);

  return size;
}


/* Returns 1 if all write request data has been written, or 0 if there is still
 * more data to write.
 *
 * Note: the return value only says something about the *current* request.
 * There may still be other write requests sitting in the queue.
 */
static int uv__write_req_update(uv_stream_t* stream,
                                uv_write_t* req,
                                size_t n) {
  uv_buf_t* buf;
  size_t len;

  assert(n <= stream->write_queue_size);
  stream->write_queue_size -= n;

  buf = req->bufs + req->write_index;

  do {
    len = n < buf->len ? n : buf->len;
    buf->base += len;
    buf->len -= len;
    buf += (buf->len == 0);  /* Advance to next buffer if this one is empty. */
    n -= len;
  } while (n > 0);

  req->write_index = buf - req->bufs;

  return req->write_index == req->nbufs;
}


static void uv__write_req_finish(uv_write_t* req) {
  uv_stream_t* stream = req->handle;

  /* Pop the req off tcp->write_queue. */
  QUEUE_REMOVE(&req->queue);

  /* Only free when there was no error. On error, we touch up write_queue_size
   * right before making the callback. The reason we don't do that right away
   * is that a write_queue_size > 0 is our only way to signal to the user that
   * they should stop writing - which they should if we got an error. Something
   * to revisit in future revisions of the libuv API.
   */
  if (req->error == 0) {
    if (req->bufs != req->bufsml)
      uv__free(req->bufs);
    req->bufs = NULL;
  }

  /* Add it to the write_completed_queue where it will have its
   * callback called in the near future.
   */
  QUEUE_INSERT_TAIL(&stream->write_completed_queue, &req->queue);
  uv__io_feed(stream->loop, &stream->io_watcher);
}


static int uv__handle_fd(uv_handle_t* handle) {
  switch (handle->type) {
    case UV_NAMED_PIPE:
    case UV_TCP:
      return ((uv_stream_t*) handle)->io_watcher.fd;

    case UV_UDP:
      return ((uv_udp_t*) handle)->io_watcher.fd;

    default:
      return -1;
  }
}

static int uv__try_write(uv_stream_t* stream,
                         const uv_buf_t bufs[],
                         unsigned int nbufs,
                         uv_stream_t* send_handle) {
  struct iovec* iov;
  int iovmax;
  int iovcnt;
  ssize_t n;

  /*
   * Cast to iovec. We had to have our own uv_buf_t instead of iovec
   * because Windows's WSABUF is not an iovec.
   */
  iov = (struct iovec*) bufs;
  iovcnt = nbufs;

  iovmax = uv__getiovmax();

  /* Limit iov count to avoid EINVALs from writev() */
  if (iovcnt > iovmax)
    iovcnt = iovmax;

  /*
   * Now do the actual writev. Note that we've been updating the pointers
   * inside the iov each time we write. So there is no need to offset it.
   */
  if (send_handle != NULL) {
    int fd_to_send;
    struct msghdr msg;
    struct cmsghdr *cmsg;
    union {
      char data[64];
      struct cmsghdr alias;
    } scratch;

    if (uv__is_closing(send_handle))
      return UV_EBADF;

    fd_to_send = uv__handle_fd((uv_handle_t*) send_handle);

    memset(&scratch, 0, sizeof(scratch));

    assert(fd_to_send >= 0);

    msg.msg_name = NULL;
    msg.msg_namelen = 0;
    msg.msg_iov = iov;
    msg.msg_iovlen = iovcnt;
    msg.msg_flags = 0;

    msg.msg_control = &scratch.alias;
    msg.msg_controllen = CMSG_SPACE(sizeof(fd_to_send));

    cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(fd_to_send));

    /* silence aliasing warning */
    {
      void* pv = CMSG_DATA(cmsg);
      int* pi = pv;
      *pi = fd_to_send;
    }

    do
      n = sendmsg(uv__stream_fd(stream), &msg, 0);
    while (n == -1 && errno == EINTR);
  } else {
    do
      n = uv__writev(uv__stream_fd(stream), iov, iovcnt);
    while (n == -1 && errno == EINTR);
  }

  if (n >= 0)
    return n;

  if (errno == EAGAIN || errno == EWOULDBLOCK || errno == ENOBUFS)
    return UV_EAGAIN;

#ifdef __APPLE__
  /* macOS versions 10.10 and 10.15 - and presumbaly 10.11 to 10.14, too -
   * have a bug where a race condition causes the kernel to return EPROTOTYPE
   * because the socket isn't fully constructed. It's probably the result of
   * the peer closing the connection and that is why libuv translates it to
   * ECONNRESET. Previously, libuv retried until the EPROTOTYPE error went
   * away but some VPN software causes the same behavior except the error is
   * permanent, not transient, turning the retry mechanism into an infinite
   * loop. See https://github.com/libuv/libuv/pull/482.
   */
  if (errno == EPROTOTYPE)
    return UV_ECONNRESET;
#endif  /* __APPLE__ */

  return UV__ERR(errno);
}

/*
  当 io 观察者发现要写入数据的时候，它也会去将数据写入到底层，函数 uv__write() 会被调用，那什么时候才是可写呢，
  回顾 stream handle 的成员变量，它有两个队列，当 stream->write_queue 队列存在数据时，表示可以写入，如果队列
  为空则表示没有数据可以写

  libuv的异步处理都是差不多的，都是通过io观察者去发现是否有可读可写，写数据的过程大致如下：用户将数据丢到写队列
  中就直接返回了，io观察者发现队列有数据，stream handle 的处理 uv__stream_io()函数被调用，开始写入操作，这个写
  入的操作是依赖系统的函数接口的，比如write()等，等写完了就通知用户即可
*/
static void uv__write(uv_stream_t* stream) {
  QUEUE* q;
  uv_write_t* req;
  ssize_t n;

  /* 健壮性的处理，断言，确保存在stream handle的fd、队列存在等 */
  assert(uv__stream_fd(stream) >= 0);

  for (;;) {
    if (QUEUE_EMPTY(&stream->write_queue))
      return;

    q = QUEUE_HEAD(&stream->write_queue);
    req = QUEUE_DATA(q, uv_write_t, queue);
    assert(req->handle == stream);

    n = uv__try_write(stream,
                      &(req->bufs[req->write_index]),
                      req->nbufs - req->write_index,
                      req->send_handle);

    /* Ensure the handle isn't sent again in case this is a partial write. */
    if (n >= 0) {
      req->send_handle = NULL;
      if (uv__write_req_update(stream, req, n)) {
        uv__write_req_finish(req);
        return;  /* TODO(bnoordhuis) Start trying to write the next request. */
      }
    } else if (n != UV_EAGAIN)
      break;

    /* If this is a blocking stream, try again. */
    if (stream->flags & UV_HANDLE_BLOCKING_WRITES)
      continue;

    /* 重新启动io观察者 */
    uv__io_start(stream->loop, &stream->io_watcher, POLLOUT);

    /* Notify select() thread about state change */
    uv__stream_osx_interrupt_select(stream);

    return;
  }

  req->error = n;
  // XXX(jwn): this must call uv__stream_flush_write_queue(stream, n) here, since we won't generate any more events
  uv__write_req_finish(req);
  uv__io_stop(stream->loop, &stream->io_watcher, POLLOUT);
  uv__stream_osx_interrupt_select(stream);
}


// 清理 stream->write_completed_queue 已完成写请求的队列，清理空间，并调用回调函数
static void uv__write_callbacks(uv_stream_t* stream) {
  uv_write_t* req;
  QUEUE* q;
  QUEUE pq;

  if (QUEUE_EMPTY(&stream->write_completed_queue))
    return;

  /* 从写完成队列获取队列 */
  QUEUE_MOVE(&stream->write_completed_queue, &pq);

  /* 队列不为空 */
  while (!QUEUE_EMPTY(&pq)) {
    /* 获取队列头部节点 */
    q = QUEUE_HEAD(&pq);
    req = QUEUE_DATA(q, uv_write_t, queue);
    QUEUE_REMOVE(q);
    uv__req_unregister(stream->loop, req);

    /* 清除并释放内存 */
    if (req->bufs != NULL) {
      stream->write_queue_size -= uv__write_req_size(req);
      if (req->bufs != req->bufsml)
        uv__free(req->bufs);
      req->bufs = NULL;
    }

    /* 通知应用层 */
    if (req->cb)
      req->cb(req, req->error);
  }
}


static void uv__stream_eof(uv_stream_t* stream, const uv_buf_t* buf) {
  stream->flags |= UV_HANDLE_READ_EOF;
  stream->flags &= ~UV_HANDLE_READING;
  uv__io_stop(stream->loop, &stream->io_watcher, POLLIN);
  uv__handle_stop(stream);
  uv__stream_osx_interrupt_select(stream);
  stream->read_cb(stream, UV_EOF, buf);
}


static int uv__stream_queue_fd(uv_stream_t* stream, int fd) {
  uv__stream_queued_fds_t* queued_fds;
  unsigned int queue_size;

  queued_fds = stream->queued_fds;
  if (queued_fds == NULL) {
    queue_size = 8;
    queued_fds = uv__malloc((queue_size - 1) * sizeof(*queued_fds->fds) +
                            sizeof(*queued_fds));
    if (queued_fds == NULL)
      return UV_ENOMEM;
    queued_fds->size = queue_size;
    queued_fds->offset = 0;
    stream->queued_fds = queued_fds;

    /* Grow */
  } else if (queued_fds->size == queued_fds->offset) {
    queue_size = queued_fds->size + 8;
    queued_fds = uv__realloc(queued_fds,
                             (queue_size - 1) * sizeof(*queued_fds->fds) +
                              sizeof(*queued_fds));

    /*
     * Allocation failure, report back.
     * NOTE: if it is fatal - sockets will be closed in uv__stream_close
     */
    if (queued_fds == NULL)
      return UV_ENOMEM;
    queued_fds->size = queue_size;
    stream->queued_fds = queued_fds;
  }

  /* Put fd in a queue */
  queued_fds->fds[queued_fds->offset++] = fd;

  return 0;
}


#if defined(__PASE__)
/* on IBMi PASE the control message length can not exceed 256. */
# define UV__CMSG_FD_COUNT 60
#else
# define UV__CMSG_FD_COUNT 64
#endif
#define UV__CMSG_FD_SIZE (UV__CMSG_FD_COUNT * sizeof(int))


static int uv__stream_recv_cmsg(uv_stream_t* stream, struct msghdr* msg) {
  struct cmsghdr* cmsg;

  for (cmsg = CMSG_FIRSTHDR(msg); cmsg != NULL; cmsg = CMSG_NXTHDR(msg, cmsg)) {
    char* start;
    char* end;
    int err;
    void* pv;
    int* pi;
    unsigned int i;
    unsigned int count;

    if (cmsg->cmsg_type != SCM_RIGHTS) {
      fprintf(stderr, "ignoring non-SCM_RIGHTS ancillary data: %d\n",
          cmsg->cmsg_type);
      continue;
    }

    /* silence aliasing warning */
    pv = CMSG_DATA(cmsg);
    pi = pv;

    /* Count available fds */
    start = (char*) cmsg;
    end = (char*) cmsg + cmsg->cmsg_len;
    count = 0;
    while (start + CMSG_LEN(count * sizeof(*pi)) < end)
      count++;
    assert(start + CMSG_LEN(count * sizeof(*pi)) == end);

    for (i = 0; i < count; i++) {
      /* Already has accepted fd, queue now */
      if (stream->accepted_fd != -1) {
        err = uv__stream_queue_fd(stream, pi[i]);
        if (err != 0) {
          /* Close rest */
          for (; i < count; i++)
            uv__close(pi[i]);
          return err;
        }
      } else {
        stream->accepted_fd = pi[i];
      }
    }
  }

  return 0;
}


#ifdef __clang__
# pragma clang diagnostic push
# pragma clang diagnostic ignored "-Wgnu-folding-constant"
# pragma clang diagnostic ignored "-Wvla-extension"
#endif

// 当io观察者发现stream handle有可读事件时，uv__read()函数会被调用，其实是被uv__stream_io()函数调用，
// 因为io观察者发现了底层有数据可读。所以该函数是用于从底层读取数据，这也是stream handle的读取操作
/*
  uv__read()函数是通过 read() 函数从底层文件描述符读取数据，读取的数据写入由 stream->alloc_cb 分配到
  内存块中，并在完成读取后由 stream->read_cb 回调函数传递到用户。因为数据已经由底层准备好，直接读取即
  可，效率非常高，是不需要等待的。而当底层没有数据的情况时，read() 系统调用也会阻塞，而是直接返回，因
  为文件描述符工作在非阻塞模式下，即使底层还没有数据，它也不会阻塞的，而真正阻塞的地方是在io循环中
*/
static void uv__read(uv_stream_t* stream) {
  uv_buf_t buf;
  ssize_t nread;
  struct msghdr msg;
  char cmsg_space[CMSG_SPACE(UV__CMSG_FD_SIZE)];
  int count;
  int err;
  int is_ipc;

  stream->flags &= ~UV_HANDLE_READ_PARTIAL;

  /* Prevent loop starvation when the data comes in as fast as (or faster than)
   * we can read it. XXX Need to rearm fd if we switch to edge-triggered I/O.
   */
  count = 32;

  /* 看看是不是管道，IPC通讯机制 */
  is_ipc = stream->type == UV_NAMED_PIPE && ((uv_pipe_t*) stream)->ipc;

  /* XXX: Maybe instead of having UV_HANDLE_READING we just test if
   * tcp->read_cb is NULL or not?
   */
  while (stream->read_cb
      && (stream->flags & UV_HANDLE_READING)
      && (count-- > 0)) {
    assert(stream->alloc_cb != NULL);

    buf = uv_buf_init(NULL, 0);
    /* 分配内存 */
    stream->alloc_cb((uv_handle_t*)stream, 64 * 1024, &buf);
    if (buf.base == NULL || buf.len == 0) {
      /* User indicates it can't or won't handle the read. */
      /* 如果内存分配失败或者buffer的长度是0则无法读取 */
      stream->read_cb(stream, UV_ENOBUFS, &buf);
      return;
    }

    /* 断言，保证有内存空间与stream handle的文件描述符是存在的 */
    assert(buf.base != NULL);
    assert(uv__stream_fd(stream) >= 0);

    /* 如果不是pipe */
    if (!is_ipc) {
      do {
        /* 通过read()去读取底层数据 */
        nread = read(uv__stream_fd(stream), buf.base, buf.len);
      }
      /* 直到读取完毕 */
      while (nread < 0 && errno == EINTR);
    } else {
      /* ipc 需要使用 recvmsg() 函数去读取 */
      msg.msg_flags = 0;
      msg.msg_iov = (struct iovec*) &buf;
      msg.msg_iovlen = 1;
      msg.msg_name = NULL;
      msg.msg_namelen = 0;
      /* Set up to receive a descriptor even if one isn't in the message */
      msg.msg_controllen = sizeof(cmsg_space);
      msg.msg_control = cmsg_space;

      do {
        /* 读取ipc机制的数据 */
        nread = uv__recvmsg(uv__stream_fd(stream), &msg, 0);
      }
      while (nread < 0 && errno == EINTR);
    }

    /* 读取数据错误 */
    if (nread < 0) {
      /* Error */
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        /* 开始下一次的等待. */
        if (stream->flags & UV_HANDLE_READING) {
          /* 重新设置io观察者活跃 */
          uv__io_start(stream->loop, &stream->io_watcher, POLLIN);
          uv__stream_osx_interrupt_select(stream);
        }
        stream->read_cb(stream, 0, &buf);
#if defined(__CYGWIN__) || defined(__MSYS__)
      } else if (errno == ECONNRESET && stream->type == UV_NAMED_PIPE) {
        uv__stream_eof(stream, &buf);
        return;
#endif
      } else {
        /* Error. User should call uv_close(). */
        /* 错误，用户应该调用uv_close()关闭 */
        stream->flags &= ~(UV_HANDLE_READABLE | UV_HANDLE_WRITABLE);
        stream->read_cb(stream, UV__ERR(errno), &buf);
        if (stream->flags & UV_HANDLE_READING) {
          stream->flags &= ~UV_HANDLE_READING;
          uv__io_stop(stream->loop, &stream->io_watcher, POLLIN);
          uv__handle_stop(stream);
          uv__stream_osx_interrupt_select(stream);
        }
      }
      return;
    } else if (nread == 0) {
      uv__stream_eof(stream, &buf);
      return;
    } else {
      /* 成功读取到数据 */
      ssize_t buflen = buf.len;

      /* ipc就这样子读取 */
      if (is_ipc) {
        err = uv__stream_recv_cmsg(stream, &msg);
        if (err != 0) {
          stream->read_cb(stream, err, &buf);
          return;
        }
      }

#if defined(__MVS__)
      if (is_ipc && msg.msg_controllen > 0) {
        uv_buf_t blankbuf;
        int nread;
        struct iovec *old;

        blankbuf.base = 0;
        blankbuf.len = 0;
        old = msg.msg_iov;
        msg.msg_iov = (struct iovec*) &blankbuf;
        nread = 0;
        do {
          nread = uv__recvmsg(uv__stream_fd(stream), &msg, 0);
          err = uv__stream_recv_cmsg(stream, &msg);
          if (err != 0) {
            stream->read_cb(stream, err, &buf);
            msg.msg_iov = old;
            return;
          }
        } while (nread == 0 && msg.msg_controllen > 0);
        msg.msg_iov = old;
      }
#endif
      /* 通过回调函数告诉应用层读取完成 */
      stream->read_cb(stream, nread, &buf);

      /* 如果没有填满缓冲区，则返回没有更多数据可读取。 */
      if (nread < buflen) {
        stream->flags |= UV_HANDLE_READ_PARTIAL;
        return;
      }
    }
  }
}


#ifdef __clang__
# pragma clang diagnostic pop
#endif

#undef UV__CMSG_FD_COUNT
#undef UV__CMSG_FD_SIZE

// 关闭流的写端口，它会等待未完成的写操作，在关闭后通过uv_shutdown_cb指定的回调函数告知应用层
int uv_shutdown(uv_shutdown_t* req, uv_stream_t* stream, uv_shutdown_cb cb) {
  /* 校验相关信息，只有 UV_TCP UV_TTY UV_NAMED_PIPE 类型的stream handle 才可以用*/
  assert(stream->type == UV_TCP ||
         stream->type == UV_TTY ||
         stream->type == UV_NAMED_PIPE);

  if (!(stream->flags & UV_HANDLE_WRITABLE) ||
      stream->flags & UV_HANDLE_SHUT ||
      stream->flags & UV_HANDLE_SHUTTING ||
      uv__is_closing(stream)) {
    return UV_ENOTCONN;
  }

  assert(uv__stream_fd(stream) >= 0);

  /* 初始化请求 */
  uv__req_init(stream->loop, req, UV_SHUTDOWN);
  req->handle = stream;
  req->cb = cb;
  stream->shutdown_req = req;
  stream->flags |= UV_HANDLE_SHUTTING;

  /* 设置关闭标志位 */
  stream->flags &= ~UV_HANDLE_WRITABLE;

  /* 初始化io观察者，将 io 观察者加入到队列中后，以便在事件循环的特定阶段进行处理 */
  uv__io_start(stream->loop, &stream->io_watcher, POLLOUT);
  uv__stream_osx_interrupt_select(stream);

  return 0;
}


static void uv__stream_io(uv_loop_t* loop, uv__io_t* w, unsigned int events) {
  uv_stream_t* stream;

  // 通过container_of()函数获取 stream handle 的实例
  stream = container_of(w, uv_stream_t, io_watcher);

  assert(stream->type == UV_TCP ||
         stream->type == UV_NAMED_PIPE ||
         stream->type == UV_TTY);
  assert(!(stream->flags & UV_HANDLE_CLOSING));

  // 如果 stream->connect_req存在，说明 该 stream handle 需要进行连接，于是调用 uv__stream_connect() 函数请求建立连接
  if (stream->connect_req) {
    uv__stream_connect(stream);
    return;
  }

  assert(uv__stream_fd(stream) >= 0);

  /* Ignore POLLHUP here. Even if it's set, there may still be data to read. */
  // 满足可读取数据的条件，调用uv__read()函数进行数据读取
  if (events & (POLLIN | POLLERR | POLLHUP))
    uv__read(stream);

  // read_cb 可能会关闭 stream，此处判断一下是否需要关闭fd
  if (uv__stream_fd(stream) == -1)
    return;  /* read_cb closed stream. */

  /* Short-circuit iff POLLHUP is set, the user is still interested in read
   * events and uv__read() reported a partial read but not EOF. If the EOF
   * flag is set, uv__read() called read_cb with err=UV_EOF and we don't
   * have to do anything. If the partial read flag is not set, we can't
   * report the EOF yet because there is still data to read.
   */
  // 如果满足流结束条件 调用 uv__stream_eof() 进行相关处理
  if ((events & POLLHUP) &&
      (stream->flags & UV_HANDLE_READING) &&
      (stream->flags & UV_HANDLE_READ_PARTIAL) &&
      !(stream->flags & UV_HANDLE_READ_EOF)) {
    uv_buf_t buf = { NULL, 0 };
    uv__stream_eof(stream, &buf);
  }

  // read_cb 可能会关闭 stream，此处判断一下是否需要关闭fd
  if (uv__stream_fd(stream) == -1)
    return;  /* read_cb closed stream. */

  // 如果满足可写条件，调用 uv__write() 函数去写入数据，当然，数据会被放在 stream->write_queue 队列中
  if (events & (POLLOUT | POLLERR | POLLHUP)) {
    uv__write(stream);
    // 在写完数据后，调用 uv__write_callbacks() 函数去清除队列的数据，并通知应用层已经写完了
    uv__write_callbacks(stream);

    /* Write queue drained. */
    if (QUEUE_EMPTY(&stream->write_queue))
      uv__drain(stream);
  }
}


/**
 * We get called here from directly following a call to connect(2).
 * In order to determine if we've errored out or succeeded must call
 * getsockopt.
 */
static void uv__stream_connect(uv_stream_t* stream) {
  int error;
  uv_connect_t* req = stream->connect_req;
  socklen_t errorsize = sizeof(int);

  assert(stream->type == UV_TCP || stream->type == UV_NAMED_PIPE);
  assert(req);

  if (stream->delayed_error) {
    /* To smooth over the differences between unixes errors that
     * were reported synchronously on the first connect can be delayed
     * until the next tick--which is now.
     */
    error = stream->delayed_error;
    stream->delayed_error = 0;
  } else {
    /* Normal situation: we need to get the socket error from the kernel. */
    assert(uv__stream_fd(stream) >= 0);
    getsockopt(uv__stream_fd(stream),
               SOL_SOCKET,
               SO_ERROR,
               &error,
               &errorsize);
    error = UV__ERR(error);
  }

  if (error == UV__ERR(EINPROGRESS))
    return;

  stream->connect_req = NULL;
  uv__req_unregister(stream->loop, req);

  if (error < 0 || QUEUE_EMPTY(&stream->write_queue)) {
    uv__io_stop(stream->loop, &stream->io_watcher, POLLOUT);
  }

  if (req->cb)
    req->cb(req, error);

  if (uv__stream_fd(stream) == -1)
    return;

  if (error < 0) {
    uv__stream_flush_write_queue(stream, UV_ECANCELED);
    uv__write_callbacks(stream);
  }
}


static int uv__check_before_write(uv_stream_t* stream,
                                  unsigned int nbufs,
                                  uv_stream_t* send_handle) {
  assert(nbufs > 0);
  assert((stream->type == UV_TCP ||
          stream->type == UV_NAMED_PIPE ||
          stream->type == UV_TTY) &&
         "uv_write (unix) does not yet support other types of streams");

  if (uv__stream_fd(stream) < 0)
    return UV_EBADF;

  if (!(stream->flags & UV_HANDLE_WRITABLE))
    return UV_EPIPE;

  if (send_handle != NULL) {
    if (stream->type != UV_NAMED_PIPE || !((uv_pipe_t*)stream)->ipc)
      return UV_EINVAL;

    /* XXX We abuse uv_write2() to send over UDP handles to child processes.
     * Don't call uv__stream_fd() on those handles, it's a macro that on OS X
     * evaluates to a function that operates on a uv_stream_t with a couple of
     * OS X specific fields. On other Unices it does (handle)->io_watcher.fd,
     * which works but only by accident.
     */
    if (uv__handle_fd((uv_handle_t*) send_handle) < 0)
      return UV_EBADF;

#if defined(__CYGWIN__) || defined(__MSYS__)
    /* Cygwin recvmsg always sets msg_controllen to zero, so we cannot send it.
       See https://github.com/mirror/newlib-cygwin/blob/86fc4bf0/winsup/cygwin/fhandler_socket.cc#L1736-L1743 */
    return UV_ENOSYS;
#endif
  }

  return 0;
}

// 扩展的写函数，可用于在管道上发送数据
int uv_write2(uv_write_t* req,
              uv_stream_t* stream,
              const uv_buf_t bufs[],
              unsigned int nbufs,
              uv_stream_t* send_handle,
              uv_write_cb cb) {
  int empty_queue;
  int err;

  err = uv__check_before_write(stream, nbufs, send_handle);
  if (err < 0)
    return err;

  /* It's legal for write_queue_size > 0 even when the write_queue is empty;
   * it means there are error-state requests in the write_completed_queue that
   * will touch up write_queue_size later, see also uv__write_req_finish().
   * We could check that write_queue is empty instead but that implies making
   * a write() syscall when we know that the handle is in error mode.
   */
  /* 即使write_queue为空，write_queue_size> 0也合法；这意味着write_completed_queue中存在错误状态请求，这些错误状态请求将在以后修改write_queue_size */
  empty_queue = (stream->write_queue_size == 0);

  /* Initialize the req */
  /* 初始化请求 */
  uv__req_init(stream->loop, req, UV_WRITE);

  /* 注册回调函数，注册请求的stream handle */
  req->cb = cb;
  req->handle = stream;
  req->error = 0;
  req->send_handle = send_handle;
  QUEUE_INIT(&req->queue);

  req->bufs = req->bufsml;
  if (nbufs > ARRAY_SIZE(req->bufsml))
    req->bufs = uv__malloc(nbufs * sizeof(bufs[0]));

  if (req->bufs == NULL)
    return UV_ENOMEM;

  memcpy(req->bufs, bufs, nbufs * sizeof(bufs[0]));
  req->nbufs = nbufs;
  req->write_index = 0;
  stream->write_queue_size += uv__count_bufs(bufs, nbufs);

  /* Append the request to write_queue. */
  /* 将请求追加到write_queue。 */
  QUEUE_INSERT_TAIL(&stream->write_queue, &req->queue);

  /* If the queue was empty when this function began, we should attempt to
   * do the write immediately. Otherwise start the write_watcher and wait
   * for the fd to become writable.
   */
  /* 如果此函数开始时队列为空，则应尝试立即进行写操作。否则，启动write_watcher并等待fd变为可写状态。 */
  if (stream->connect_req) {
    /* Still connecting, do nothing. */
    /* 仍在连接，什么也不做. */
  }
  else if (empty_queue) {
    uv__write(stream);
  }
  else {
    /*
     * blocking streams should never have anything in the queue.
     * if this assert fires then somehow the blocking stream isn't being
     * sufficiently flushed in uv__write.
     */
    /* 阻塞流永远不会在队列中有任何东西。 */
    assert(!(stream->flags & UV_HANDLE_BLOCKING_WRITES));
    uv__io_start(stream->loop, &stream->io_watcher, POLLOUT);
    uv__stream_osx_interrupt_select(stream);
  }

  return 0;
}


/* The buffers to be written must remain valid until the callback is called.
 * This is not required for the uv_buf_t array.
 */
// 向stream handle 写入数据，实际上是调用uv_write2()这个函数
int uv_write(uv_write_t* req,
             uv_stream_t* handle,
             const uv_buf_t bufs[],
             unsigned int nbufs,
             uv_write_cb cb) {
  return uv_write2(req, handle, bufs, nbufs, NULL, cb);
}


int uv_try_write(uv_stream_t* stream,
                 const uv_buf_t bufs[],
                 unsigned int nbufs) {
  return uv_try_write2(stream, bufs, nbufs, NULL);
}


int uv_try_write2(uv_stream_t* stream,
                  const uv_buf_t bufs[],
                  unsigned int nbufs,
                  uv_stream_t* send_handle) {
  int err;

  /* Connecting or already writing some data */
  if (stream->connect_req != NULL || stream->write_queue_size != 0)
    return UV_EAGAIN;

  err = uv__check_before_write(stream, nbufs, NULL);
  if (err < 0)
    return err;

  return uv__try_write(stream, bufs, nbufs, send_handle);
}


int uv__read_start(uv_stream_t* stream,
                   uv_alloc_cb alloc_cb,
                   uv_read_cb read_cb) {
  assert(stream->type == UV_TCP || stream->type == UV_NAMED_PIPE ||
      stream->type == UV_TTY);

  /* The UV_HANDLE_READING flag is irrelevant of the state of the stream - it
   * just expresses the desired state of the user. */
  /* 设置标志位，表示此时流正在被使用读取 */
  stream->flags |= UV_HANDLE_READING;
  stream->flags &= ~UV_HANDLE_READ_EOF;

  /* TODO: try to do the read inline? */
  assert(uv__stream_fd(stream) >= 0);
  assert(alloc_cb);

  /* 注册回调函数 */
  stream->read_cb = read_cb;
  stream->alloc_cb = alloc_cb;

  /* 启动io观察者 */
  uv__io_start(stream->loop, &stream->io_watcher, POLLIN);
  uv__handle_start(stream);
  uv__stream_osx_interrupt_select(stream);

  return 0;
}

// 与uv_read_start()函数刚好相反，uv_read_stop()函数是停止从流读取数据。 uv_read_cb 回调函数将不再被调用
int uv_read_stop(uv_stream_t* stream) {
  /* 如果它未被设置为 UV_HANDLE_READING 读取占用标志位，则不需要停止 */
  if (!(stream->flags & UV_HANDLE_READING))
    return 0;

  /* 取消设置 UV_HANDLE_READING 标志位 */
  stream->flags &= ~UV_HANDLE_READING;

  /* 停止流读取，关闭流io观察者 */
  uv__io_stop(stream->loop, &stream->io_watcher, POLLIN);
  /* 关闭这个 stream handle */
  uv__handle_stop(stream);
  uv__stream_osx_interrupt_select(stream);

  /* 取消注册回调函数 */
  stream->read_cb = NULL;
  stream->alloc_cb = NULL;
  return 0;
}


int uv_is_readable(const uv_stream_t* stream) {
  return !!(stream->flags & UV_HANDLE_READABLE);
}


int uv_is_writable(const uv_stream_t* stream) {
  return !!(stream->flags & UV_HANDLE_WRITABLE);
}


#if defined(__APPLE__)
int uv___stream_fd(const uv_stream_t* handle) {
  const uv__stream_select_t* s;

  assert(handle->type == UV_TCP ||
         handle->type == UV_TTY ||
         handle->type == UV_NAMED_PIPE);

  s = handle->select;
  if (s != NULL)
    return s->fd;

  return handle->io_watcher.fd;
}
#endif /* defined(__APPLE__) */


void uv__stream_close(uv_stream_t* handle) {
  unsigned int i;
  uv__stream_queued_fds_t* queued_fds;

#if defined(__APPLE__)
  /* Terminate select loop first */
  if (handle->select != NULL) {
    uv__stream_select_t* s;

    s = handle->select;

    uv_sem_post(&s->close_sem);
    uv_sem_post(&s->async_sem);
    uv__stream_osx_interrupt_select(handle);
    uv_thread_join(&s->thread);
    uv_sem_destroy(&s->close_sem);
    uv_sem_destroy(&s->async_sem);
    uv__close(s->fake_fd);
    uv__close(s->int_fd);
    uv_close((uv_handle_t*) &s->async, uv__stream_osx_cb_close);

    handle->select = NULL;
  }
#endif /* defined(__APPLE__) */

  uv__io_close(handle->loop, &handle->io_watcher);
  uv_read_stop(handle);
  uv__handle_stop(handle);
  handle->flags &= ~(UV_HANDLE_READABLE | UV_HANDLE_WRITABLE);

  if (handle->io_watcher.fd != -1) {
    /* Don't close stdio file descriptors.  Nothing good comes from it. */
    if (handle->io_watcher.fd > STDERR_FILENO)
      uv__close(handle->io_watcher.fd);
    handle->io_watcher.fd = -1;
  }

  if (handle->accepted_fd != -1) {
    uv__close(handle->accepted_fd);
    handle->accepted_fd = -1;
  }

  /* Close all queued fds */
  if (handle->queued_fds != NULL) {
    queued_fds = handle->queued_fds;
    for (i = 0; i < queued_fds->offset; i++)
      uv__close(queued_fds->fds[i]);
    uv__free(handle->queued_fds);
    handle->queued_fds = NULL;
  }

  assert(!uv__io_active(&handle->io_watcher, POLLIN | POLLOUT));
}


int uv_stream_set_blocking(uv_stream_t* handle, int blocking) {
  /* Don't need to check the file descriptor, uv__nonblock()
   * will fail with EBADF if it's not valid.
   */
  return uv__nonblock(uv__stream_fd(handle), !blocking);
}
