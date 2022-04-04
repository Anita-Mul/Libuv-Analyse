#include <stdio.h>
#include <inttypes.h>

#include <uv.h>

uv_loop_t *loop;
uv_process_t child_req;
/*
    由于上述的options是全局变量，因此被初始化为0。如果你在局部变量中定义options，请记得将所有没用的域设为0
    v_process_options_t options = {0};
*/
uv_process_options_t options;

void on_exit(uv_process_t *req, int64_t exit_status, int term_signal) {
    fprintf(stderr, "Process exited with status %" PRId64 ", signal %d\n", exit_status, term_signal);
    // 在进程关闭后，需要回收handler
    uv_close((uv_handle_t*) req, NULL);
}

/*
    uv_process_t只是作为句柄，所有的选择项都通过uv_process_options_t设置，为了简单地开始一个进程，你只需要
    设置file和args，file是要执行的程序，args是所需的参数（和c语言中main函数的传入参数类似）。因为uv_spawn在
    内部使用了execvp，所以不需要提供绝对地址。遵从惯例，实际传入参数的数目要比需要的参数多一个，因为最后一个
    参数会被设为NULL。
    设置uv_process_options_t.cwd，更改相应的目录
*/
int main() {
    loop = uv_default_loop();

    char* args[3];
    args[0] = "mkdir";
    args[1] = "test-dir";
    args[2] = NULL;

    options.exit_cb = on_exit;
    options.file = "mkdir";
    options.args = args;

    int r;
    // 初始化进程描述符并启动进程
    if ((r = uv_spawn(loop, &child_req, &options))) {
        fprintf(stderr, "%s\n", uv_strerror(r));
        return 1;
    } else {
        // 在函数uv_spawn被调用之后，uv_process_t.pid会包含子进程的id
        fprintf(stderr, "Launched process with ID %d\n", child_req.pid);
    }

    return uv_run(loop, UV_RUN_DEFAULT);
}

/*
    执行结果
    Launched process with ID 51777
    Process exited with status 0, signal 0
*/