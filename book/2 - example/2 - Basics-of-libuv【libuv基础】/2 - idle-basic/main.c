#include <stdio.h>
#include <uv.h>

int64_t counter = 0;

void wait_for_a_while(uv_idle_t* handle) {
    counter++;

    if (counter >= 10e6)
        // 停止句柄，回调函数将不会再被调用
        uv_idle_stop(handle);
}

int main() {
    // 空转句柄类型
    uv_idle_t idler;

    // 初始化句柄
    uv_idle_init(uv_default_loop(), &idler);
    // 以给定的回调函数开始句柄
    // 回调函数在每一个循环中都会被调用
    uv_idle_start(&idler, wait_for_a_while);

    printf("Idling...\n");

    // 这个函数运行事件循环。 它将依指定的模式而采取不同的行为
    // 当达到事先规定好的计数后，空转监视器会退出。因为uv_run
    // 已经找不到活着的事件监视器了，所以uv_run()也退出
    uv_run(uv_default_loop(), UV_RUN_DEFAULT);
    uv_loop_close(uv_default_loop());
    return 0;
}
