## Introduction
 - libuv强制使用异步的，事件驱动的编程风格。它的核心工作是提供一个event-loop，还有基于I/O和其它事件通知的回调函数。libuv还提供了一些核心工具，例如定时器，非阻塞的网络支持，异步文件系统访问，子进程等
 - 我们不需要关心I/O在后台是如何工作的，但是由于我们的计算机硬件的工作方式，线程是处理器最基本的执行单元，libuv和操作系统通常会运行后台/工作者线程, 或者采用非阻塞方式来轮流执行任务


## Error handling
 - 可以使用uv_strerror(int)和uv_err_name(int)分别获取const char *格式的错误信息和错误名字


## Handles and Requests
#### libuv watchers
```c
/* Handle types. */
typedef struct uv_loop_s uv_loop_t;
typedef struct uv_handle_s uv_handle_t;
typedef struct  uv_stream_t;
typedef struct uv_tcp_s uv_tcp_t;
typedef struct uv_udp_s uv_udp_t;
typedef struct uv_pipe_s uv_pipe_t;
typedef struct uv_tty_s uv_tty_t;
typedef struct uv_poll_s uv_poll_t;
typedef struct uv_timer_s uv_timer_t;
typedef struct uv_prepare_s uv_prepare_t;
typedef struct uv_check_s uv_check_t;
typedef struct uv_idle_s uv_idle_t;
typedef struct uv_async_s uv_async_t;
typedef struct uv_process_s uv_process_t;
typedef struct uv_fs_event_s uv_fs_event_t;
typedef struct uv_fs_poll_s uv_fs_poll_t;
typedef struct uv_signal_s uv_signal_t;

/* Request types. */
typedef struct uv_req_s uv_req_t;
typedef struct uv_getaddrinfo_s uv_getaddrinfo_t;
typedef struct uv_getnameinfo_s uv_getnameinfo_t;
typedef struct uv_shutdown_s uv_shutdown_t;
typedef struct uv_write_s uv_write_t;
typedef struct uv_connect_s uv_connect_t;
typedef struct uv_udp_send_s uv_udp_send_t;
typedef struct uv_fs_s uv_fs_t;
typedef struct uv_work_s uv_work_t;

/* None of the above. */
typedef struct uv_cpu_info_s uv_cpu_info_t;
typedef struct uv_interface_address_s uv_interface_address_t;
typedef struct uv_dirent_s uv_dirent_t;
```
 - handle代表了持久性对象。在异步的操作中，相应的handle上有许多与之关联的request。request是短暂性对象（通常只维持在一个回调函数的时间），通常对映着handle上的一个I/O操作。request用来在初始函数和回调函数之间，传递上下文。例如uv_udp_t代表了一个udp的socket，然而，对于每一个向socket的写入的完成后，都会向回调函数传递一个uv_udp_send_t
 - handle可以通过下面的函数设置：
    ```c
    uv_TYPE_init(uv_loop_t *, uv_TYPE_t *)
    ```
 - 回调函数是libuv所关注的事件发生后，所调用的函数。应用程序的特定逻辑会在回调函数中实现。例如，一个IO监视器的回调函数会接收到从文件读取到的数据，一个定时器的回调函数会在超时后被触发等等





## Idling
 - 下面有一个使用空转handle的例子。回调函数在每一个循环中都会被调用。在Utilities这部分会讲到一些空转handle的使用场景。现在让我们使用一个空转监视器，然后来观察它的生命周期，接着看uv_run调用是否会造成阻塞。当达到事先规定好的计数后，空转监视器会退出。因为uv_run已经找不到活着的事件监视器了，所以uv_run()也退出
#### idle-basic/main.c




## Storing context
 - 在基于回调函数的编程风格中，你可能会需要在调用处和回调函数之间，传递一些上下文等特定的应用信息。所有的handle和request都有一个data域，可以用来存储信息并传递。这是一个c语言库中很常见的模式。即使是uv_loop_t也有一个相似的data域