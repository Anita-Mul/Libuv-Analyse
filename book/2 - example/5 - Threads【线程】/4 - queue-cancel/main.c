#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <uv.h>

#define FIB_UNTIL 25
uv_loop_t *loop;
uv_work_t fib_reqs[FIB_UNTIL];

long fib_(long t) {
    if (t == 0 || t == 1)
        return 1;
    else
        return fib_(t-1) + fib_(t-2);
}

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
    // 对于已经成功取消的任务，他的回调函数的参数status会被设置为UV_ECANCELED
    if (status == UV_ECANCELED)
        fprintf(stderr, "Calculation of %d cancelled.\n", *(int *) req->data);
}

void signal_handler(uv_signal_t *req, int signum)
{
    printf("Signal received!\n");
    int i;
    for (i = 0; i < FIB_UNTIL; i++) {
        // 这个转换厉害
        uv_cancel((uv_req_t*) &fib_reqs[i]);
    }

    // 终止这个信号
    uv_signal_stop(req);
}

int main() {
    loop = uv_default_loop();

    int data[FIB_UNTIL];
    int i;
    for (i = 0; i < FIB_UNTIL; i++) {
        data[i] = i;
        fib_reqs[i].data = (void *) &data[i];
        uv_queue_work(loop, &fib_reqs[i], fib, after_fib);
    }

    uv_signal_t sig;
    uv_signal_init(loop, &sig);
    /*
        当用户通过Ctrl+C触发信号时，uv_cancel()回收任务队列中所有的任务，
        如果任务已经开始执行或者执行完毕，uv_cancel()返回0
    */
    uv_signal_start(&sig, signal_handler, SIGINT);

    return uv_run(loop, UV_RUN_DEFAULT);
}

/*
    执行结果
    0th fibonacci is 1
    2th fibonacci is 2
    3th fibonacci is 3
    4th fibonacci is 5
    5th fibonacci is 8
    1th fibonacci is 1
    8th fibonacci is 34
    9th fibonacci is 55
    6th fibonacci is 13
    11th fibonacci is 144
    7th fibonacci is 21
    10th fibonacci is 89
    13th fibonacci is 377
    14th fibonacci is 610
    12th fibonacci is 233
    15th fibonacci is 987
    16th fibonacci is 1597
    17th fibonacci is 2584
    18th fibonacci is 4181
    20th fibonacci is 10946
    22th fibonacci is 28657
    23th fibonacci is 46368
    19th fibonacci is 6765
    21th fibonacci is 17711
    24th fibonacci is 75025
    ^CSignal received!
*/