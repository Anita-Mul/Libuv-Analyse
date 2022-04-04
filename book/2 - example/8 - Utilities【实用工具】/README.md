## Timers 【uv_timer_t】—— 计时器句柄
 - [官方文档](https://libuv-docs-chinese.readthedocs.io/zh/latest/timer.html)
#### 部分 API
 - `uv_timer_set_repeat(uv_timer_t *timer, int64_t repeat);`
    如果上述函数是在定时器回调函数中调用的：
    >  - 如果定时器未设置为循环，这意味着定时器已经停止。需要先用uv_timer_start重新启动
    >  - 如果定时器被设置为循环，那么下一次超时的时间已经被规划好了，所以在切换到新的间隔之前，旧的间隔还会发挥一次作用
 - `int uv_timer_again(uv_timer_t *)`
    只适用于循环定时器，相当于停止定时器，然后把原先的timeout和repeat值都设置为之前的repeat值，启动定时器。如果当该函数调用时，定时器未启动，则调用失败（错误码为UV_EINVAL）并且返回－1





## handle【uv_handle_t】—— 基础句柄
 - [官方文档](https://libuv-docs-chinese.readthedocs.io/zh/latest/handle.html)
 - uv_handle_t 是所有libuv句柄类型的基类型
#### Event loop reference count
 - libuv事件循环（如果运行在默认模式）运行直至没有剩下活动的 和 被引用的句柄。在handle增加时，event-loop的引用计数加1，在handle停止时，引用计数减少1。用户能够通过对活动句柄解引用来强制循环提前退出， 例如调用 uv_unref() 在调用 uv_timer_start() 之后。
    > void uv_ref(uv_handle_t*);
    > void uv_unref(uv_handle_t*); 
#### ref-timer/main.c
 - 定时器只有发生错误或者有垃圾回收完才能停止他们。所以，定时器的存在会让程序一直执行下去，调用 uv_unref() 可以使得其它的监视器都退出之后，终止程序。
 - 编译执行
    `gcc main.c -L/usr/local/lib/ -luv -o main`





## Idler pattern【uv_idle_t】—— 空转句柄
 - [官方文档](https://libuv-docs-chinese.readthedocs.io/zh/latest/idle.html)
 - 空转的回调函数会在每一次的event-loop循环激发一次。
 - 作用：
   1. 可以向开发者发送应用程序的每日性能表现情况，以便于分析，或者是使用用户应用cpu时间来做SETI运算
   2. 空转程序还可以用于GUI应用。比如你在使用event-loop来下载文件，如果tcp连接未中断而且当前并没有其他的事件，则你的event-loop会阻塞，这也就意味着你的下载进度条会停滞，用户会面对一个无响应的程序。面对这种情况，空转监视器可以保持UI可操作
#### idle-compute/main.c
 - 编译执行
   `gcc main.c -L/usr/local/lib/ -luv -o main`






## Thread pool work scheduling —— 线程池工作调度
 - libuv 提供了一个线程池，可用于运行用户代码并在循环线程中获取通知
 - 线程池是全局的，并在所有事件循环之间共享
#### API
 - int uv_queue_work(uv_loop_t* loop, uv_work_t* req, uv_work_cb work_cb, uv_after_work_cb after_work_cb)
   初始化一个工作请求，该请求将在线程池中的线程中运行给定的work_cb。work_cb完成后，将在循环线程上调用after_work_cb
   可以使用 uv_cancel（） 取消此请求
#### example
 - 自定义 struct，然后用 uv_work_t.data 指向它。这样可以同时回收数据和 uv_work_t
   ```c
   struct ftp_baton {
      uv_work_t req;
      char *host;
      int port;
      char *username;
      char *password;
   }
   ```
   ```c
   ftp_baton *baton = (ftp_baton*) malloc(sizeof(ftp_baton));
   baton->req.data = (void*) baton;
   baton->host = strdup("my.webhost.com");
   baton->port = 21;
   // ...

   uv_queue_work(loop, &baton->req, ftp_session, ftp_cleanup);
   ```
   ```c
   // 在线程池上执行这个函数，req 是传递来的数据
   void ftp_session(uv_work_t *req) {
      ftp_baton *baton = (ftp_baton*) req->data;

      fprintf(stderr, "Connecting to %s\n", baton->host);
   }

   // 当线程池上的函数执行结束之后，调用这个
   void ftp_cleanup(uv_work_t *req) {
      ftp_baton *baton = (ftp_baton*) req->data;

      free(baton->host);
      // ...
      free(baton);
   }
   ```




## uv_poll_t —— 轮询句柄
 - [官方文档](https://libuv-docs-chinese.readthedocs.io/zh/latest/poll.html)




## Shared library handling - 共享库处理
- [官方文档](https://libuv-docs-chinese.readthedocs.io/zh/latest/dll.html)
#### plugin/plugin.h
 - 提供给插件作者的接口
   ```c
   #ifndef UVBOOK_PLUGIN_SYSTEM
   #define UVBOOK_PLUGIN_SYSTEM

   // Plugin authors should use this to register their plugins with mfp.
   void mfp_register(const char *name);

   #endif
   ```
 - 使用这个插件
   ```c
   #include "plugin.h"

   void initialize() {
      mfp_register("Hello World!");
   }
   ```
 - 运行
   ```c
   $ ./plugin libhello.dylib
   Loading libhello.dylib
   Registered plugin "Hello World!"
   ```




## uv_tty_t —— TTY句柄
 - TTY句柄代表对终端的一个流
#### tty/main.c
 - 编译运行
   `gcc main.c -L/usr/local/lib/ -luv -o main`
#### tty-gravity/main.c
 - 编译运行
   `gcc main.c -L/usr/local/lib/ -luv -o main`