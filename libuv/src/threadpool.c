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

#include "uv-common.h"

#if !defined(_WIN32)
# include "unix/internal.h"
#endif

#include <stdlib.h>

#define MAX_THREADPOOL_SIZE 1024

static uv_once_t once = UV_ONCE_INIT;
static uv_cond_t cond;
static uv_mutex_t mutex;
static unsigned int idle_threads;
static unsigned int slow_io_work_running;
static unsigned int nthreads;
static uv_thread_t* threads;
static uv_thread_t default_threads[4];
static QUEUE exit_message;
static QUEUE wq;
static QUEUE run_slow_work_message;
static QUEUE slow_io_pending_wq;

static unsigned int slow_work_thread_threshold(void) {
  return (nthreads + 1) / 2;
}

static void uv__cancelled(struct uv__work* w) {
  abort();
}

// 该线程池在用户提交了第一个任务的时候初始化，而不是系统启动的时候就初始化
static void worker(void* arg) {
  struct uv__work* w;
  QUEUE* q;
  int is_slow_work;

  // 线程启动成功，因为初始化线程的时候，等待所有线程都执行成功之后才会往下执行
  uv_sem_post((uv_sem_t*) arg);
  arg = NULL;

  // 加锁互斥访问任务队列
  uv_mutex_lock(&mutex);

  for (;;) {
    /*
        1 队列为空，
        2 队列不为空，但是队列里只有慢 IO 任务且正在执行的慢 IO 任务个数达到阈值
        则空闲线程加一，防止慢 IO 占用过多线程，导致其他快的任务无法得到执行
    */
    while (QUEUE_EMPTY(&wq) ||
           (QUEUE_HEAD(&wq) == &run_slow_work_message &&
            QUEUE_NEXT(&run_slow_work_message) == &wq &&
            slow_io_work_running >= slow_work_thread_threshold())) {
      idle_threads += 1;
      // 阻塞，等待队列中有任务的时候唤醒
      uv_cond_wait(&cond, &mutex);
      // 被唤醒，开始干活，空闲线程数减一
      idle_threads -= 1;
    }

    // 取出头结点，头指点可能是退出消息、慢 IO，一般请求
    q = QUEUE_HEAD(&wq);

    // 如果头结点是退出消息，则结束线程
    if (q == &exit_message) {
      // 唤醒其他因为没有任务正阻塞等待任务的线程，别的线程同样取出这个节点，结束线程...
      // 最后线程会全部结束
      uv_cond_signal(&cond);
      uv_mutex_unlock(&mutex);
      break;
    }

    // 移除节点
    QUEUE_REMOVE(q);
    // 重置前后指针
    QUEUE_INIT(q);  /* Signal uv_cancel() that the work req is executing. */

    is_slow_work = 0;

    /*
      如果当前节点等于慢 IO 节点，上面的 while 只判断了是不是只有慢 io 任务且达到
      阈值，这里是任务队列里肯定有非慢 io 任务，可能有慢 io，如果有慢 io 并且正在
      执行的个数达到阈值，则先不处理该慢 io 任务，继续判断是否还有非慢 io 任务可
      执行。
    */
    if (q == &run_slow_work_message) {
      // 遇到阈值，重新入队
      if (slow_io_work_running >= slow_work_thread_threshold()) {
        QUEUE_INSERT_TAIL(&wq, q);
        continue;
      }

      // 没有慢 IO 任务则继续
      if (QUEUE_EMPTY(&slow_io_pending_wq))
        continue;

      // 有慢 io，开始处理慢 IO 任务
      is_slow_work = 1;
      // 正在处理慢 IO 任务的个数累加，用于其他线程判断慢 IO 任务个数是否达到阈值
      slow_io_work_running++;

      // 摘下一个慢 io 任务
      q = QUEUE_HEAD(&slow_io_pending_wq);
      QUEUE_REMOVE(q);
      QUEUE_INIT(q);

      /*
        取出一个任务后，如果还有慢 IO 任务则把慢 IO 标记节点重新入队，
        表示还有慢 IO 任务，因为上面把该标记节点出队了
      */
      if (!QUEUE_EMPTY(&slow_io_pending_wq)) {
        // 有空闲线程则唤醒他，因为还有任务处理
        QUEUE_INSERT_TAIL(&wq, &run_slow_work_message);
        if (idle_threads > 0)
          uv_cond_signal(&cond);
      }
    }

    // 不需要操作队列了，尽快释放锁
    uv_mutex_unlock(&mutex);

    // q 是慢 IO 或者一般任务
    w = QUEUE_DATA(q, struct uv__work, wq);
    // 执行业务的任务函数，该函数一般会阻塞
    w->work(w);

    // 准备操作 loop 的任务完成队列，加锁
    uv_mutex_lock(&w->loop->wq_mutex);
    // 置空说明指向完了，不能被取消了，见 cancel 逻辑
    w->work = NULL;  

    // 执行完任务,插入到 loop 的 wq 队列,在 uv__work_done 的时候会执行队列中节点的 done 函数
    QUEUE_INSERT_TAIL(&w->loop->wq, &w->wq);
    // 通知 loop 的 wq_async 节点
    uv_async_send(&w->loop->wq_async);
    uv_mutex_unlock(&w->loop->wq_mutex);

    // 为下一轮操作任务队列加锁
    uv_mutex_lock(&mutex);
    if (is_slow_work) {
      // 执行完慢 IO 任务，记录正在执行的慢 IO 个数变量减 1，上面加锁保证了互斥访问这个变量
      slow_io_work_running--;
    }
  }
}

// 把任务插入队列等待线程处理
static void post(QUEUE* q, enum uv__work_kind kind) {
  // 加锁访问任务队列，因为这个队列是线程池共享的
  uv_mutex_lock(&mutex);

  // 类型是慢 IO
  if (kind == UV__WORK_SLOW_IO) {
    /* 
      插入慢 IO 对应的队列，llibuv 这个版本把任务分为几种类型，
      对于慢 io 类型的任务，libuv 是往任务队列里面插入一个特殊的节点
      run_slow_work_message，然后用 slow_io_pending_wq 维护了一个慢 io 任务的队列，
      当处理到 run_slow_work_message 这个节点的时候，libuv 会从 slow_io_pending_wq
      队列里逐个取出任务节点来执行。
    */
    QUEUE_INSERT_TAIL(&slow_io_pending_wq, q);
    /*
      有慢 IO 任务的时候，需要给主队列 wq 插入一个消息节点 run_slow_work_message,
      说明有慢 IO 任务，所以如果 run_slow_work_message 是空，说明还没有插入主队列。
      需要进行 q = &run_slow_work_message;赋值，然后把 run_slow_work_message 插入
      主队列。如果 run_slow_work_message 非空，说明已经插入线程池的任务队列了。
      解锁然后直接返回。
    */
    if (!QUEUE_EMPTY(&run_slow_work_message)) {
      /* Running slow I/O tasks is already scheduled => Nothing to do here.
         The worker that runs said other task will schedule this one as well. */
      uv_mutex_unlock(&mutex);
      return;
    }

    // 说明 run_slow_work_message 还没有插入队列，准备插入队列
    q = &run_slow_work_message;
  }

  // 把节点插入主队列，可能是慢 IO 消息节点（如果遍历这个队列发现是消息节点
  // 就可以执行 slow_io_pending_wq 队列里的任务了）或者一般任务（直接执行）
  QUEUE_INSERT_TAIL(&wq, q);

  // 有空闲线程则唤醒他，如果大家都在忙，则等到他忙完后就会重新判断是否还有新任务
  if (idle_threads > 0)
    uv_cond_signal(&cond);
  uv_mutex_unlock(&mutex);
}


#ifdef __MVS__
/* TODO(itodorov) - zos: revisit when Woz compiler is available. */
__attribute__((destructor))
#endif
void uv__threadpool_cleanup(void) {
  unsigned int i;

  if (nthreads == 0)
    return;

#ifndef __MVS__
  /* TODO(gabylb) - zos: revisit when Woz compiler is available. */
  post(&exit_message, UV__WORK_CPU);
#endif

  for (i = 0; i < nthreads; i++)
    if (uv_thread_join(threads + i))
      abort();

  if (threads != default_threads)
    uv__free(threads);

  uv_mutex_destroy(&mutex);
  uv_cond_destroy(&cond);

  threads = NULL;
  nthreads = 0;
}


static void init_threads(void) {
  unsigned int i;
  const char* val;
  uv_sem_t sem;

  // 默认线程数 4 个，static uv_thread_t default_threads[4];
  nthreads = ARRAY_SIZE(default_threads);
  // 判断用户是否在环境变量中设置了线程数，是的话取用户定义的
  val = getenv("UV_THREADPOOL_SIZE");
  if (val != NULL)
    nthreads = atoi(val);
  if (nthreads == 0)
    nthreads = 1;
  
  // #define MAX_THREADPOOL_SIZE 128 最多 128 个线程
  if (nthreads > MAX_THREADPOOL_SIZE)
    nthreads = MAX_THREADPOOL_SIZE;

  threads = default_threads;

  // 超过默认大小，重新分配内存
  if (nthreads > ARRAY_SIZE(default_threads)) {
    threads = uv__malloc(nthreads * sizeof(threads[0]));
    // 分配内存失败，回退到默认
    if (threads == NULL) {
      nthreads = ARRAY_SIZE(default_threads);
      threads = default_threads;
    }
  }

  // 初始化条件变量
  if (uv_cond_init(&cond))
    abort();

  // 初始化互斥变量
  if (uv_mutex_init(&mutex))
    abort();

  // 初始化三个队列
  QUEUE_INIT(&wq);
  QUEUE_INIT(&slow_io_pending_wq);
  QUEUE_INIT(&run_slow_work_message);

  // 初始化信号量变量，值为 0
  if (uv_sem_init(&sem, 0))
    abort();

  // 创建多个线程，工作函数为 worker，sem 为 worker 入参
  for (i = 0; i < nthreads; i++)
    if (uv_thread_create(threads + i, worker, &sem))
      abort();

  // 为 0 则阻塞，非 0 则减一，这里等待所有线程启动成功再往下执行
  for (i = 0; i < nthreads; i++)
    uv_sem_wait(&sem);

  uv_sem_destroy(&sem);
}


#ifndef _WIN32
static void reset_once(void) {
  uv_once_t child_once = UV_ONCE_INIT;
  memcpy(&once, &child_once, sizeof(child_once));
}
#endif


static void init_once(void) {
#ifndef _WIN32
  if (pthread_atfork(NULL, NULL, &reset_once))
    abort();
#endif
  init_threads();
}

// 给线程池提交一个任务
void uv__work_submit(uv_loop_t* loop,
                     struct uv__work* w,
                     enum uv__work_kind kind,
                     void (*work)(struct uv__work* w),
                     void (*done)(struct uv__work* w, int status)) {
  // 保证已经初始化线程，并只执行一次，所以线程池是在提交第一个任务的时候才被初始化
  uv_once(&once, init_once);
  w->loop = loop;
  w->work = work;
  w->done = done;
  // 调 post 往线程池的队列中加入一个新的任务。Libuv 把任务分为三种类型，慢
  // io（dns 解析）、快 io（文件操作）、cpu 密集型等，kind 就是说明任务的类型的
  post(&w->wq, kind);
}


static int uv__work_cancel(uv_loop_t* loop, uv_req_t* req, struct uv__work* w) {
  int cancelled;

  // 加锁，为了把节点移出队列
  uv_mutex_lock(&mutex);
  // 加锁，为了判断 w->wq 是否为空
  uv_mutex_lock(&w->loop->wq_mutex);

  /*
  w 在任务队列中并且任务函数 work 不为空，则可取消，
  在 work 函数中，如果执行完了任务，会把 work 置 NULL，
  所以一个任务可以取消的前提是他还没执行完。或者说还没执行过
  */
  cancelled = !QUEUE_EMPTY(&w->wq) && w->work != NULL;
  // 从任务队列中删除该节点
  if (cancelled)
    QUEUE_REMOVE(&w->wq);

  uv_mutex_unlock(&w->loop->wq_mutex);
  uv_mutex_unlock(&mutex);

  // 不能取消
  if (!cancelled)
    return UV_EBUSY;

  // 重置回调函数
  w->work = uv__cancelled;
  uv_mutex_lock(&loop->wq_mutex);
  /*
    插入 loop 的 wq 队列，对于取消的动作，libuv 认为是任务执行完了。
    所以插入已完成的队列，不过他的回调是 uv__cancelled 函数，
    而不是用户设置的回调
  */
  QUEUE_INSERT_TAIL(&loop->wq, &w->wq);
  // 通知主线程有任务完成
  uv_async_send(&loop->wq_async);
  uv_mutex_unlock(&loop->wq_mutex);

  return 0;
}


void uv__work_done(uv_async_t* handle) {
  struct uv__work* w;
  uv_loop_t* loop;
  QUEUE* q;
  QUEUE wq;
  int err;

  // 通过结构体字段获得结构体首地址
  loop = container_of(handle, uv_loop_t, wq_async);
  // 准备处理队列，加锁
  uv_mutex_lock(&loop->wq_mutex);
  // 把 loop->wq 队列的节点全部移到 wp 变量中，这样一来可以尽快释放锁
  QUEUE_MOVE(&loop->wq, &wq);
  // 不需要使用了，解锁
  uv_mutex_unlock(&loop->wq_mutex);

  // wq 队列的节点来源是在线程的 worker 里插入
  while (!QUEUE_EMPTY(&wq)) {
    q = QUEUE_HEAD(&wq);
    QUEUE_REMOVE(q);

    w = container_of(q, struct uv__work, wq);
    err = (w->work == uv__cancelled) ? UV_ECANCELED : 0;
    // 执行回调
    w->done(w, err);
  }
}


static void uv__queue_work(struct uv__work* w) {
  uv_work_t* req = container_of(w, uv_work_t, work_req);

  req->work_cb(req);
}


static void uv__queue_done(struct uv__work* w, int err) {
  uv_work_t* req;

  req = container_of(w, uv_work_t, work_req);
  uv__req_unregister(req->loop, req);

  if (req->after_work_cb == NULL)
    return;

  req->after_work_cb(req, err);
}


int uv_queue_work(uv_loop_t* loop,
                  uv_work_t* req,
                  uv_work_cb work_cb,
                  uv_after_work_cb after_work_cb) {
  if (work_cb == NULL)
    return UV_EINVAL;

  uv__req_init(loop, req, UV_WORK);
  req->loop = loop;
  req->work_cb = work_cb;
  req->after_work_cb = after_work_cb;
  uv__work_submit(loop,
                  &req->work_req,
                  UV__WORK_CPU,
                  uv__queue_work,
                  uv__queue_done);
  return 0;
}


int uv_cancel(uv_req_t* req) {
  struct uv__work* wreq;
  uv_loop_t* loop;

  switch (req->type) {
  case UV_FS:
    loop =  ((uv_fs_t*) req)->loop;
    wreq = &((uv_fs_t*) req)->work_req;
    break;
  case UV_GETADDRINFO:
    loop =  ((uv_getaddrinfo_t*) req)->loop;
    wreq = &((uv_getaddrinfo_t*) req)->work_req;
    break;
  case UV_GETNAMEINFO:
    loop = ((uv_getnameinfo_t*) req)->loop;
    wreq = &((uv_getnameinfo_t*) req)->work_req;
    break;
  case UV_RANDOM:
    loop = ((uv_random_t*) req)->loop;
    wreq = &((uv_random_t*) req)->work_req;
    break;
  case UV_WORK:
    loop =  ((uv_work_t*) req)->loop;
    wreq = &((uv_work_t*) req)->work_req;
    break;
  default:
    return UV_EINVAL;
  }

  return uv__work_cancel(loop, req, wreq);
}
