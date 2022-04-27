> 通过之前的学习，咱们已经明白了在事件循环中的三个核心内容，分别是：
>
> [Libuv源码分析 —— 定时器](https://juejin.cn/post/7084040526248280094)
>
> [Libuv源码分析 —— idle、prepare、check](https://juejin.cn/post/7084114526681399327)
> 
> [Libuv源码分析 —— poll io](https://juejin.cn/post/7084207782668271623)
>
> 现在让咱们从头捋一遍事件循环到底完成了什么功能呢？
## uv_run 
-   int `uv_run`([uv_loop_t](https://libuv-docs-chinese.readthedocs.io/zh/latest/loop.html#c.uv_loop_t "uv_loop_t")* *loop*, [uv_run_mode](https://libuv-docs-chinese.readthedocs.io/zh/latest/loop.html#c.uv_run_mode "uv_run_mode") *mode*)[](https://libuv-docs-chinese.readthedocs.io/zh/latest/loop.html#c.uv_run "永久链接至目标")

-   这个函数运行事件循环。 它将依指定的模式而采取不同的行为：

    -   UV_RUN_DEFAULT：运行事件循环直到没有更多的活动的和被引用到的句柄或请求。 返回非零如果 [`uv_stop()`](https://libuv-docs-chinese.readthedocs.io/zh/latest/loop.html#c.uv_stop "uv_stop") 被调用且仍有活动的句柄或请求。 在所有其他情况下返回零。
    -   UV_RUN_ONCE：轮询I/O一次。 注意如若没有待处理的回调函数这个函数阻塞。 返回零当完成时（没有剩余的活动的句柄或请求）， 或者非零值如果期望更多回调函数时 （意味着你应该在未来某时再次运行这个事件循环）。
    -   UV_RUN_NOWAIT：轮询I/O一次但不会阻塞，如若没有待处理的回调函数时。 返回零当完成时（没有剩余的活动的句柄或请求）， 或者非零值如果期望更多回调函数时 （意味着你应该在未来某时再次运行这个事件循环）。
    
## uv_run 源码
 ```c
 int uv_run(uv_loop_t* loop, uv_run_mode mode) {
  int timeout;
  int r;
  int ran_pending;

  r = uv__loop_alive(loop);
  if (!r)
    uv__update_time(loop);

  while (r != 0 && loop->stop_flag == 0) {
    // 更新时间并开始倒计时 loop->time
    uv__update_time(loop);
    // 执行超时回调
    uv__run_timers(loop);
    // 执行 pending 回调，ran_pending 代表 pending 队列是否为空，即没有节点可以执行
    ran_pending = uv__run_pending(loop);
    // 运行idle handle
    uv__run_idle(loop);
    // 运行prepare handle
    uv__run_prepare(loop);

    timeout = 0;
    // UV_RUN_ONCE 并且有 pending 节点的时候，会阻塞式 poll io，默认模式也是
    if ((mode == UV_RUN_ONCE && !ran_pending) || mode == UV_RUN_DEFAULT)
      timeout = uv__backend_timeout(loop);

    // 计算要阻塞的时间，开始阻塞
    // poll io timeout 是 epoll_wait 的超时时间
    uv__io_poll(loop, timeout);

    uv__metrics_update_idle_time(loop);

    // 程序执行到这里表示被唤醒了，被唤醒的原因可能是I/O可读可写、或者超时了，检查handle是否可以操作
    uv__run_check(loop);
    // 看看是否有close的handle
    uv__run_closing_handles(loop);

    // 单次模式 
    // 还有一次执行超时回调的机会
    if (mode == UV_RUN_ONCE) {
      uv__update_time(loop);
      uv__run_timers(loop);
    }

    // handle保活处理
    r = uv__loop_alive(loop);
    if (mode == UV_RUN_ONCE || mode == UV_RUN_NOWAIT)
      break;
  }
  
  // 修改停止标记，退出循环
  if (loop->stop_flag != 0)
    loop->stop_flag = 0;

  return r;
}
```
## 源码分析
#### uv__loop_alive
- 判断事件循环是否可以继续执行下去
    ```c
    static int uv__loop_alive(const uv_loop_t* loop) {
      return uv__has_active_handles(loop) ||
             uv__has_active_reqs(loop) ||
             !QUEUE_EMPTY(&loop->pending_queue) ||
             loop->closing_handles != NULL;
    }

    #define uv__has_active_handles(loop) ((loop)->active_handles > 0)
    #define uv__has_active_reqs(loop) ((loop)->active_reqs.count > 0)
    ```
#### uv__update_time
- 更新当前时间
    ```c
    UV_UNUSED(static void uv__update_time(uv_loop_t* loop)) {
      /* Use a fast time source if available.  We only need millisecond precision.
       */
      loop->time = uv__hrtime(UV_CLOCK_FAST) / 1000000;
    }
    ```
#### uv__run_pending
 - 执行 `loop->pending_queue` 队列中的回调函数
    ```c
    static int uv__run_pending(uv_loop_t* loop) {
      QUEUE* q;
      QUEUE pq;
      uv__io_t* w;

      if (QUEUE_EMPTY(&loop->pending_queue))
        return 0;

      QUEUE_MOVE(&loop->pending_queue, &pq);

      while (!QUEUE_EMPTY(&pq)) {
        q = QUEUE_HEAD(&pq);
        QUEUE_REMOVE(q);
        QUEUE_INIT(q);
        w = QUEUE_DATA(q, uv__io_t, pending_queue);
        w->cb(loop, w, POLLOUT);
      }

      return 1;
    }
    ```
#### uv__run_closing_handles
 - close 是 libuv 每轮事件循环中最后的一个阶段。我们看看怎么使用。我们知 道对于一个 handle，他的使用一般是 init，start，stop。但是如果我们在 stop 一个 handle 之后，还有些事情需要处理怎么办？这时候就可以使用 close 阶 段。close 阶段可以用来关闭一个 handle，并且执行一个回调。比如用于释 放动态申请的内存。close 阶段的任务由 uv_close 产生
 - 例如在停止 poll 的时候会调用 uv_close
    ```c
    int uv_fs_poll_stop(uv_fs_poll_t* handle) {
      struct poll_ctx* ctx;

      if (!uv_is_active((uv_handle_t*)handle))
        return 0;

      ctx = handle->poll_ctx;
      assert(ctx != NULL);
      assert(ctx->parent_handle == handle);

      if (uv_is_active((uv_handle_t*)&ctx->timer_handle))
        // 当结束监听的时候，他需要释放掉这块内存，通过回调函数传递释放内存的函数
        uv_close((uv_handle_t*)&ctx->timer_handle, timer_close_cb);

      uv__handle_stop(handle);

      return 0;
    }
    
    // 释放上下文结构体的内存 
    static void timer_close_cb(uv_handle_t* handle) { 
        uv__free(container_of(handle, struct poll_ctx, timer_handle)); 
    }
    ```
 - uv_close 
    ```c
    void uv_close(uv_handle_t* handle, uv_close_cb close_cb) {
      assert(!uv__is_closing(handle));

      // 正在关闭，但是还没执行回调等后置操作
      // 设置状态和回调
      handle->flags |= UV_HANDLE_CLOSING;
      handle->close_cb = close_cb;

      // 根据 handle 类型调对应的 close 函数，一般 就是 stop 这个 handle
      switch (handle->type) {
      case UV_NAMED_PIPE:
        uv__pipe_close((uv_pipe_t*)handle);
        break;

      case UV_TTY:
        uv__stream_close((uv_stream_t*)handle);
        break;

      case UV_TCP:
        uv__tcp_close((uv_tcp_t*)handle);
        break;

      case UV_UDP:
        uv__udp_close((uv_udp_t*)handle);
        break;

      case UV_PREPARE:
        uv__prepare_close((uv_prepare_t*)handle);
        break;

      case UV_CHECK:
        uv__check_close((uv_check_t*)handle);
        break;

      case UV_IDLE:
        uv__idle_close((uv_idle_t*)handle);
        break;

      case UV_ASYNC:
        uv__async_close((uv_async_t*)handle);
        break;

      case UV_TIMER:
        uv__timer_close((uv_timer_t*)handle);
        break;

      case UV_PROCESS:
        uv__process_close((uv_process_t*)handle);
        break;

      case UV_FS_EVENT:
        uv__fs_event_close((uv_fs_event_t*)handle);
        break;

      case UV_POLL:
        uv__poll_close((uv_poll_t*)handle);
        break;

      case UV_FS_POLL:
        uv__fs_poll_close((uv_fs_poll_t*)handle);
        /* Poll handles use file system requests, and one of them may still be
         * running. The poll code will call uv__make_close_pending() for us. */
        return;

      case UV_SIGNAL:
        uv__signal_close((uv_signal_t*) handle);
        break;

      default:
        assert(0);
      }
      
      // 执行 uv__make_close_pending 往 close 队列追加节点
      uv__make_close_pending(handle);
    }
    
    void uv__make_close_pending(uv_handle_t* handle) {
      assert(handle->flags & UV_HANDLE_CLOSING);
      assert(!(handle->flags & UV_HANDLE_CLOSED));
      handle->next_closing = handle->loop->closing_handles;
      handle->loop->closing_handles = handle;
    }
    ```
 - 在事件循环中需要执行的步骤
    ```c
    static void uv__run_closing_handles(uv_loop_t* loop) {
      uv_handle_t* p;
      uv_handle_t* q;

      p = loop->closing_handles;
      loop->closing_handles = NULL;

      // 关闭 closing_handles 中的所有句柄
      while (p) {
        q = p->next_closing;
        uv__finish_close(p);
        p = q;
      }
    }


    static void uv__finish_close(uv_handle_t* handle) {
      uv_signal_t* sh;

      // 句柄处于正在关闭状态，而不是已关闭状态
      assert(handle->flags & UV_HANDLE_CLOSING);
      assert(!(handle->flags & UV_HANDLE_CLOSED));
      // 修改句柄状态为已关闭
      handle->flags |= UV_HANDLE_CLOSED;

      switch (handle->type) {
        case UV_PREPARE:
        case UV_CHECK:
        case UV_IDLE:
        case UV_ASYNC:
        case UV_TIMER:
        case UV_PROCESS:
        case UV_FS_EVENT:
        case UV_FS_POLL:
        case UV_POLL:
          break;

        case UV_SIGNAL:
          sh = (uv_signal_t*) handle;
          if (sh->caught_signals > sh->dispatched_signals) {
            handle->flags ^= UV_HANDLE_CLOSED;
            uv__make_close_pending(handle);  /* Back into the queue. */
            return;
          }
          break;

        case UV_NAMED_PIPE:
        case UV_TCP:
        case UV_TTY:
          uv__stream_destroy((uv_stream_t*)handle);
          break;

        case UV_UDP:
          uv__udp_finish_close((uv_udp_t*)handle);
          break;

        default:
          assert(0);
          break;
      }

      // 句柄的引用计数减一
      uv__handle_unref(handle);
      // 从 Handle 中移除
      QUEUE_REMOVE(&handle->handle_queue);

      // 如果设置了关闭的回调函数，执行这个回调函数
      if (handle->close_cb) {
        handle->close_cb(handle);
      }
    }
    ```

#### uv__loop_alive
 - 检查时间循环在执行这么多任务之后的状态，是否可以继续执行下一轮循环
    ```c
    static int uv__loop_alive(const uv_loop_t* loop) {
      return uv__has_active_handles(loop) ||
             uv__has_active_reqs(loop) ||
             !QUEUE_EMPTY(&loop->pending_queue) ||
             loop->closing_handles != NULL;
    }
    ```
 
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    