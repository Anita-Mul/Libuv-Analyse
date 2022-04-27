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

#include "uv.h"
#include "internal.h"

#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef SA_RESTART
# define SA_RESTART 0
#endif

typedef struct {
  uv_signal_t* handle;
  int signum;
} uv__signal_msg_t;

RB_HEAD(uv__signal_tree_s, uv_signal_s);


static int uv__signal_unlock(void);
static int uv__signal_start(uv_signal_t* handle,
                            uv_signal_cb signal_cb,
                            int signum,
                            int oneshot);
static void uv__signal_event(uv_loop_t* loop, uv__io_t* w, unsigned int events);
static int uv__signal_compare(uv_signal_t* w1, uv_signal_t* w2);
static void uv__signal_stop(uv_signal_t* handle);
static void uv__signal_unregister_handler(int signum);


static uv_once_t uv__signal_global_init_guard = UV_ONCE_INIT;
static struct uv__signal_tree_s uv__signal_tree =
    RB_INITIALIZER(uv__signal_tree);
static int uv__signal_lock_pipefd[2] = { -1, -1 };

RB_GENERATE_STATIC(uv__signal_tree_s,
                   uv_signal_s, tree_entry,
                   uv__signal_compare)

static void uv__signal_global_reinit(void);

static void uv__signal_global_init(void) {
  if (uv__signal_lock_pipefd[0] == -1)
    // 注册 fork 之后，在子进程执行的函数
    if (pthread_atfork(NULL, NULL, &uv__signal_global_reinit))
      abort();

  uv__signal_global_reinit();
}


void uv__signal_cleanup(void) {
  /* We can only use signal-safe functions here.
   * That includes read/write and close, fortunately.
   * We do all of this directly here instead of resetting
   * uv__signal_global_init_guard because
   * uv__signal_global_once_init is only called from uv_loop_init
   * and this needs to function in existing loops.
   */
  if (uv__signal_lock_pipefd[0] != -1) {
    uv__close(uv__signal_lock_pipefd[0]);
    uv__signal_lock_pipefd[0] = -1;
  }

  if (uv__signal_lock_pipefd[1] != -1) {
    uv__close(uv__signal_lock_pipefd[1]);
    uv__signal_lock_pipefd[1] = -1;
  }
}


static void uv__signal_global_reinit(void) {
  // 清除原来的（如果有的话）
  uv__signal_cleanup();

  // 新建一个管道用于互斥控制
  if (uv__make_pipe(uv__signal_lock_pipefd, 0))
    abort();

  // 先往管道写入数据，即解锁。后续才能顺利 lock，unlock 配对使用
  if (uv__signal_unlock())
    abort();
}


void uv__signal_global_once_init(void) {
  uv_once(&uv__signal_global_init_guard, uv__signal_global_init);
}


static int uv__signal_lock(void) {
  int r;
  char data;

  do {
    r = read(uv__signal_lock_pipefd[0], &data, sizeof data);
  } while (r < 0 && errno == EINTR);

  return (r < 0) ? -1 : 0;
}


static int uv__signal_unlock(void) {
  int r;
  char data = 42;

  do {
    r = write(uv__signal_lock_pipefd[1], &data, sizeof data);
  } while (r < 0 && errno == EINTR);

  return (r < 0) ? -1 : 0;
}


static void uv__signal_block_and_lock(sigset_t* saved_sigmask) {
  sigset_t new_mask;

  if (sigfillset(&new_mask))
    abort();

  /* to shut up valgrind */
  sigemptyset(saved_sigmask);
  if (pthread_sigmask(SIG_SETMASK, &new_mask, saved_sigmask))
    abort();

  if (uv__signal_lock())
    abort();
}


static void uv__signal_unlock_and_unblock(sigset_t* saved_sigmask) {
  if (uv__signal_unlock())
    abort();

  if (pthread_sigmask(SIG_SETMASK, saved_sigmask, NULL))
    abort();
}


static uv_signal_t* uv__signal_first_handle(int signum) {
  /* This function must be called with the signal lock held. */
  uv_signal_t lookup;
  uv_signal_t* handle;

  lookup.signum = signum;
  lookup.flags = 0;
  lookup.loop = NULL;

  handle = RB_NFIND(uv__signal_tree_s, &uv__signal_tree, &lookup);

  if (handle != NULL && handle->signum == signum)
    return handle;

  return NULL;
}

// 该函数遍历红黑树，找到注册了该信号的handle，然后封装一个msg写入管道（即libuv的通信管道）。信号的通知处理就完成了
static void uv__signal_handler(int signum) {
  uv__signal_msg_t msg;
  uv_signal_t* handle;
  int saved_errno;

  saved_errno = errno;
  memset(&msg, 0, sizeof msg);

  // 保持上一个系统调用的错误码
  if (uv__signal_lock()) {
    errno = saved_errno;
    return;
  }

  /* 获取signal handle */
  for (handle = uv__signal_first_handle(signum);
       handle != NULL && handle->signum == signum;
       handle = RB_NEXT(uv__signal_tree_s, &uv__signal_tree, handle)) {
    int r;

    msg.signum = signum;
    msg.handle = handle;

    /* 往signal_pipefd管道写入数据，就是通知libuv，拿些signal handle需要处理信号，这是在事件循环中处理的 */
    do {
      r = write(handle->loop->signal_pipefd[1], &msg, sizeof msg);
    } while (r == -1 && errno == EINTR);

    assert(r == sizeof msg ||
           (r == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)));

    /* 记录该signal handle收到信号的次数 */
    if (r != -1)
      handle->caught_signals++;
  }

  uv__signal_unlock();
  errno = saved_errno;
}


static int uv__signal_register_handler(int signum, int oneshot) {
  struct sigaction sa;

  memset(&sa, 0, sizeof(sa));
  // 全置一，说明收到 signum 信号的时候，暂时屏蔽其他信号
  if (sigfillset(&sa.sa_mask))
    abort();

  // 所有信号都由该函数处理
  sa.sa_handler = uv__signal_handler;
  sa.sa_flags = SA_RESTART;

  // 设置了 oneshot，说明信号处理函数只执行一次，然后被恢复为系统的默认处理函数
  if (oneshot)
    sa.sa_flags |= SA_RESETHAND;

  // 注册
  if (sigaction(signum, &sa, NULL))
    return UV__ERR(errno);

  return 0;
}


static void uv__signal_unregister_handler(int signum) {
  /* When this function is called, the signal lock must be held. */
  struct sigaction sa;

  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = SIG_DFL;

  /* sigaction can only fail with EINVAL or EFAULT; an attempt to deregister a
   * signal implies that it was successfully registered earlier, so EINVAL
   * should never happen.
   */
  if (sigaction(signum, &sa, NULL))
    abort();
}

/*
  此代码主要的工作有两个
      1 申请一个管道，用于其他进程（libuv 进程或 fork 出来的进程）和 libuv 进程通信。
      然后往 libuv 的 io 观察者队列注册一个观察者，libuv 在 poll io 阶段会把观察者加到
      epoll 中。io 观察者里保存了管道读端的文件描述符 loop->signal_pipefd[0]和回调函
      数 uv__signal_event。uv__signal_event 是任意信号触发时的回调，他会继续根据触
      发的信号进行逻辑分发。
      2 初始化信号信号 handle 的字段。
*/
static int uv__signal_loop_once_init(uv_loop_t* loop) {
  int err;

  /* 如果已经初始化则返回 */
  if (loop->signal_pipefd[0] != -1)
    return 0;

  // 申请一个管道，用于其他进程和 libuv 主进程通信，并设置非阻塞标记
  err = uv__make_pipe(loop->signal_pipefd, UV_NONBLOCK_PIPE);
  if (err)
    return err;

  /* 置信号io观察者的处理函数和文件描述符，libuv在循环I/O的时候，
     如果发现管道读端loop->signal_pipefd[0]可读，则执行对应的回调函数uv__signal_event */
  uv__io_init(&loop->signal_io_watcher,
              uv__signal_event,
              loop->signal_pipefd[0]);

  /* 插入libuv的signal io观察者队列，当管道可读的时候，执行uv__signal_event */
  uv__io_start(loop, &loop->signal_io_watcher, POLLIN);

  return 0;
}


int uv__signal_loop_fork(uv_loop_t* loop) {
  uv__io_stop(loop, &loop->signal_io_watcher, POLLIN);
  uv__close(loop->signal_pipefd[0]);
  uv__close(loop->signal_pipefd[1]);
  loop->signal_pipefd[0] = -1;
  loop->signal_pipefd[1] = -1;
  return uv__signal_loop_once_init(loop);
}


void uv__signal_loop_cleanup(uv_loop_t* loop) {
  QUEUE* q;

  /* Stop all the signal watchers that are still attached to this loop. This
   * ensures that the (shared) signal tree doesn't contain any invalid entries
   * entries, and that signal handlers are removed when appropriate.
   * It's safe to use QUEUE_FOREACH here because the handles and the handle
   * queue are not modified by uv__signal_stop().
   */
  QUEUE_FOREACH(q, &loop->handle_queue) {
    uv_handle_t* handle = QUEUE_DATA(q, uv_handle_t, handle_queue);

    if (handle->type == UV_SIGNAL)
      uv__signal_stop((uv_signal_t*) handle);
  }

  if (loop->signal_pipefd[0] != -1) {
    uv__close(loop->signal_pipefd[0]);
    loop->signal_pipefd[0] = -1;
  }

  if (loop->signal_pipefd[1] != -1) {
    uv__close(loop->signal_pipefd[1]);
    loop->signal_pipefd[1] = -1;
  }
}

// 初始化信号句柄，将signal handle绑定到指定的loop事件循环中
/*
libuv申请一个管道，用于其他进程（libuv进程或fork出来的进程）和libuv进程通信。然后往libuv的io观察者队列注册一个观察者，
这其实就是观察这个管道是否可读，libuv在轮询I/O的阶段会把观察者加到epoll中。io观察者里保存了管道读端的文件描述符loop->signal_pipefd[0]
和回调函数uv__signal_event
*/
int uv_signal_init(uv_loop_t* loop, uv_signal_t* handle) {
  int err;

  /* 初始化loop，它只会被初始化一次 */
  err = uv__signal_loop_once_init(loop);
  if (err)
    return err;

  /* 初始化handle的类型，并且插入loop的handle队列，因为所有的handle都会被放到该队列管理 */
  uv__handle_init(loop, (uv_handle_t*) handle, UV_SIGNAL);
  handle->signum = 0;
  handle->caught_signals = 0;
  handle->dispatched_signals = 0;

  return 0;
}


void uv__signal_close(uv_signal_t* handle) {
  uv__signal_stop(handle);
}


int uv_signal_start(uv_signal_t* handle, uv_signal_cb signal_cb, int signum) {
  return uv__signal_start(handle, signal_cb, signum, 0);
}

// libuv只响应一次信号，在响应一次后恢复系统默认的信号处理
int uv_signal_start_oneshot(uv_signal_t* handle,
                            uv_signal_cb signal_cb,
                            int signum) {
  return uv__signal_start(handle, signal_cb, signum, 1);
}


static int uv__signal_start(uv_signal_t* handle,
                            uv_signal_cb signal_cb,
                            int signum,
                            int oneshot) {
  sigset_t saved_sigmask;
  int err;
  uv_signal_t* first_handle;

  assert(!uv__is_closing(handle));

  /* 如果用户提供的 signum == 0，则返回错误。 */ 
  if (signum == 0)
    return UV_EINVAL;

  /* 这个信号已经注册过了，重新设置回调处理函数就行。 */
  if (signum == handle->signum) {
    handle->signal_cb = signal_cb;
    return 0;
  }

  /* 如果信号处理程序已经处于活动状态，请先停止它。 */
  if (handle->signum != 0) {
    uv__signal_stop(handle);
  }

  /* 暂时屏蔽所有信号 */
  uv__signal_block_and_lock(&saved_sigmask);

  /*
      注册了该信号的第一个 handle，
      优先返回设置了 UV_SIGNAL_ONE_SHOT flag 的，
      见 compare 函数
  */
  first_handle = uv__signal_first_handle(signum);

  /* 
      1 之前没有注册过该信号的处理函数则直接设置

      2 之前设置过，但是是 one shot，但是现在需要
      设置的规则不是 one shot，需要修改。否则第
      二次不会不会触发。因为一个信号只能对应一
      个信号处理函数，所以，以规则宽的为准备，在回调
      里再根据 flags 判断是不是真的需要执行

      3 如果注册过信号和处理函数，则直接插入红黑树就行。
  */ 
  if (first_handle == NULL ||
      (!oneshot && (first_handle->flags & UV_SIGNAL_ONE_SHOT))) {
    // 注册信号和处理函数
    err = uv__signal_register_handler(signum, oneshot);
    if (err) {
      /* Registering the signal handler failed. Must be an invalid signal. */
      /* 注册信号失败 */
      uv__signal_unlock_and_unblock(&saved_sigmask);
      return err;
    }
  }

  // 记录感兴趣的信号
  handle->signum = signum;
  /* 设置UV_SIGNAL_ONE_SHOT标记，表示libuv只响应一次信号 */
  if (oneshot)
    handle->flags |= UV_SIGNAL_ONE_SHOT;

  /* 插入红黑树 */
  RB_INSERT(uv__signal_tree_s, &uv__signal_tree, handle);

  /* 接触屏蔽信号 */
  uv__signal_unlock_and_unblock(&saved_sigmask);

  // 信号触发时的业务层回调
  handle->signal_cb = signal_cb;
  /* 设置handle的标志UV_HANDLE_ACTIVE，表示处于活跃状态 */
  uv__handle_start(handle);

  return 0;
}

/*
  在信号通知完成后，事件循环中管道读取数据段有消息到达，此时事件循环将接收到消息，接下来在libuv的poll io阶段才做真正的处理。
  从uv__io_init()函数的处理过程得知，它把管道的读取端loop->signal_pipefd[0]看作是一个io观察者，在poll io阶段，epoll会检
  测到管道loop->signal_pipefd[0]是否可读，如果可读，然后会执行uv__signal_event()函数。在这个uv__signal_event()函数中，
  libuv将从管道读取刚才写入的一个个msg，从msg中取出对应的handle，然后执行里面保存的回调函数
*/
static void uv__signal_event(uv_loop_t* loop,
                             uv__io_t* w,
                             unsigned int events) {
  uv__signal_msg_t* msg;
  uv_signal_t* handle;
  char buf[sizeof(uv__signal_msg_t) * 32];
  size_t bytes, end, i;
  int r;

  bytes = 0;
  end = 0;

  /* 读取管道里的消息，处理所有的信号消息 */
  do {
    r = read(loop->signal_pipefd[0], buf + bytes, sizeof(buf) - bytes);

    if (r == -1 && errno == EINTR)
      continue;

    if (r == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      /* If there are bytes in the buffer already (which really is extremely
       * unlikely if possible at all) we can't exit the function here. We'll
       * spin until more bytes are read instead.
       */
      if (bytes > 0)
        continue;

      /* Otherwise, there was nothing there. */
      return;
    }

    /* Other errors really should never happen. */
    if (r == -1)
      abort();

    bytes += r;

    /* `end` is rounded down to a multiple of sizeof(uv__signal_msg_t). */
    end = (bytes / sizeof(uv__signal_msg_t)) * sizeof(uv__signal_msg_t);

    for (i = 0; i < end; i += sizeof(uv__signal_msg_t)) {
      msg = (uv__signal_msg_t*) (buf + i);
      handle = msg->handle;

      /* 如果收到的信号与预期的信号是一致的，则执行回调函数 */
      if (msg->signum == handle->signum) {
        assert(!(handle->flags & UV_HANDLE_CLOSING));
        /* signal 回调函数 */
        handle->signal_cb(handle, handle->signum);
      }

      /* 记录处理的信号个数 */
      handle->dispatched_signals++;

      /* 只响应一次，需要回复系统默认的处理函数 */
      if (handle->flags & UV_SIGNAL_ONE_SHOT)
        uv__signal_stop(handle);
    }

    bytes -= end;

    // 处理完了关闭
    if (bytes) {
      memmove(buf, buf + end, bytes);
      continue;
    }
  } while (end == sizeof buf);
}


static int uv__signal_compare(uv_signal_t* w1, uv_signal_t* w2) {
  int f1;
  int f2;
  /* Compare signums first so all watchers with the same signnum end up
   * adjacent.
   */
  if (w1->signum < w2->signum) return -1;
  if (w1->signum > w2->signum) return 1;

  /* Handlers without UV_SIGNAL_ONE_SHOT set will come first, so if the first
   * handler returned is a one-shot handler, the rest will be too.
   */
  f1 = w1->flags & UV_SIGNAL_ONE_SHOT;
  f2 = w2->flags & UV_SIGNAL_ONE_SHOT;
  if (f1 < f2) return -1;
  if (f1 > f2) return 1;

  /* Sort by loop pointer, so we can easily look up the first item after
   * { .signum = x, .loop = NULL }.
   */
  if (w1->loop < w2->loop) return -1;
  if (w1->loop > w2->loop) return 1;

  if (w1 < w2) return -1;
  if (w1 > w2) return 1;

  return 0;
}


int uv_signal_stop(uv_signal_t* handle) {
  assert(!uv__is_closing(handle));
  uv__signal_stop(handle);
  return 0;
}

// 停止signal handle，将信号句柄设置为非活跃状态，事件循环中不在对它进行轮询
static void uv__signal_stop(uv_signal_t* handle) {
  uv_signal_t* removed_handle;
  sigset_t saved_sigmask;
  uv_signal_t* first_handle;
  int rem_oneshot;
  int first_oneshot;
  int ret;

  /* If the watcher wasn't started, this is a no-op. */
  /* 如果没有启动观察程序，则该操作无效。 */
  if (handle->signum == 0)
    return;

  /* 暂时屏蔽所有信号 */
  uv__signal_block_and_lock(&saved_sigmask);

  /* 从红黑树取出信号节点 */
  removed_handle = RB_REMOVE(uv__signal_tree_s, &uv__signal_tree, handle);
  assert(removed_handle == handle);
  (void) removed_handle;

  /* Check if there are other active signal watchers observing this signal. If
   * not, unregister the signal handler.
   */
  /* 检查是否还有其他活动的信号监视程序正在观察此信号。如果没有了则注销信号处理程序。*/
  first_handle = uv__signal_first_handle(handle->signum);
  if (first_handle == NULL) {
    /* 注销信号，还是依赖系统的函数sigaction() */
    uv__signal_unregister_handler(handle->signum);
  } else {
    rem_oneshot = handle->flags & UV_SIGNAL_ONE_SHOT;
    first_oneshot = first_handle->flags & UV_SIGNAL_ONE_SHOT;
    if (first_oneshot && !rem_oneshot) {
      ret = uv__signal_register_handler(handle->signum, 1);
      assert(ret == 0);
      (void)ret;
    }
  }

  /* 解除屏蔽所有信号 */
  uv__signal_unlock_and_unblock(&saved_sigmask);

  handle->signum = 0;
  uv__handle_stop(handle);
}




struct uv_handle_s {
    void* data;                                 /* 公有数据，指向用户自定义数据，libuv不使用该成员 */
    uv_loop_t* loop;                            /* 所属事件循环 */
    uv_handle_type type;                        /* handle 类型 */
    uv_close_cb close_cb;                       /* 关闭 handle 时的回调 */
    void* handle_queue[2];                      /* 用于插入事件循环的 handle 队列 */
    union {                                                                     
      int fd;                                                                   
      void* reserved[4];                                                        
    } u; 
    uv_handle_t* next_closing;                  /* 指向下一个需要关闭的handle */
    unsigned int flags;                         /* 状态标记，比如引用、关闭、正在关闭、激活等状态 */
}