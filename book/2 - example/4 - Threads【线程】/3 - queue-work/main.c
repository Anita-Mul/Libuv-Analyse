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
        // 可以通过void *data传递任何数据，使用它来完成线程之间的沟通任务
        req[i].data = (void *) &data[i];
        uv_queue_work(loop, &req[i], fib, after_fib);
    }

    return uv_run(loop, UV_RUN_DEFAULT);
}

/*
    执行结果
    0th fibonacci is 1
    2th fibonacci is 2
    3th fibonacci is 3
    Done calculating 0th fibonacci
    Done calculating 2th fibonacci
    Done calculating 3th fibonacci
    4th fibonacci is 5
    5th fibonacci is 8
    Done calculating 4th fibonacci
    Done calculating 5th fibonacci
    1th fibonacci is 1
    Done calculating 1th fibonacci
    8th fibonacci is 34
    Done calculating 8th fibonacci
    9th fibonacci is 55
    Done calculating 9th fibonacci
    6th fibonacci is 13
    Done calculating 6th fibonacci
    11th fibonacci is 144
    Done calculating 11th fibonacci
    7th fibonacci is 21
    Done calculating 7th fibonacci
    13th fibonacci is 377
    Done calculating 13th fibonacci
    14th fibonacci is 610
    Done calculating 14th fibonacci
    10th fibonacci is 89
    Done calculating 10th fibonacci
    12th fibonacci is 233
    Done calculating 12th fibonacci
    15th fibonacci is 987
    Done calculating 15th fibonacci
    16th fibonacci is 1597
    17th fibonacci is 2584
    Done calculating 16th fibonacci
    Done calculating 17th fibonacci
    18th fibonacci is 4181
    Done calculating 18th fibonacci
    20th fibonacci is 10946
    Done calculating 20th fibonacci
    22th fibonacci is 28657
    Done calculating 22th fibonacci
    23th fibonacci is 46368
    Done calculating 23th fibonacci
    19th fibonacci is 6765
    Done calculating 19th fibonacci
    21th fibonacci is 17711
    Done calculating 21th fibonacci
    24th fibonacci is 75025
    Done calculating 24th fibonacci
*/