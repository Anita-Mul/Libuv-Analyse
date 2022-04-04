#include <stdio.h>
#include <stdlib.h>
#include <uv.h>


/*
    用户就可以在使用uv_loop_init初始化loop之前，给其分配相应的内存。
    这就允许你植入自定义的内存管理方法。记住要使用uv_loop_close(uv_loop_t *)
    关闭loop，然后再回收内存空间
*/
int main() {
    uv_loop_t *loop = malloc(sizeof(uv_loop_t));
    uv_loop_init(loop);

    printf("Now quitting.\n");
    uv_run(loop, UV_RUN_DEFAULT);

    uv_loop_close(loop);
    free(loop);
    return 0;
}
