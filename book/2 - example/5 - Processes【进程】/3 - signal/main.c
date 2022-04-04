#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <uv.h>

uv_loop_t* create_loop()
{
    uv_loop_t *loop = malloc(sizeof(uv_loop_t));
    if (loop) {
      uv_loop_init(loop);
    }
    return loop;
}

void signal_handler(uv_signal_t *handle, int signum)
{
    printf("Signal received: %d\n", signum);
    uv_signal_stop(handle);
}

// two signal handlers in one loop
void thread1_worker(void *userp)
{
    uv_loop_t *loop1 = create_loop();

    uv_signal_t sig1a, sig1b;
    uv_signal_init(loop1, &sig1a);
    uv_signal_start(&sig1a, signal_handler, SIGUSR1);

    uv_signal_init(loop1, &sig1b);
    uv_signal_start(&sig1b, signal_handler, SIGUSR1);

    uv_run(loop1, UV_RUN_DEFAULT);
}

// two signal handlers, each in its own loop
void thread2_worker(void *userp)
{
    uv_loop_t *loop2 = create_loop();
    uv_loop_t *loop3 = create_loop();

    uv_signal_t sig2;
    uv_signal_init(loop2, &sig2);
    uv_signal_start(&sig2, signal_handler, SIGUSR1);

    uv_signal_t sig3;
    uv_signal_init(loop3, &sig3);
    uv_signal_start(&sig3, signal_handler, SIGUSR1);

    while (uv_run(loop2, UV_RUN_NOWAIT) || uv_run(loop3, UV_RUN_NOWAIT)) {
    }
}

/*
    使用uv_signal_init初始化handle（uv_signal_t），然后将它与loop关联。为了使用
    handle监听特定的信号，使用uv_signal_start()函数。每一个handle只能与一个信号
    关联，后续的uv_signal_start会覆盖前面的关联。使用uv_signal_stop终止监听

    uv_run(loop, UV_RUN_NOWAIT)和uv_run(loop, UV_RUN_ONCE)非常像，因为它们都只
    处理一个事件。但是不同在于，UV_RUN_ONCE会在没有任务的时候阻塞，但是UV_RUN_NOWAIT
    会立刻返回。我们使用NOWAIT，这样才使得一个loop不会因为另外一个loop没有要处理的事
    件而挨饿

    当向进程发送SIGUSR1，你会发现signal_handler函数被激发了4次，每次都对应一个uv_signal_t。
    然后signal_handler调用uv_signal_stop终止了每一个uv_signal_t，最终程序退出。对每个handler
    函数来说，任务的分配很重要。一个使用了多个event-loop的服务器程序，只要简单地给每一个进程添
    加信号SIGINT监视器，就可以保证程序在中断退出前，数据能够安全地保存
*/
int main()
{
    printf("PID %d\n", getpid());

    uv_thread_t thread1, thread2;

    uv_thread_create(&thread1, thread1_worker, 0);
    uv_thread_create(&thread2, thread2_worker, 0);

    uv_thread_join(&thread1);
    uv_thread_join(&thread2);
    return 0;
}

/*
    运行结果：
    PID 52570
    之后终端输入 kill -s SIGUSR1 52570，结果

    PID 52570
    Signal received: 10
    Signal received: 10
    Signal received: 10
    Signal received: 10
*/