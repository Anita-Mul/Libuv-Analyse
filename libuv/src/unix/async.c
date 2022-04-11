/* Copyright Joyent, Inc. and other Node contributors. All rights reserved.
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

/* This file contains both the uv__async internal infrastructure and the
 * user-facing uv_async_t functions.
 */

#include "uv.h"
#include "internal.h"
#include "atomic-ops.h"

#include <errno.h>
#include <stdio.h>  /* snprintf() */
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sched.h>  /* sched_yield() */

#ifdef __linux__
#include <sys/eventfd.h>
#endif

static void uv__async_send(uv_loop_t* loop);
static int uv__async_start(uv_loop_t* loop);


int uv_async_init(uv_loop_t* loop, uv_async_t* handle, uv_async_cb async_cb) {
  int err;

  /* 其实这个函数的内部主要做了以下操作：创建一个管道，并且将管道注册到loop->async_io_watcher，并且start */
  err = uv__async_start(loop);
  if (err)
    return err;

  /* 设置UV_HANDLE_REF标记，并且将async handle 插入loop->handle_queue */
  uv__handle_init(loop, (uv_handle_t*)handle, UV_ASYNC);

  handle->async_cb = async_cb;
  // 标记是否有任务完成了
  handle->pending = 0;

  /* queue 作为队列节点插入 loop->async_handles */
  QUEUE_INSERT_TAIL(&loop->async_handles, &handle->queue);
  // 激活 handle 为 active 状态
  uv__handle_start(handle);

  return 0;
}

/*
  uv_async_send()函数发送消息唤醒事件循环线程并触发回调函数调用，其实我们不难想象出来，
  它的唤醒就是将消息写入管道中，让io观察者发现管道有数据从而唤醒事件循环线程，并随之处
  理这个async handle
*/
int uv_async_send(uv_async_t* handle) {
  /* Do a cheap read first. */
  if (ACCESS_ONCE(int, handle->pending) != 0)
    return 0;

  /*
    设置 async handle 的 pending 标记
    如果 pending 是 0，则设置为 1，返回 0，如果是 1 则返回 1，
    所以同一个 handle 如果多次调用该函数是会被合并的
  */
  if (cmpxchgi(&handle->pending, 0, 1) != 0)
    return 0;

  /* Wake up the other thread's event loop. */
  // 真正的唤醒操作是uv__async_send()函数
  uv__async_send(handle->loop);

  /* Tell the other thread we're done. */
  if (cmpxchgi(&handle->pending, 1, 2) != 1)
    abort();

  return 0;
}


/* Only call this from the event loop thread. */
static int uv__async_spin(uv_async_t* handle) {
  int i;
  int rc;

  for (;;) {
    /* 997 is not completely chosen at random. It's a prime number, acyclical
     * by nature, and should therefore hopefully dampen sympathetic resonance.
     */
    for (i = 0; i < 997; i++) {
      /* rc=0 -- handle is not pending.
       * rc=1 -- handle is pending, other thread is still working with it.
       * rc=2 -- handle is pending, other thread is done.
       */
      rc = cmpxchgi(&handle->pending, 2, 0);

      if (rc != 1)
        return rc;

      /* Other thread is busy with this handle, spin until it's done. */
      cpu_relax();
    }

    /* Yield the CPU. We may have preempted the other thread while it's
     * inside the critical section and if it's running on the same CPU
     * as us, we'll just burn CPU cycles until the end of our time slice.
     */
    sched_yield();
  }
}


void uv__async_close(uv_async_t* handle) {
  uv__async_spin(handle);
  QUEUE_REMOVE(&handle->queue);
  uv__handle_stop(handle);
}

/*
  我们注意到uv_async_init()函数可能多次被调用，初始化多个async handle，但是 loop->async_io_watcher 
  只有一个，那么问题来了，那么多个async handle都共用一个io观察者（假设loop是一个），那么在 loop->async_io_watcher 
  上有 I/O 事件时，并不知道是哪个 async handle 发送的，因此我们要知道async handle是如何处理这些的
*/
static void uv__async_io(uv_loop_t* loop, uv__io_t* w, unsigned int events) {
  char buf[1024];
  ssize_t r;
  QUEUE queue;
  QUEUE* q;
  uv_async_t* h;

  assert(w == &loop->async_io_watcher);

  for (;;) {
    /* 不断的读取 w->fd 上的数据到 buf 中直到为空，buf 中的数据无实际用途 */
    r = read(w->fd, buf, sizeof(buf));

    // 如果数据大于 buf 的长度，接着读，清空这一轮写入的数据
    if (r == sizeof(buf))
      continue;

    // 不等于-1，说明读成功，失败的时候返回-1，errno 是错误码
    if (r != -1)
      break;

    if (errno == EAGAIN || errno == EWOULDBLOCK)
      break;

    // 被信号中断，继续读
    if (errno == EINTR)
      continue;

    // 出错，发送 abort 信号
    abort();
  }

  // 把 async_handles 队列里的所有节点都移到 queue 变量中
  QUEUE_MOVE(&loop->async_handles, &queue);

  /* 遍历队列判断是谁发的消息，并执行相应的回调函数 */
  // 因为 uv_async_init() 函数可能多次被调用，始化多个 async handle，但是 loop->async_io_watcher 
  // 只有一个，所以要遍历 async_handles，来看看是哪些 async 被触发了
  while (!QUEUE_EMPTY(&queue)) {
    // 逐个取出节点
    q = QUEUE_HEAD(&queue);

    // 根据结构体字段获取结构体首地址
    h = QUEUE_DATA(q, uv_async_t, queue);

    // 从队列中移除该节点
    QUEUE_REMOVE(q);

    // 重新插入 async_handles 队列，等待下次事件
    QUEUE_INSERT_TAIL(&loop->async_handles, q);

    /*
        判断哪些 async 被触发了。pending 在 uv_async_send
        里设置成 1，如果 pending 等于 1，则清 0，返回 1.如果
        pending 等于 0，则返回 0
    */
    if (0 == uv__async_spin(h))
      continue;  /* Not pending. */

    if (h->async_cb == NULL)
      continue;

    /* 调用async 的回调函数 */
    h->async_cb(h);
  }
}


static void uv__async_send(uv_loop_t* loop) {
  const void* buf;
  ssize_t len;
  int fd;
  int r;

  buf = "";
  len = 1;
  // 用于异步通信的管道的写端
  fd = loop->async_wfd;

#if defined(__linux__)
  // 说明用的是 eventfd 而不是管道
  if (fd == -1) {
    static const uint64_t val = 1;
    buf = &val;
    len = sizeof(val);
    fd = loop->async_io_watcher.fd;  /* eventfd */
  }
#endif
  // 通知读端
  do
    r = write(fd, buf, len);
  while (r == -1 && errno == EINTR);

  if (r == len)
    return;

  if (r == -1)
    if (errno == EAGAIN || errno == EWOULDBLOCK)
      return;

  abort();
}


// 初始化异步通信的 io 观察者
static int uv__async_start(uv_loop_t* loop) {
  int pipefd[2];
  int err;

  /*
    因为 libuv 在初始化的时候会主动注册一个
    用于主线程和子线程通信的 async handle。
    从而初始化了 async_io_watcher。所以如果后续
    再注册 async handle，则不需要处理了。
    
    父子线程通信时，libuv 是优先使用 eventfd，如果不支持会回退到匿名管道。
    如果是匿名管道
    fd 是管道的读端，loop->async_wfd 是管道的写端
    如果是 eventfd
    fd 是读端也是写端。async_wfd 是-1 
    所以这里判断 loop->async_io_watcher.fd 而不是 async_wfd 的值
 */
  if (loop->async_io_watcher.fd != -1)
    return 0;

  #ifdef __linux__
    // 获取一个用于进程间通信的 fd
    err = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (err < 0)
      return UV__ERR(errno);

    // 成功则保存起来，不支持则使用管道通信作为进程间通信
    pipefd[0] = err;
    pipefd[1] = -1;
  #else
    err = uv__make_pipe(pipefd, UV_NONBLOCK_PIPE);
    if (err < 0)
      return err;
  #endif

  // 初始化 io 观察者 async_io_watcher
  uv__io_init(&loop->async_io_watcher, uv__async_io, pipefd[0]);
  // 注册 io 观察者到 loop 里，并注册需要监听的事件 POLLIN，读
  uv__io_start(loop, &loop->async_io_watcher, POLLIN);
  // 用于主线程和子线程通信的 fd，管道的写端，子线程使用
  loop->async_wfd = pipefd[1];

  return 0;
}


int uv__async_fork(uv_loop_t* loop) {
  if (loop->async_io_watcher.fd == -1) /* never started */
    return 0;

  uv__async_stop(loop);

  return uv__async_start(loop);
}


void uv__async_stop(uv_loop_t* loop) {
  if (loop->async_io_watcher.fd == -1)
    return;

  if (loop->async_wfd != -1) {
    if (loop->async_wfd != loop->async_io_watcher.fd)
      uv__close(loop->async_wfd);
    loop->async_wfd = -1;
  }

  uv__io_stop(loop, &loop->async_io_watcher, POLLIN);
  uv__close(loop->async_io_watcher.fd);
  loop->async_io_watcher.fd = -1;
}
