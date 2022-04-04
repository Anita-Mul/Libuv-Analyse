#include <stdio.h>

#include <uv.h>

uv_loop_t *loop;
uv_fs_t stdin_watcher;
// 空转句柄类型
uv_idle_t idler;
char buffer[1024];

// 空转的回调函数会在每一次的event-loop循环激发一次
// 如果注释掉 uv_idle_stop，那么屏幕会一直输出 Computing PI...
void crunch_away(uv_idle_t* handle) {
    // Compute extra-terrestrial life
    // fold proteins
    // computer another digit of PI
    // or similar
    fprintf(stderr, "Computing PI...\n");
    // 停止句柄，回调函数将不会再被调用
    uv_idle_stop(handle);
}

void on_type(uv_fs_t *req) {
    // 屏幕上输入的字节数
    if (stdin_watcher.result > 0) {
        buffer[stdin_watcher.result] = '\0';
        printf("Typed %s\n", buffer);

        uv_buf_t buf = uv_buf_init(buffer, 1024);
        uv_fs_read(loop, &stdin_watcher, 0, &buf, 1, -1, on_type);
        
        // 再次初始化句柄
        uv_idle_start(&idler, crunch_away);
    }
    else if (stdin_watcher.result < 0) {
        fprintf(stderr, "error opening file: %s\n", uv_strerror(req->result));
    }
}

// 空转的回调函数
int main() {
    loop = uv_default_loop();

    // 初始化句柄
    uv_idle_init(loop, &idler);

    uv_buf_t buf = uv_buf_init(buffer, 1024);
    // 第三个参数是 uv_fs_read 标准输入
    uv_fs_read(loop, &stdin_watcher, 0, &buf, 1, -1, on_type);

    // 以给定的回调函数开始句柄
    uv_idle_start(&idler, crunch_away);
    return uv_run(loop, UV_RUN_DEFAULT);
}

// 执行结果
/*
    首先在屏幕上出现
            Computing PI...
    输入 anita 之后回车，屏幕出现
            Typed anita

            Computing PI...
*/
/*
    Computing PI...
    anita
    Typed anita

    Computing PI...
*/