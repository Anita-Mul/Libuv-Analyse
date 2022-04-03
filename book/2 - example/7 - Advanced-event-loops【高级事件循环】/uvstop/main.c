#include <stdio.h>
#include <uv.h>

int64_t counter = 0;

void idle_cb(uv_idle_t *handle) {
    printf("Idle callback\n");
    counter++;

    if (counter >= 5) {
        // 停止事件循环
        uv_stop(uv_default_loop());
        printf("uv_stop() called\n");
    }
}

void prep_cb(uv_prepare_t *handle) {
    printf("Prep callback\n");
}

int main() {
    // 空转句柄
    uv_idle_t idler;
    // 准备句柄
    uv_prepare_t prep;

    uv_idle_init(uv_default_loop(), &idler);
    uv_idle_start(&idler, idle_cb);

    uv_prepare_init(uv_default_loop(), &prep);
    uv_prepare_start(&prep, prep_cb);

    uv_run(uv_default_loop(), UV_RUN_DEFAULT);

    return 0;
}

// 执行结果
/*
    Idle callback
    Prep callback
    Idle callback
    Prep callback
    Idle callback
    Prep callback
    Idle callback
    Prep callback
    Idle callback
    uv_stop() called
    Prep callback           // 当停止事件循环后，还要继续将当前循环代码继续执行完毕
*/
