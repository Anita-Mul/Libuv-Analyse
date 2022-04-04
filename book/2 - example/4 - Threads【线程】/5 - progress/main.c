#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <uv.h>

uv_loop_t *loop;
uv_async_t async;

double percentage;

void fake_download(uv_work_t *req) {
    int size = *((int*) req->data);
    int downloaded = 0;
    while (downloaded < size) {
        percentage = downloaded*100.0/size;
        async.data = (void*) &percentage;
        // uv_async_send同样是非阻塞的，调用后会立即返回
        uv_async_send(&async);

        sleep(1);
        downloaded += (200+random())%1000; // can only download max 1000bytes/sec,
                                           // but at least a 200;
    }
}

void after(uv_work_t *req, int status) {
    fprintf(stderr, "Download complete\n");
    uv_close((uv_handle_t*) &async, NULL);
}

// 函数print_progress是标准的libuv模式，从监视器中抽取数据
void print_progress(uv_async_t *handle) {
    double percentage = *((double*) handle->data);
    fprintf(stderr, "Downloaded %.2f%%\n", percentage);
}

/*
    运行的线程之间能够相互发送消息
    一个下载管理程序向用户展示各个下载线程的进度

    演示了一个下载管理程序向用户展示各个下载线程的进度

    因为消息的发送是异步的,当uv_async_send在另外一个线程中被调用后，回调函数可能会立即被调用, 也可能在稍后的某个时刻被调用。
    libuv也有可能多次调用uv_async_send，但只调用了一次回调函数。唯一可以保证的是: 线程在调用uv_async_send之后回调函数可至少被调用一次。
    如果你没有未调用的uv_async_send, 那么回调函数也不会被调用。
    如果你调用了两次(以上)的uv_async_send, 而 libuv 暂时还没有机会运行回调函数, 则libuv可能会在多次调用uv_async_send后只
    调用一次回调函数，你的回调函数绝对不会在一次事件中被调用两次(或多次)
*/
int main() {
    loop = uv_default_loop();

    uv_work_t req;
    int size = 10240;
    req.data = (void*) &size;

    /*
        因为异步的线程通信是基于event-loop的，所以尽管所有的线程都可以是发送方，
        但是只有在event-loop上的线程可以是接收方（或者说event-loop是接收方）。
        在上述的代码中，当异步监视者接收到信号的时候，libuv会激发回调函数
        （print_progress）
    */
    uv_async_init(loop, &async, print_progress);
    uv_queue_work(loop, &req, fake_download, after);

    return uv_run(loop, UV_RUN_DEFAULT);
}

/*
    Downloaded 0.00%
    Downloaded 5.69%
    Downloaded 6.53%
    Downloaded 16.07%
    Downloaded 17.20%
    Downloaded 26.89%
    Downloaded 32.12%
    Downloaded 37.84%
    Downloaded 44.60%
    Downloaded 52.89%
    Downloaded 58.96%
    Downloaded 64.44%
    Downloaded 66.66%
    Downloaded 75.35%
    Downloaded 77.88%
    Downloaded 87.29%
    Downloaded 88.52%
    Downloaded 95.74%
    Download complete
*/