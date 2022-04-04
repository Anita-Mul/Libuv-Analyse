#include <stdio.h>
#include <unistd.h>

#include <uv.h>

void hare(void *arg) {
    int tracklen = *((int *) arg);
    while (tracklen) {
        tracklen--;
        sleep(1);
        fprintf(stderr, "Hare ran another step\n");
    }
    fprintf(stderr, "Hare done running!\n");
}

void tortoise(void *arg) {
    int tracklen = *((int *) arg);
    while (tracklen) {
        tracklen--;
        fprintf(stderr, "Tortoise ran another step\n");
        sleep(3);
    }
    fprintf(stderr, "Tortoise done running!\n");
}

/*
    可以使用uv_thread_create()开始一个线程，
    再使用uv_thread_join()等待其结束

    uv_thread_t的第二个参数指向了要执行的函数的地址。
    最后一个参数用来传递自定义的参数。最终，函数hare
    将在新的线程中执行，由操作系统调度。
*/
int main() {
    int tracklen = 10;
    uv_thread_t hare_id;
    uv_thread_t tortoise_id;
    uv_thread_create(&hare_id, hare, &tracklen);
    uv_thread_create(&tortoise_id, tortoise, &tracklen);

    /*
        uv_thread_join不像pthread_join那样，允许线线程通过第二个参数向父线程返回值。
        想要传递值，必须使用线程间通信Inter-thread communication
    */
    uv_thread_join(&hare_id);
    uv_thread_join(&tortoise_id);
    return 0;
}


/*
    执行结果
    Tortoise ran another step
    Hare ran another step
    Hare ran another step
    Tortoise ran another step
    Hare ran another step
    Hare ran another step
    Hare ran another step
    Tortoise ran another step
    Hare ran another step
    Hare ran another step
    Hare ran another step
    Tortoise ran another step
    Hare ran another step
    Hare ran another step
    Hare done running!
    Tortoise ran another step
    Tortoise ran another step
    Tortoise ran another step
    Tortoise ran another step
    Tortoise ran another step
    Tortoise ran another step
    Tortoise done running!
*/