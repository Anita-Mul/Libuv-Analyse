## Libuv 介绍
 - Libuv 是一个跨平台的的基于事件驱动的异步 io 库。但是他提供的功能不仅仅是 io，包括进程、线程、信号、定时器、进程间通信等

    ![image.png](https://p3-juejin.byteimg.com/tos-cn-i-k3u1fbpfcp/cf95c1c7fbad4e628a2360f9e3e31476~tplv-k3u1fbpfcp-watermark.image?)
## 句柄【Handle】
 - 句柄 表示能够在活动时执行特定操作的长期存在的对象
 - Libuv 中所有的句柄都需要初始化，初始化的时候会调用 `uv_xxx_init`，`xxx` 表示句柄的类型
 - Libuv 中句柄种类
     > -   [`uv_loop_t` --- 事件循环](https://libuv-docs-chinese.readthedocs.io/zh/latest/loop.html)
     > -   [`uv_handle_t` --- 基础句柄](https://libuv-docs-chinese.readthedocs.io/zh/latest/handle.html)
     > -   [`uv_req_t` --- 基础请求](https://libuv-docs-chinese.readthedocs.io/zh/latest/request.html)
     > -   [`uv_timer_t` --- 计时器句柄](https://libuv-docs-chinese.readthedocs.io/zh/latest/timer.html)
     > -   [`uv_prepare_t` --- 准备句柄](https://libuv-docs-chinese.readthedocs.io/zh/latest/prepare.html)
     > -   [`uv_check_t` --- 检查句柄](https://libuv-docs-chinese.readthedocs.io/zh/latest/check.html)
     > -   [`uv_idle_t` --- 空转句柄](https://libuv-docs-chinese.readthedocs.io/zh/latest/idle.html)
     > -   [`uv_async_t` --- 异步句柄](https://libuv-docs-chinese.readthedocs.io/zh/latest/async.html)
     > -   [`uv_poll_t` --- 轮询句柄](https://libuv-docs-chinese.readthedocs.io/zh/latest/poll.html)
     > -   [`uv_signal_t` --- 信号句柄](https://libuv-docs-chinese.readthedocs.io/zh/latest/signal.html)
     > -   [`uv_process_t` --- 进程句柄](https://libuv-docs-chinese.readthedocs.io/zh/latest/process.html)
     > -   [`uv_stream_t` --- 流句柄](https://libuv-docs-chinese.readthedocs.io/zh/latest/stream.html)
     > -   [`uv_tcp_t` --- TCP句柄](https://libuv-docs-chinese.readthedocs.io/zh/latest/tcp.html)
     > -   [`uv_pipe_t` --- 管道句柄](https://libuv-docs-chinese.readthedocs.io/zh/latest/pipe.html)
     > -   [`uv_tty_t` --- TTY句柄](https://libuv-docs-chinese.readthedocs.io/zh/latest/tty.html)
     > -   [`uv_udp_t` --- UDP句柄](https://libuv-docs-chinese.readthedocs.io/zh/latest/udp.html)
     > -   [`uv_fs_event_t` --- FS Event handle](https://libuv-docs-chinese.readthedocs.io/zh/latest/fs_event.html)
     > -   [`uv_fs_poll_t` --- FS Poll handle](https://libuv-docs-chinese.readthedocs.io/zh/latest/fs_poll.html)
#### uv_handle_t【基础句柄】
 - 所有句柄的抽象基类
 - 源码
     ```c
     struct uv_handle_s {
      UV_HANDLE_FIELDS
     };


     #define UV_HANDLE_FIELDS                                                           \
      void* data;                /* 公有数据，指向用户自定义数据，libuv不使用该成员 */    \                                                   \
      /* 只读数据 */                                                                    \
      uv_loop_t* loop;           /* 指向依赖的循环 */                                   \
      uv_handle_type type;       /* 句柄类型 */                                        \
      /* private */                                                                    \
      uv_close_cb close_cb;      /* 句柄关闭时的回调函数 */                             \
      void* handle_queue[2];     /* 句柄队列指针，分别指向上一个和下一个 */              \
      union {                                                                          \
        int fd;                                                                        \
        void* reserved[4];                                                             \
      } u;                                                                             \
      UV_HANDLE_PRIVATE_FIELDS                                                         \


      #define UV_HANDLE_PRIVATE_FIELDS                                                        \
      uv_handle_t* next_closing;      /* 指向下一个需要关闭的handle */                       \
      unsigned int flags;             /* 状态标记，比如引用、关闭、正在关闭、激活等状态 */     \
     ```
  - `uv_handle_t` 属性介绍
      > -   loop 为句柄所属的事件循环
      > -   type 为句柄类型，和`uv_##name##_t`强相关，对应的`uv_##name##_t`的type为`UV_##NAME##`。这种情况下可以通过`handle->type`很容易判断出来`uv_handle_t`子类的类型。
      > -   close_cb 句柄关闭时候执行的回调，入参参数为`uv_handle_t`的指针
      > -   handle_queue 句柄队列指针
      > -   u.fd 文件描述符
      > -   next_closing 下一个需要关闭的句柄，可以使得`loop->closing_handles`形成一个链表结构，从而方便删除
      ```c
        void uv__make_close_pending(uv_handle_t* handle) {
          assert(handle->flags & UV_HANDLE_CLOSING);
          assert(!(handle->flags & UV_HANDLE_CLOSED));
          handle->next_closing = handle->loop->closing_handles;
          handle->loop->closing_handles = handle;
        }
      ```
     > - 在初始化时，会把`handle->next_closing`以及`loop->closing_handles `全部置空。通过这段代码，可以清晰的看到在关闭句柄的时候，只需要判断`handle->next_closing`是否为null就可以得知所有句柄是否已经全部关闭。
     > - flags 是对handle状态的标记。
 #### uv_##name##_t 的实现
 在这里我们以`uv_poll_s`结构体为例，来解读一下libuv如何使用宏实现的类似继承的操作：
```c
struct uv_poll_s {
  UV_HANDLE_FIELDS
  uv_poll_cb poll_cb;
  UV_POLL_PRIVATE_FIELDS
};
```
在`uv_poll_s`数据结构中，第一个使用的宏便是`UV_HANDLE_FIELDS`宏，其次才为`uv_poll_s`的私有宏。所有的`uv_##name##_t`均包含了`uv_handle_t`的结构体变量，所以任何的`uv_##name##_t`都可以转换为`uv_handle_t`。
 #### handle 的基本操作
  - `uv__handle_init` 
      初始化 handle 的类型，设置 REF 标记，插入 handle 队列
      ```c
      #define uv__handle_init(loop_, h, type_)                                    \
      do {                                                                        \
        (h)->loop = (loop_);                                                      \
        (h)->type = (type_);                                                      \
        (h)->flags = UV_HANDLE_REF;  /* Ref the loop when active. */              \
        /*所有的 handle 都是由 loop -> handle_queue 来同一管理的*/
        QUEUE_INSERT_TAIL(&(loop_)->handle_queue, &(h)->handle_queue);            \
        uv__handle_platform_init(h);                                              \
      }                                                                           \
      while (0)


      #if defined(_WIN32)
      # define uv__handle_platform_init(h) ((h)->u.fd = -1)
      #else
      # define uv__handle_platform_init(h) ((h)->next_closing = NULL)
      #endif
      ```
  - `uv__handle_start`
      设置标记 handle 为 ACTIVE，如果设置了 REF 标记，则 active handle 的个数加一，active handle 数会影响事件循环的退出
      ```c
      #define uv__handle_start(h)                                                   \
      do {                                                                        \
        if (((h)->flags & UV_HANDLE_ACTIVE) != 0) break;                          \
        (h)->flags |= UV_HANDLE_ACTIVE;                                           \
        if (((h)->flags & UV_HANDLE_REF) != 0) uv__active_handle_add(h);          \
      }                                                                           \
      while (0)
      
      
      #define uv__active_handle_add(h)                                              \
      do {                                                                        \
        (h)->loop->active_handles++;                                              \
      }                                                                           \
      while (0)
      ```
 - `uv__handle_stop`
     uv__handle_stop 和 uv__handle_start 相反
     ```c
     #define uv__handle_stop(h)                                                    \
      do {                                                                        \
        if (((h)->flags & UV_HANDLE_ACTIVE) == 0) break;                          \
        (h)->flags &= ~UV_HANDLE_ACTIVE;                                          \
        if (((h)->flags & UV_HANDLE_REF) != 0) uv__active_handle_rm(h);           \
      }                                                                           \
      while (0)
     ```
 - `uv__handle_ref`
     uv__handle_ref 标记 handle 为 REF 状态，如果 handle 是 ACTIVE 状态，则 active handle 数加一
     ```c
     #define uv__handle_ref(h)                                                     \
      do {        
        /* 如果已经是引用状态，返回 */                                                                \
        if (((h)->flags & UV_HANDLE_REF) != 0) break;                             \
        /* 设为引用状态 */
        (h)->flags |= UV_HANDLE_REF;                                              \
        /* 正在关闭，直接返回 */
        if (((h)->flags & UV_HANDLE_CLOSING) != 0) break;                         \
        /* 激活状态下，将循环的active_handles加一 */
        if (((h)->flags & UV_HANDLE_ACTIVE) != 0) uv__active_handle_add(h);       \
      }                                                                           \
      while (0)
     ```
 - `uv__handle_unref`
     uv__handle_unref 去掉 handle 的 REF 状态，如果 handle 是 ACTIVE 状态，则 active handle 数减一
     ```c
     #define uv__handle_unref(h)                                                   \
      do {                                                                        \
        if (((h)->flags & UV_HANDLE_REF) == 0) break;                             \
        /* 去掉UV__HANDLE_REF标记 */
        (h)->flags &= ~UV_HANDLE_REF;                                             \
        if (((h)->flags & UV_HANDLE_CLOSING) != 0) break;                         \
        if (((h)->flags & UV_HANDLE_ACTIVE) != 0) uv__active_handle_rm(h);        \
      }                                                                           \
      while (0)
     ```
 - libuv 中 handle 有 REF 和 ACTIVE 两个状态。当一个 handle 调用 xxx_init 函数的时候， 他首先被打上 REF 标记，并且插入 loop->handle 队列。当 handle 调用 xxx_start 函 数的时候，他首先被打上 ACTIVE 标记，并且记录 active handle 的个数加一。只有 ACTIVE 状态的 handle 才会影响事件循环的退出  
 ## 请求【request】
  - 请求代表着（通常是）短期的操作。 这些操作可以通过一个句柄执行： 写请求用于在句柄上写数据；或是独立不需要句柄的：getaddrinfo 请求 不需要句柄，它们直接在循环上运行
 #### request 的基本操作
  - `uv__req_register`
      uv__req_register 记录请求（request）的个数加一
      ```c
      #define uv__req_register(loop, req)                                         \
      do {                                                                        \
        (loop)->active_reqs.count++;                                              \
      }                                                                           \
      while (0)
      ```
  - `uv__req_unregister`
      uv__req_unregister 记录请求（request）的个数减一
      ```c
      #define uv__req_unregister(loop, req)                                         \
      do {                                                                        \
        assert(uv__has_active_reqs(loop));                                        \
        (loop)->active_reqs.count--;                                              \
      }                                                                           \
      while (0)
      ```
  - `uv__req_init`
      初始化请求的类型，记录请求的个数
      ```c
      #define uv__req_init(loop, req, typ)                                        \
      do {                                                                        \
        UV_REQ_INIT(req, typ);                                                    \
        uv__req_register(loop, req);                                              \
      }                                                                           \
      while (0)
      
      
      #if defined(_WIN32)
      # define UV_REQ_INIT(req, typ)                                                \
        do {                                                                        \
          (req)->type = (typ);                                                      \
          (req)->u.io.overlapped.Internal = 0;  /* SET_REQ_SUCCESS() */             \
        }                                                                           \
        while (0)
      #else
      # define UV_REQ_INIT(req, typ)                                                \
        do {                                                                        \
          (req)->type = (typ);                                                      \
        }                                                                           \
        while (0)
      #endif
      ```
  [下图来自豆米的博客](https://zhuanlan.zhihu.com/p/86242398)
    ![1607609-63a6103f0fb911d6.webp](https://p1-juejin.byteimg.com/tos-cn-i-k3u1fbpfcp/f8b06ab8a3314232a1a63c3cd7e837bb~tplv-k3u1fbpfcp-watermark.image?)

 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 