## Threads
 - [官方文档](https://libuv-docs-chinese.readthedocs.io/zh/latest/threading.html#c.uv_thread_create)
 - libuv的线程模块是自成一体的。由操作系统调度，比如，其他的功能模块都需要依赖于event loop和回调的原则，但是线程并不是这样。它们是不受约束的，会在需要的时候阻塞，通过返回值产生信号错误
 - 只有一个主线程，主线程上只有一个event loop。不会有其他与主线程交互的线程了。（除非使用uv_async_send）
#### thread-create/main.c
 - 编译运行
   `gcc main.c -L/usr/local/lib/ -luv -o main`





## Synchronization Primitives
#### Mutexes
```c
UV_EXTERN int uv_mutex_init(uv_mutex_t* handle);
UV_EXTERN void uv_mutex_destroy(uv_mutex_t* handle);
UV_EXTERN void uv_mutex_lock(uv_mutex_t* handle);
UV_EXTERN int uv_mutex_trylock(uv_mutex_t* handle);
UV_EXTERN void uv_mutex_unlock(uv_mutex_t* handle);
```
#### Lock
 - 两个读者线程可以同时从共享区中读取数据。当读者以读模式占有读写锁时，写者不能再占有它。当写者以写模式占有这个锁时，其他的写者或者读者都不能占有它
###### locks/main.c - simple rwlocks
 - 编译运行
   `gcc main.c -L/usr/local/lib/ -luv -o main`
#### Others
 - libuv同样支持信号量，条件变量和屏障，而且API的使用方法和pthread中的用法很类似
 - `uv_once()`
    多个线程调用这个函数，参数可以使用一个uv_once_t和一个指向特定函数的指针，最终只有一个线程能够执行这个特定函数，并且这个特定函数只会被调用一次
    ```c
     /* Initialize guard */
    static uv_once_t once_only = UV_ONCE_INIT;

    int i = 0;

    void increment() {
        i++;
    }

    void thread1() {
        /* ... work */
        uv_once(once_only, increment);
    }

    void thread2() {
        /* ... work */
        uv_once(once_only, increment);
    }

    int main() {
        /* ... spawn threads */
    }
    ```
    当所有的线程执行完毕时，i == 1





## libuv work queue【Thread pool work scheduling】
 - [官方文档](https://libuv-docs-chinese.readthedocs.io/zh/latest/threadpool.html#c.uv_queue_work)
 - 使一个应用程序能够在不同的线程运行任务，当任务完成后，回调函数将会被触发
 - 当使用event-loop的时候，最重要的是不能让loop线程阻塞，或者是执行高cpu占用的程序，因为这样会使得loop慢下来，loop event的高效特性也不能得到很好地发挥
#### queue-work/main.c - lazy fibonacci
 - 编译运行
   `gcc main.c -L/usr/local/lib/ -luv -o main`
#### queue-cancel/main.c
 - `uv_cancel()`
    可以用来清理任务队列中的等待执行的任务





## communication【uv_async_t --- 异步句柄】
 - [官方文档](https://libuv-docs-chinese.readthedocs.io/zh/latest/async.html)
 - 异步句柄允许用户 "唤醒" 事件循环并且从另一个线程调用回调函数
#### progress/main.c
