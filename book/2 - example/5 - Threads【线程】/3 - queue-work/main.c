#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <uv.h>

#define FIB_UNTIL 25
uv_loop_t *loop;

long fib_(long t) {
    if (t == 0 || t == 1)
        return 1;
    else
        return fib_(t-1) + fib_(t-2);
}

// 将在不同的函数中运行
void fib(uv_work_t *req) {
    int n = *(int *) req->data;
    if (random() % 2)
        sleep(1);
    else
        sleep(3);
    long fib = fib_(n);
    fprintf(stderr, "%dth fibonacci is %lu\n", n, fib);
}

void after_fib(uv_work_t *req, int status) {
    fprintf(stderr, "Done calculating %dth fibonacci\n", *(int *) req->data);
}

/*
    我们将要执行fibonacci数列，并且睡眠一段时间，但是将阻塞和cpu占用时间长的任务分配
    到不同的线程，使得其不会阻塞event loop上的其他任务
*/
int main() {
    loop = uv_default_loop();

    int data[FIB_UNTIL];
    uv_work_t req[FIB_UNTIL];   // 子线程的参数
    int i;

    for (i = 0; i < FIB_UNTIL; i++) {
        data[i] = i;
        req[i].data = (void *) &data[i];
        uv_queue_work(loop, &req[i], fib, after_fib);
    }

    return uv_run(loop, UV_RUN_DEFAULT);
}
