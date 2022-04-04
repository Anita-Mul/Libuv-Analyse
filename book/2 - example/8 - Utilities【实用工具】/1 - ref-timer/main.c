#include <stdio.h>

#include <uv.h>

uv_loop_t *loop;
uv_timer_t gc_req;
uv_timer_t fake_job_req;

void gc(uv_timer_t *handle) {
    fprintf(stderr, "Freeing unused objects\n");
}

void fake_job(uv_timer_t *handle) {
    fprintf(stdout, "Fake job done\n");
}

/**
 * 首先初始化垃圾回收器的定时器，然后在立刻unref它。注意观察9秒之后，
 * 此时fake_job完成，程序会自动退出，即使垃圾回收器还在运行
 */
int main() {
    loop = uv_default_loop();

    uv_timer_init(loop, &gc_req);
    uv_unref((uv_handle_t*) &gc_req);

    uv_timer_start(&gc_req, gc, 0, 2000);

    // could actually be a TCP download or something
    uv_timer_init(loop, &fake_job_req);
    uv_timer_start(&fake_job_req, fake_job, 9000, 0);
    return uv_run(loop, UV_RUN_DEFAULT);
}

// 执行结果
/*
    Freeing unused objects
    Freeing unused objects
    Freeing unused objects
    Freeing unused objects
    Freeing unused objects
    Fake job done
*/
