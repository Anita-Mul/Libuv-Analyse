#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <uv.h>

uv_loop_t *loop;
uv_tty_t tty;

// 把白字打印到红色背景上
int main() {
    loop = uv_default_loop();

    uv_tty_init(loop, &tty, 1, 0);
    // 使用指定的终端模式设置TTY
    /* 初始/正常终端模式 */
    uv_tty_set_mode(&tty, UV_TTY_MODE_NORMAL);
    
    if (uv_guess_handle(1) == UV_TTY) {
        uv_write_t req;
        uv_buf_t buf;
        buf.base = "\033[41;37m";
        buf.len = strlen(buf.base);
        uv_write(&req, (uv_stream_t*) &tty, &buf, 1, NULL);
    }

    uv_write_t req;
    uv_buf_t buf;
    buf.base = "Hello TTY\n";
    buf.len = strlen(buf.base);
    uv_write(&req, (uv_stream_t*) &tty, &buf, 1, NULL);

    // 当程序退出时将被调用
    uv_tty_reset_mode();
    return uv_run(loop, UV_RUN_DEFAULT);
}
