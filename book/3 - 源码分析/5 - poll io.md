>  - 经过之前的学习，咱们已经学会了计时器模块、idle、prepare、check 模块。下面咱们继续学习 `uv_run` 中最后一个，也是最难的一个模块 —— `poll io`
>  - `poll io` 是 libuv 非常重要的一个阶段，文件 io、网络 io、信号处理等都在这 个阶段处理
 - `uv_run`
     ```c
     int uv_run(uv_loop_t* loop, uv_run_mode mode) {
     int timeout;
     int r;
     int ran_pending;

     r = uv__loop_alive(loop);
     if (!r)
       uv__update_time(loop);

     while (r != 0 && loop->stop_flag == 0) {
       uv__update_time(loop);
       uv__run_timers(loop);
       ran_pending = uv__run_pending(loop);
       uv__run_idle(loop);
       uv__run_prepare(loop);

       timeout = 0;
       if ((mode == UV_RUN_ONCE && !ran_pending) || mode == UV_RUN_DEFAULT)
         timeout = uv__backend_timeout(loop);

       // ———————————————————————————— 下面来讲解这一部分 ——————————————————————————————————
       uv__io_poll(loop, timeout);
       // —————————————————————————————————————————————————————————————————————————————————

       uv__metrics_update_idle_time(loop);
       uv__run_check(loop);
       uv__run_closing_handles(loop);

       if (mode == UV_RUN_ONCE) {
         uv__update_time(loop);
         uv__run_timers(loop);
       }

       r = uv__loop_alive(loop);
       if (mode == UV_RUN_ONCE || mode == UV_RUN_NOWAIT)
         break;
     }

     if (loop->stop_flag != 0)
       loop->stop_flag = 0;

     return r;
    }
     ```
## 源码解析
 - 在正式讲解 `uv__io_poll` 源码之前，首先让咱们来认识一个核心概念和数据结构 ———— io观察者
#### io 观察者
 - 定义
     ```c
     struct uv__io_s {
      /* 事件触发后的回调 */
      uv__io_cb cb;
      /* 用于插入队列 */
      void* pending_queue[2];
      void* watcher_queue[2];
      /* 保存本次感兴趣的事件，在插入 io 观察者队列时设置 */
      unsigned int pevents; 
      /* 保存当前感兴趣的事件 */
      unsigned int events;  
      int fd;
    };
     ```
 - io 观察者就是封装了事件和回调的结构体，然后插入到 loop 维护的 io 观察者队列，在 poll io 阶段，libuv 会根据 io 观察者描述的信息，往底层的事件驱动模块注册相应的信 息。当注册的事件触发的时候，io 观察者的回调就会被执行。
###### 基本操作
 - `uv__io_init`
    ```c
    // 初始化 io 观察者
    void uv__io_init(uv__io_t* w, uv__io_cb cb, int fd) {
        assert(cb != NULL);
        assert(fd >= -1);
        // 初始化队列，回调，需要监听的 fd
        QUEUE_INIT(&w->pending_queue);
        QUEUE_INIT(&w->watcher_queue);
        w->cb = cb;
        w->fd = fd;

        // 上次加入 epoll 时感兴趣的事件，在执行完 epoll 操作函数后设
        w->events = 0;
        // 当前感兴趣的事件，在再次执行 epoll 函数之前设置
        w->pevents = 0;

        #if defined(UV_HAVE_KQUEUE)
            w->rcount = 0;
            w->wcount = 0;
        #endif /* defined(UV_HAVE_KQUEUE) */
    }
    ```
 - `uv__io_start`
    ```c
    // 注册一个 io 观察到 libuv
    // uv__io_start 函数就是把一个 io 观察者插入到 libuv 的观察者队列中，并且在 watchers数组中保存一个映射关系
    void uv__io_start(uv_loop_t* loop, uv__io_t* w, unsigned int events) {
      assert(0 == (events & ~(POLLIN | POLLOUT | UV__POLLRDHUP | UV__POLLPRI)));
      assert(0 != events);
      assert(w->fd >= 0);
      assert(w->fd < INT_MAX);

      // 设置当前感兴趣的事件
      w->pevents |= events;
      // 可能需要扩容
      maybe_resize(loop, w->fd + 1);

      #if !defined(__sun)
        if (w->events == w->pevents)
          return;
      #endif

      // io 观察者没有挂载在其他地方则插入 libuv 的 io 观察者队列
      if (QUEUE_EMPTY(&w->watcher_queue))
        QUEUE_INSERT_TAIL(&loop->watcher_queue, &w->watcher_queue);

      // 保存映射关系
      if (loop->watchers[w->fd] == NULL) {
        loop->watchers[w->fd] = w;
        loop->nfds++;
      }
    }
    ```
 - `uv__io_stop`
    ```c
    // 撤销 io 观察者或者事件
    // uv__io_stop 修改 io 观察者感兴趣的事件，如果还有感兴趣的事件的话，io 观察者还
    // 会在队列里，否则移出
    void uv__io_stop(uv_loop_t* loop, uv__io_t* w, unsigned int events) {
      assert(0 == (events & ~(POLLIN | POLLOUT | UV__POLLRDHUP | UV__POLLPRI)));
      assert(0 != events);

      if (w->fd == -1)
        return;

      assert(w->fd >= 0);

      /* Happens when uv__io_stop() is called on a handle that was never started. */
      if ((unsigned) w->fd >= loop->nwatchers)
        return;

      // 清除之前注册的事件，保存在 pevents 里，表示当前感兴趣的事件
      w->pevents &= ~events;

      // 对所有事件都不感兴趣了
      if (w->pevents == 0) {
        // 移出 loop 的观察者队列
        QUEUE_REMOVE(&w->watcher_queue);

        // 重置
        QUEUE_INIT(&w->watcher_queue);
        w->events = 0;

        // 重置
        if (w == loop->watchers[w->fd]) {
          assert(loop->nfds > 0);
          loop->watchers[w->fd] = NULL;
          loop->nfds--;
        }
      }// 之前还没有插入 io 观察者队列，则插入，等到 poll io 时处理，否则不需要处理
      else if (QUEUE_EMPTY(&w->watcher_queue))
        QUEUE_INSERT_TAIL(&loop->watcher_queue, &w->watcher_queue);
    }
    ```
    

#### epoll 相关知识点
 - epoll是Linux下多路复用IO接口select/poll的增强版本，它能显著提高程序在大量并发连接中只有少量活跃的情况下的系统CPU利用率，因为它会复用文件描述符集合来传递结果而不用迫使开发者每次等待事件之前都必须重新准备要被侦听的文件描述符集合，另一点原因就是获取事件的时候，它无须遍历整个被侦听的描述符集，只要遍历那些被内核IO事件异步唤醒而加入Ready队列的描述符集合就行了
###### 基础 API
 - `epoll_create`：创建一个epoll句柄，参数size用来告诉内核监听的文件描述符的个数，跟内存大小有关
     ```c
        #include <sys/epoll.h>
        int epoll_create(int size)     size：监听数目
     ```
 - `epoll_ctl`：控制某个epoll监控的文件描述符上的事件：注册、修改、删除
     ```c
        #include <sys/epoll.h>
        int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event)
            epfd：  为epoll_creat的句柄
              op：  表示动作，用3个宏来表示：
                    EPOLL_CTL_ADD (注册新的fd到epfd)，
                    EPOLL_CTL_MOD (修改已经注册的fd的监听事件)，
                    EPOLL_CTL_DEL (从epfd删除一个fd)；
            event： 告诉内核需要监听的事件

            // epoll_event 结构体
            struct epoll_event {
                __uint32_t events;    /* Epoll events */
                epoll_data_t data;    /* User data variable */
            };

            // epoll_data_t 结构体
            typedef union epoll_data {
                void *ptr;
                int fd;
                uint32_t u32;
                uint64_t u64;
            } epoll_data_t;

            // 监听的事件类型
            **EPOLLIN** ：  表示对应的文件描述符可以读（包括对端SOCKET正常关闭）
            **EPOLLOUT**：  表示对应的文件描述符可以写
                EPOLLPRI：  表示对应的文件描述符有紧急的数据可读（这里应该表示有带外数据到来）
            **EPOLLERR**：  表示对应的文件描述符发生错误
                EPOLLHUP：  表示对应的文件描述符被挂断；
                 EPOLLET：  将EPOLL设为边缘触发(Edge Triggered)模式，这是相对于水平触发(Level Triggered)而言的
            EPOLLONESHOT：  只监听一次事件，当监听完这次事件之后，如果还需要继续监听这个socket的话，需要再次把这个socket加入到EPOLL队列里
     ```
 - `epoll_wait`：等待所监控文件描述符上有事件的产生
     ```c
        #include <sys/epoll.h>
        int epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout)
                // 参数
               events： 用来存内核得到事件的集合，
            maxevents： 告之内核这个events有多大，这个maxevents的值不能大于创建epoll_create()时的size，
              timeout： 是超时时间
                   -1： 阻塞
                    0： 立即返回，非阻塞
                   >0： 指定毫秒

               返回值： 成功返回有多少文件描述符就绪，时间到时返回0，出错返回-1
     ```
#### uv__epoll_init
 - 主要是封装了 `epoll_create` 函数，并将返回的句柄赋值给 `loop->backend_fd`
     ```c
     int uv__epoll_init(uv_loop_t* loop) {
      int fd;
      fd = epoll_create1(O_CLOEXEC);

      /* epoll_create1() can fail either because it's not implemented (old kernel)
       * or because it doesn't understand the O_CLOEXEC flag.
       */
      if (fd == -1 && (errno == ENOSYS || errno == EINVAL)) {
        fd = epoll_create(256);

        if (fd != -1)
          uv__cloexec(fd, 1);
      }

      loop->backend_fd = fd;
      if (fd == -1)
        return UV__ERR(errno);

      return 0;
    }
    ```
#### uv__io_poll
###### 第一部分
 - 遍历io观察者队列，根据 uv__io_t 句柄初始化 epoll_event 结构体，之后调用 epoll_ctl 来修改文件描述符上的事件
    ```c
    // 没有 io 观察者，则直接返回 
    if (loop->nfds == 0) {
        assert(QUEUE_EMPTY(&loop->watcher_queue));
        return;
    }

    memset(&e, 0, sizeof(e));

    // 遍历io观察者队列
    while (!QUEUE_EMPTY(&loop->watcher_queue)) {
        // 取出当前头节点
        q = QUEUE_HEAD(&loop->watcher_queue);
        // 脱离队列
        QUEUE_REMOVE(q);
        // 初始化（重置）节点的前后指针
        QUEUE_INIT(q);

        // 通过结构体成功获取结构体首地址
        w = QUEUE_DATA(q, uv__io_t, watcher_queue);
        assert(w->pevents != 0);
        assert(w->fd >= 0);
        assert(w->fd < (int) loop->nwatchers);


        // ————————————————————  根据 uv__io_t 句柄初始化 epoll_event 结构体 ————————————————————
        // 设置当前感兴趣的事件
        e.events = w->pevents;
        // 这里使用了fd字段，事件触发后再通过fd从watchs字段里找到对应的io观察者，没有使用ptr指向io观察者的方案
        e.data.fd = w->fd;


        // ———————————————————— 判断事件类型 ————————————————————
        // w->events初始化的时候为0，则新增，否则修改
        if (w->events == 0)
          op = EPOLL_CTL_ADD;
        else
          op = EPOLL_CTL_MOD;

        // ———————————————————— 调用 epoll_ctl 来修改文件描述符上的事件 ————————————————————
        if (epoll_ctl(loop->backend_fd, op, w->fd, &e)) {
          if (errno != EEXIST)
            abort();

          assert(op == EPOLL_CTL_ADD);

          /* We've reactivated a file descriptor that's been watched before. */
          // 重新激活以前被监视过的文件描述符
          if (epoll_ctl(loop->backend_fd, EPOLL_CTL_MOD, w->fd, &e))
            abort();
        }

        // 记录当前加到epoll时的状态 
        w->events = w->pevents;
    }
    ```
###### 第二部分
 - 主要是调用 epoll wait 来监控文件描述符上事件的产生
  ```c
  // ———————————————————— 屏蔽SIGPROF信号，避免SIGPROF信号唤醒epoll_wait，但是却没有就绪的事件 ————————————————————
  /*
    如果设置了UV_LOOP_BLOCK_SIGPROF的话。libuv会做一个优化。如果调setitimer(ITIMER_PROF,…)设置了定时触发SIGPROF信号，
    则到期后，并且每隔一段时间后会触发SIGPROF信号，这里如果设置了UV_LOOP_BLOCK_SIGPROF就会屏蔽这个信号。否则会提前唤
    醒epoll_wait
    http://man7.org/linux/man-pages/man2/epoll_wait.2.html
  */
  sigmask = 0;
  if (loop->flags & UV_LOOP_BLOCK_SIGPROF) {
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGPROF);
    sigmask |= 1 << (SIGPROF - 1);
  }


  assert(timeout >= -1);
  base = loop->time;          // 循环开始的时间
  count = 48; 
  real_timeout = timeout;     // 超时时间

  if (uv__get_internal_fields(loop)->flags & UV_METRICS_IDLE_TIME) {
    reset_timeout = 1;
    user_timeout = timeout;
    timeout = 0;
  } else {
    reset_timeout = 0;
    user_timeout = 0;
  }

  no_epoll_pwait = uv__load_relaxed(&no_epoll_pwait_cached);
  no_epoll_wait = uv__load_relaxed(&no_epoll_wait_cached);

  for (;;) {
    if (timeout != 0)
      uv__metrics_set_provider_entry_time(loop);

    if (sizeof(int32_t) == sizeof(long) && timeout >= max_safe_timeout)
      timeout = max_safe_timeout;

    if (sigmask != 0 && no_epoll_pwait != 0)
      if (pthread_sigmask(SIG_BLOCK, &sigset, NULL))
        abort();

    // ———————————————————— 调用 epoll wait 来监控文件描述符上事件的产生 ————————————————————
    if (no_epoll_wait != 0 || (sigmask != 0 && no_epoll_pwait == 0)) {
      nfds = epoll_pwait(loop->backend_fd,
                         events,
                         ARRAY_SIZE(events),
                         timeout,
                         &sigset);
      if (nfds == -1 && errno == ENOSYS) {
        uv__store_relaxed(&no_epoll_pwait_cached, 1);
        no_epoll_pwait = 1;
      }
    } else {
      nfds = epoll_wait(loop->backend_fd,
                        events,
                        ARRAY_SIZE(events),
                        timeout);
      if (nfds == -1 && errno == ENOSYS) {
        uv__store_relaxed(&no_epoll_wait_cached, 1);
        no_epoll_wait = 1;
      }
    }

    if (sigmask != 0 && no_epoll_pwait != 0)
      if (pthread_sigmask(SIG_UNBLOCK, &sigset, NULL))
        abort();

    // ———————————————————— epoll 可能阻塞，这里需要更新事件循环的时间 ————————————————————
    /*
        在 epoll_wait 可能会引起主线程阻塞，具体要根据 libuv 当前的情况。所以 wait 返回后需要更
        新当前的时间，否则在使用的时候时间差会比较大。因为 libuv 会在每轮时间循环开始的时候缓存
        当前时间这个值。其他地方直接使用， 而不是每次都去获取
    */
    SAVE_ERRNO(uv__update_time(loop));
   ```
###### 第三部分
 - 这里开始处理 io 事件，执行 io 观察者里保存的回调。但是有一个特殊的地方 就是信号处理的 io 观察者需要单独判断。他是一个全局的 io 观察者，和一般动态申请和销毁的 io 观察者不一样，他是存在于 libuv 运行的整个生命周期。
   ```c
    if (nfds == 0) {
      assert(timeout != -1);

      if (reset_timeout != 0) {
        timeout = user_timeout;
        reset_timeout = 0;
      }

      if (timeout == -1)
        continue;

      if (timeout == 0)
        return;

      /* We may have been inside the system call for longer than |timeout|
       * milliseconds so we need to update the timestamp to avoid drift.
       */
      goto update_timeout;
    }

    if (nfds == -1) {
      if (errno == ENOSYS) {
        /* epoll_wait() or epoll_pwait() failed, try the other system call. */
        assert(no_epoll_wait == 0 || no_epoll_pwait == 0);
        continue;
      }

      if (errno != EINTR)
        abort();

      if (reset_timeout != 0) {
        timeout = user_timeout;
        reset_timeout = 0;
      }

      if (timeout == -1)
        continue;

      if (timeout == 0)
        return;

      /* Interrupted by a signal. Update timeout and poll again. */
      goto update_timeout;
    }

    have_signals = 0;
    nevents = 0;

    {
      /* Squelch a -Waddress-of-packed-member warning with gcc >= 9. */
      union {
        struct epoll_event* events;
        uv__io_t* watchers;
      } x;

      x.events = events;
      assert(loop->watchers != NULL);

      // 保存epoll_wait返回的一些数据，maybe_resize申请空间的时候+2了
      loop->watchers[loop->nwatchers] = x.watchers;
      loop->watchers[loop->nwatchers + 1] = (void*) (uintptr_t) nfds;
    }


    // ———————————————————— 处理 epoll 返回的结果 ————————————————————
    for (i = 0; i < nfds; i++) {
      // 触发的事件和文件描述符
      pe = events + i;
      fd = pe->data.fd;

      if (fd == -1)
        continue;

      assert(fd >= 0);
      assert((unsigned) fd < loop->nwatchers);

      // 根据fd获取io观察者
      w = loop->watchers[fd];

      // 会其他回调里被删除了，则从epoll中删除
      if (w == NULL) {
        epoll_ctl(loop->backend_fd, EPOLL_CTL_DEL, fd, pe);
        continue;
      }

  
      pe->events &= w->pevents | POLLERR | POLLHUP;

      if (pe->events == POLLERR || pe->events == POLLHUP)
        pe->events |=
          w->pevents & (POLLIN | POLLOUT | UV__POLLRDHUP | UV__POLLPRI);

      if (pe->events != 0) {
        // 用于信号处理的io观察者感兴趣的事件触发了，即有信号发生
        if (w == &loop->signal_io_watcher) {
          have_signals = 1;
        } else {
          uv__metrics_update_idle_time(loop);
          // 一般的io观察者指向回调
          w->cb(loop, w, pe->events);
        }

        nevents++;
      }
    }

    if (reset_timeout != 0) {
      timeout = user_timeout;
      reset_timeout = 0;
    }

    // 有信号发生，触发回调
    if (have_signals != 0) {
      uv__metrics_update_idle_time(loop);
      loop->signal_io_watcher.cb(loop, &loop->signal_io_watcher, POLLIN);
    }
   ```

















