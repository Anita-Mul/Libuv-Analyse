#include <stdio.h>

#include <uv.h>

uv_loop_t *loop;
uv_process_t child_req;
uv_process_options_t options;

int main() {
    loop = uv_default_loop();

    char* args[3];
    args[0] = "sleep";
    args[1] = "100";
    args[2] = NULL;

    options.exit_cb = NULL;
    options.file = "sleep";
    options.args = args;
    // 使用标识 UV_PROCESS_DETACHED 可以启动守护进程(daemon)，或者是使得子进程
    // 从父进程中独立出来，这样父进程的退出就不会影响到它
    options.flags = UV_PROCESS_DETACHED;

    int r;
    if ((r = uv_spawn(loop, &child_req, &options))) {
        fprintf(stderr, "%s\n", uv_strerror(r));
        return 1;
    }

    fprintf(stderr, "Launched sleep with PID %d\n", child_req.pid);
    /*
        反引用给定的句柄。 引用是幂等的，也就是说， 如果没有被引用的句柄再调用这个函数无效果。
        handle会始终监视着子进程，所以你的程序不会退出。uv_unref()会解除handle
    */
    uv_unref((uv_handle_t*) &child_req);

    return uv_run(loop, UV_RUN_DEFAULT);
}

/*
    执行结果
    Launched sleep with PID 52084
*/