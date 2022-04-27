> 在上一节中，咱们介绍了 `uv_run` 中的第一部分 —— 定时器，让咱们接着来看 `uv_run` 中的下一部分 —— prepare,check,idle
 - uv_run
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
       // 运行 idle handle
       uv__run_idle(loop);
       // 运行 prepare handle
       uv__run_prepare(loop);

       timeout = 0;
       if ((mode == UV_RUN_ONCE && !ran_pending) || mode == UV_RUN_DEFAULT)
         timeout = uv__backend_timeout(loop);

       uv__io_poll(loop, timeout);
       uv__metrics_update_idle_time(loop);
        
       // 运行 check handle
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
## [源码解析 —— idle](https://libuv-docs-chinese.readthedocs.io/zh/latest/idle.html)
- 空转句柄将在每次循环迭代时运行给定的回调函数一次， 在 [`uv_prepare_t`](https://libuv-docs-chinese.readthedocs.io/zh/latest/prepare.html#c.uv_prepare_t "uv_prepare_t") 句柄前一刻
- idle 阶段的任务属于 handle
#### 数据类型
- `uv_idle_t`
   > 空转句柄类型
   ```c
   struct uv_idle_t {
      // 句柄[handle]相关参数
      // uv_handle_t
      void* data;
      uv_loop_t* loop;
      uv_handle_type type;
      uv_close_cb close_cb;
      void* handle_queue[2];
      union {                                                                     
        int fd;                                                                   
        void* reserved[4];                                                        
      } u; 
      uv_handle_t* next_closing;
      unsigned int flags;

      // idle 相关参数
      uv_idle_cb idle_cb;                                                     
      void* queue[2];                                                         
   }
   ```
- void `(*uv_idle_cb)`([uv_idle_t](https://libuv-docs-chinese.readthedocs.io/zh/latest/idle.html#c.uv_idle_t "uv_idle_t")* *handle*)
   > 传递给 [`uv_idle_start()`](https://libuv-docs-chinese.readthedocs.io/zh/latest/idle.html#c.uv_idle_start "uv_idle_start") 的回调函数的类型定义
#### API
-   int `uv_idle_init`([uv_loop_t](https://libuv-docs-chinese.readthedocs.io/zh/latest/loop.html#c.uv_loop_t "uv_loop_t")* *loop*, [uv_idle_t](https://libuv-docs-chinese.readthedocs.io/zh/latest/idle.html#c.uv_idle_t "uv_idle_t")* *idle*)[](https://libuv-docs-chinese.readthedocs.io/zh/latest/idle.html#c.uv_idle_init "永久链接至目标")

    初始化句柄。

<!---->

-   int `uv_idle_start`([uv_idle_t](https://libuv-docs-chinese.readthedocs.io/zh/latest/idle.html#c.uv_idle_t "uv_idle_t")* *idle*, [uv_idle_cb](https://libuv-docs-chinese.readthedocs.io/zh/latest/idle.html#c.uv_idle_cb "uv_idle_cb") *cb*)[](https://libuv-docs-chinese.readthedocs.io/zh/latest/idle.html#c.uv_idle_start "永久链接至目标")

    以给定的回调函数开始句柄。

<!---->

-   int `uv_idle_stop`([uv_idle_t](https://libuv-docs-chinese.readthedocs.io/zh/latest/idle.html#c.uv_idle_t "uv_idle_t")* *idle*)[](https://libuv-docs-chinese.readthedocs.io/zh/latest/idle.html#c.uv_idle_stop "永久链接至目标")

    停止句柄，回调函数将不会再被调用。
#### example
 - 例：
    ```c
    #include <stdio.h>
    #include <stdlib.h>
    #include <uv.h>

    int64_t num = 0;

    void my_idle_cb(uv_idle_t* handle)
    {
        num++;
        if (num >= 10e6) {
            printf("idle stop, num = %ld\n", num);
            uv_idle_stop(handle);
        }
    }

    int main() 
    {
        uv_idle_t idler;
        
        // 初始化句柄
        uv_idle_init(uv_default_loop(), &idler);

        printf("idle start, num = %ld\n", num);
        // 以 my_idle_cb 开始句柄
        uv_idle_start(&idler, my_idle_cb);

        uv_run(uv_default_loop(), UV_RUN_DEFAULT);

        return 0;
    }
    ```
- 在每次的时间循环中调用 `my_idle_cb` 函数，直到 num 值达到 10e6 时停止。
- 如果直接在全局找 `uv_idle_init` 函数的时候，是找不到的。因为 libuv 将idle、prepare以及check相关的函数都通过C语言的`##`连接符统一用宏定义了，并且在编译器预处理的时候产生对应的函数代码。咱们在最后讲解这个宏定义。下面来看看 `prepare` 句柄的概念。
## [源码解析 —— prepare](https://libuv-docs-chinese.readthedocs.io/zh/latest/prepare.html)
- 准备句柄将在每次循环迭代时运行给定的回调函数一次， 在I/O轮询前一刻
- prepare 阶段的任务属于 handle
#### 数据类型
-   `uv_prepare_t`[](https://libuv-docs-chinese.readthedocs.io/zh/latest/prepare.html#c.uv_prepare_t "永久链接至目标")

    准备句柄类型。
       ```c
       struct uv_prepare_t {
          // 句柄[handle]相关参数
          // uv_handle_t
          void* data;
          uv_loop_t* loop;
          uv_handle_type type;
          uv_close_cb close_cb;
          void* handle_queue[2];
          union {                                                                     
            int fd;                                                                   
            void* reserved[4];                                                        
          } u; 
          uv_handle_t* next_closing;
          unsigned int flags;

          // idle 相关参数
          uv_prepare_cb prepare_cb;                                                    
          void* queue[2];                                                         
       }
       ```

<!---->

-   void `(*uv_prepare_cb)`([uv_prepare_t](https://libuv-docs-chinese.readthedocs.io/zh/latest/prepare.html#c.uv_prepare_t "uv_prepare_t")* *handle*)[](https://libuv-docs-chinese.readthedocs.io/zh/latest/prepare.html#c.uv_prepare_cb "永久链接至目标")

    传递给 [`uv_prepare_start()`](https://libuv-docs-chinese.readthedocs.io/zh/latest/prepare.html#c.uv_prepare_start "uv_prepare_start") 的回调函数的类型定义。
#### API
-   int `uv_prepare_init`([uv_loop_t](https://libuv-docs-chinese.readthedocs.io/zh/latest/loop.html#c.uv_loop_t "uv_loop_t")* *loop*, [uv_prepare_t](https://libuv-docs-chinese.readthedocs.io/zh/latest/prepare.html#c.uv_prepare_t "uv_prepare_t")* *prepare*)[](https://libuv-docs-chinese.readthedocs.io/zh/latest/prepare.html#c.uv_prepare_init "永久链接至目标")

    初始化句柄。

<!---->

-   int `uv_prepare_start`([uv_prepare_t](https://libuv-docs-chinese.readthedocs.io/zh/latest/prepare.html#c.uv_prepare_t "uv_prepare_t")* *prepare*, [uv_prepare_cb](https://libuv-docs-chinese.readthedocs.io/zh/latest/prepare.html#c.uv_prepare_cb "uv_prepare_cb") *cb*)[](https://libuv-docs-chinese.readthedocs.io/zh/latest/prepare.html#c.uv_prepare_start "永久链接至目标")

    以给定的回调函数开始句柄。

<!---->

-   int `uv_prepare_stop`([uv_prepare_t](https://libuv-docs-chinese.readthedocs.io/zh/latest/prepare.html#c.uv_prepare_t "uv_prepare_t")* *prepare*)[](https://libuv-docs-chinese.readthedocs.io/zh/latest/prepare.html#c.uv_prepare_stop "永久链接至目标")

    停止句柄，回调函数将不会再被调用。
#### example
 - 例
    ```c
    #include <stdio.h>
    #include <stdlib.h>
    #include <uv.h>

    int64_t num = 0;

    void my_idle_cb(uv_idle_t* handle)
    {
        num++;
        printf("idle callback\n");
        if (num >= 5) {
            printf("idle stop, num = %ld\n", num);
            uv_stop(uv_default_loop());
        }
    }

    void my_prep_cb(uv_prepare_t *handle) 
    {
        printf("prep callback\n\n");
    }

    int main() 
    {
        uv_idle_t idler;
        uv_prepare_t prep;

        uv_idle_init(uv_default_loop(), &idler);
        uv_idle_start(&idler, my_idle_cb);
        
        // 初始化句柄
        uv_prepare_init(uv_default_loop(), &prep);
        // 以 my_prep_cb 开始句柄
            

        uv_run(uv_default_loop(), UV_RUN_DEFAULT);

        return 0;
    }
    ```
- 执行结果
    ```
    idle callback
    prep callback         // prepare 在 idle 之后执行

    idle callback
    prep callback

    idle callback
    prep callback

    idle callback
    prep callback

    idle callback          
    idle stop, num = 5     // 停止的是下一次的循环，还要把这一次循环里的内容执行完毕
    prep callback
    ```
#### idle 和 prepare 的区别
 - 由上述执行结果来看，貌似 idle 和 prepare 没有区别，都是在事件循环的过程中执行一个函数。现在咱们来看一下 `uv_run` 中 `uv__backend_timeout` 它做了些什么
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
           // 运行 idle handle
           uv__run_idle(loop);
           // 运行 prepare handle
           uv__run_prepare(loop);

           timeout = 0;
           // ————————————————————————————————————————
           // 咱们下面介绍 uv__backend_timeout 这个函数
           if ((mode == UV_RUN_ONCE && !ran_pending) || mode == UV_RUN_DEFAULT)
             timeout = uv__backend_timeout(loop);
           // ————————————————————————————————————————
           uv__io_poll(loop, timeout);
           uv__metrics_update_idle_time(loop);

           // 运行 check handle
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
- `uv__backend_timeout`
    ```c
    int uv_backend_timeout(const uv_loop_t* loop) {
        // 下面几种情况下返回0，即不阻塞在epoll_wait 
        if (loop->stop_flag != 0)
          return 0;

        // 没有东西需要处理，则不需要阻塞poll io阶段
        if (!uv__has_active_handles(loop) && !uv__has_active_reqs(loop))
          return 0;

        // idle阶段有任务，不阻塞，尽快返回直接idle任务
        if (!QUEUE_EMPTY(&loop->idle_handles))
          return 0;

        if (loop->closing_handles)
          return 0;

        // 返回下一个最早过期的时间，即最早超时的节点
        return uv__next_timeout(loop);
    }
    ```
- 哦耶，原来是这样，idle 阶段有任务，不会阻塞事件循环。而 prepare 对事件循环的阻塞时常没有影响。
## [源码解析 —— check](https://libuv-docs-chinese.readthedocs.io/zh/latest/check.html)
- 检查句柄将在每次循环迭代时运行给定的回调函数一次， 在I/O轮询后一刻
- prepare 阶段的任务属于 handle
#### 数据类型
-   `uv_check_t`[](https://libuv-docs-chinese.readthedocs.io/zh/latest/check.html#c.uv_check_t "永久链接至目标")

    检查句柄类型。
       ```c
       struct uv_check_t {
          // 句柄[handle]相关参数
          // uv_handle_t
          void* data;
          uv_loop_t* loop;
          uv_handle_type type;
          uv_close_cb close_cb;
          void* handle_queue[2];
          union {                                                                     
            int fd;                                                                   
            void* reserved[4];                                                        
          } u; 
          uv_handle_t* next_closing;
          unsigned int flags;

          // idle 相关参数
          uv_check_cb check_cb;                                                    
          void* queue[2];                                                         
       }
       ```
<!---->

-   void `(*uv_check_cb)`([uv_check_t](https://libuv-docs-chinese.readthedocs.io/zh/latest/check.html#c.uv_check_t "uv_check_t")* *handle*)[](https://libuv-docs-chinese.readthedocs.io/zh/latest/check.html#c.uv_check_cb "永久链接至目标")

    传递给 [`uv_check_start()`](https://libuv-docs-chinese.readthedocs.io/zh/latest/check.html#c.uv_check_start "uv_check_start") 的回调函数的类型定义。
#### API
-   int `uv_check_init`([uv_loop_t](https://libuv-docs-chinese.readthedocs.io/zh/latest/loop.html#c.uv_loop_t "uv_loop_t")* *loop*, [uv_check_t](https://libuv-docs-chinese.readthedocs.io/zh/latest/check.html#c.uv_check_t "uv_check_t")* *check*)[](https://libuv-docs-chinese.readthedocs.io/zh/latest/check.html#c.uv_check_init "永久链接至目标")

    初始化句柄。

<!---->

-   int `uv_check_start`([uv_check_t](https://libuv-docs-chinese.readthedocs.io/zh/latest/check.html#c.uv_check_t "uv_check_t")* *check*, [uv_check_cb](https://libuv-docs-chinese.readthedocs.io/zh/latest/check.html#c.uv_check_cb "uv_check_cb") *cb*)[](https://libuv-docs-chinese.readthedocs.io/zh/latest/check.html#c.uv_check_start "永久链接至目标")

    以给定的回调函数开始句柄。

<!---->

-   int `uv_check_stop`([uv_check_t](https://libuv-docs-chinese.readthedocs.io/zh/latest/check.html#c.uv_check_t "uv_check_t")* *check*)[](https://libuv-docs-chinese.readthedocs.io/zh/latest/check.html#c.uv_check_stop "永久链接至目标")

    停止句柄，回调函数将不会再被调用。
#### example
- 例
    ```c
    #include <stdio.h>
    #include <stdlib.h>
    #include <uv.h>

    int64_t num = 0;

    void my_idle_cb(uv_idle_t* handle)
    {
        num++;
        printf("idle callback\n");
        if (num >= 5) {
            printf("idle stop, num = %ld\n", num);
            uv_stop(uv_default_loop());
        }
    }

    void my_prep_cb(uv_prepare_t *handle) 
    {
        printf("prep callback\n");
    }

    void my_check_cb(uv_check_t *handle) 
    {
        printf("check callback\n\n");
    }

    int main() 
    {
        uv_idle_t idler;
        uv_prepare_t prep;
        uv_check_t check;

        uv_idle_init(uv_default_loop(), &idler);
        uv_idle_start(&idler, my_idle_cb);

        uv_prepare_init(uv_default_loop(), &prep);
        uv_prepare_start(&prep, my_prep_cb);

        uv_check_init(uv_default_loop(), &check);
        uv_check_start(&check, my_check_cb);

        uv_run(uv_default_loop(), UV_RUN_DEFAULT);

        return 0;
    }
    ```
- 执行结果
    ```
    idle callback
    prep callback
    check callback

    idle callback
    prep callback
    check callback

    idle callback
    prep callback
    check callback

    idle callback
    prep callback
    check callback

    idle callback
    idle stop, num = 5
    prep callback
    check callback
    ```
## idle、prepare、check 相关的宏定义
- 下面咱们来看一下这个宏定义 
- `src\unix\loop-watcher.c 文件内容`
    ```c
    // 将代码中的##name或者name##或者##name##替换为idle/prepare/check，##type替换为IDLE/PREPARE/CHECK
    #define UV_LOOP_WATCHER_DEFINE(name, type)                                    \
      int uv_##name##_init(uv_loop_t* loop, uv_##name##_t* handle) {              \
        /* 初始化handle的类型，所属loop，设置UV_HANDLE_REF标志，并且把handle插入loop->handle_queue队列的队尾 */
        uv__handle_init(loop, (uv_handle_t*)handle, UV_##type);                   \
        handle->name##_cb = NULL;                                                 \
        return 0;                                                                 \
      }                                                                           \
                                                                                  \
      int uv_##name##_start(uv_##name##_t* handle, uv_##name##_cb cb) {           \
        /* 如果已经执行过start函数则直接返回 */
        if (uv__is_active(handle)) return 0;                                      \
        /* 回调函数不允许为空 */ 
        if (cb == NULL) return UV_EINVAL;                                         \
        /* 把handle插入loop中XXX_handles队列，loop有prepare，idle和check三个队列 */
        QUEUE_INSERT_HEAD(&handle->loop->name##_handles, &handle->queue);         \
        /* 指定回调函数，在事件循环迭代的时候被执行 */
        handle->name##_cb = cb;                                                   \
        /* 启动idle handle，设置UV_HANDLE_ACTIVE标记并且将loop中的handle的active计数加一，
           init的时候只是把handle挂载到loop，start的时候handle才处于激活态 */
        uv__handle_start(handle);                                                 \
        return 0;                                                                 \
      }                                                                           \
                                                                                  \
      int uv_##name##_stop(uv_##name##_t* handle) {                               \
        /* 如果xxx handle没有被启动则直接返回 */
        if (!uv__is_active(handle)) return 0;                                     \
        /* 把handle从loop中相应的队列【name##_handles】移除，但是还挂载到handle_queue中 */
        /* QUEUE_INSERT_TAIL(&(loop_)->handle_queue, &(h)->handle_queue);*/
        /* QUEUE_INSERT_HEAD(&handle->loop->name##_handles, &handle->queue); */
        QUEUE_REMOVE(&handle->queue);                                             \
        /* 清除UV_HANDLE_ACTIVE标记并且减去loop中handle的active计数 */
        uv__handle_stop(handle);                                                  \
        return 0;                                                                 \
      }                                                                           \

      /* 在每一轮循环中执行该函数，具体见uv_run */                                 \
      void uv__run_##name(uv_loop_t* loop) {                                      \
        uv_##name##_t* h;                                                         \
        QUEUE queue;                                                              \
        QUEUE* q;
        /* 把loop的XXX_handles队列中所有节点摘下来挂载到queue变量 */               \
        QUEUE_MOVE(&loop->name##_handles, &queue);                                \
        /* while循环遍历队列，执行每个节点里面的函数 */
        while (!QUEUE_EMPTY(&queue)) {                                            \
          /* 取下当前待处理的节点 */
          q = QUEUE_HEAD(&queue);                                                 \
          /* 取得该节点对应的整个结构体的基地址 */
          h = QUEUE_DATA(q, uv_##name##_t, queue);                                \
          /* 把该节点移出当前队列【name##_handles】，在handle_queue中仍然存在 */
          QUEUE_REMOVE(q);                                                        \
          /* 重新插入loop->idle_handles队列 */
          QUEUE_INSERT_TAIL(&loop->name##_handles, q);                            \
          /* 执行对应的回调函数 */
          h->name##_cb(h);                                                        \
        }                                                                         \
      }                                                                           \

      /* 关闭这个idle handle */                                                   \
      void uv__##name##_close(uv_##name##_t* handle) {                            \
        uv_##name##_stop(handle);                                                 \
      }

    UV_LOOP_WATCHER_DEFINE(prepare, PREPARE)
    UV_LOOP_WATCHER_DEFINE(check, CHECK)
    UV_LOOP_WATCHER_DEFINE(idle, IDLE)
    ```





































