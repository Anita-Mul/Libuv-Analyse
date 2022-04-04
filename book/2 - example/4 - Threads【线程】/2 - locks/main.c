#include <stdio.h>
#include <uv.h>

uv_barrier_t blocker;
uv_rwlock_t numlock;
int shared_num;

/*
pthread_barrier_xxx其实只做且只能做一件事,
就是充当栏杆（barrier意为栏杆）形象的说就是把先后到达的多个线程挡在同一栏杆前,
直到所有线程到齐，然后撤下栏杆同时放行
使用了屏障。因此主线程来等待所有的线程都已经结束，最后再将屏障和锁一块回收
https://www.cnblogs.com/J1ac/p/9042857.html
*/
void reader(void *n)
{
    int num = *(int *)n;
    int i;

    for (i = 0; i < 20; i++) {
        uv_rwlock_rdlock(&numlock);
        printf("Reader %d: acquired lock\n", num);
        printf("Reader %d: shared num = %d\n", num, shared_num);
        uv_rwlock_rdunlock(&numlock);
        printf("Reader %d: released lock\n", num);
    }

    uv_barrier_wait(&blocker);
}

void writer(void *n)
{
    int num = *(int *)n;
    int i;

    for (i = 0; i < 20; i++) {
        uv_rwlock_wrlock(&numlock);
        printf("Writer %d: acquired lock\n", num);
        shared_num++;
        printf("Writer %d: incremented shared num = %d\n", num, shared_num);
        uv_rwlock_wrunlock(&numlock);
        printf("Writer %d: released lock\n", num);
    }

    uv_barrier_wait(&blocker);
}

/*
读写锁是更细粒度的实现机制。两个读者线程可以同时从共享区中读取数据。当读者以读模式占有读写锁时，
写者不能再占有它。当写者以写模式占有这个锁时，其他的写者或者读者都不能占有它。读写锁在数据库操作
中非常常见，下面是一个玩具式的例子

在有多个写者的时候，调度器会给予他们高优先级。因此，如果你加入两个读者，你会看到所有的读者趋向于
在读者得到加锁机会前结束
*/
int main()
{
    uv_barrier_init(&blocker, 4);

    shared_num = 0;
    // 初始化读写锁
    uv_rwlock_init(&numlock);

    uv_thread_t threads[3];

    int thread_nums[] = {1, 2, 1};

    // int uv_thread_create(uv_thread_t* tid, uv_thread_cb entry, void* arg)
    uv_thread_create(&threads[0], reader, &thread_nums[0]);
    uv_thread_create(&threads[1], reader, &thread_nums[1]);

    uv_thread_create(&threads[2], writer, &thread_nums[2]);

    uv_barrier_wait(&blocker);
    uv_barrier_destroy(&blocker);

    // 销毁读写锁
    uv_rwlock_destroy(&numlock);
    return 0;
}


/*
    执行结果
    Reader 1: acquired lock
    Reader 1: shared num = 0
    Reader 1: released lock
    Reader 1: acquired lock
    Reader 1: shared num = 0
    Reader 1: released lock
    Reader 1: acquired lock
    Reader 1: shared num = 0
    Reader 1: released lock
    Reader 1: acquired lock
    Reader 1: shared num = 0
    Reader 1: released lock
    Reader 1: acquired lock
    Reader 1: shared num = 0
    Reader 1: released lock
    Reader 1: acquired lock
    Reader 1: shared num = 0
    Reader 1: released lock
    Reader 1: acquired lock
    Reader 1: shared num = 0
    Reader 1: released lock
    Reader 1: acquired lock
    Reader 1: shared num = 0
    Reader 1: released lock
    Reader 1: acquired lock
    Reader 1: shared num = 0
    Reader 1: released lock
    Reader 1: acquired lock
    Reader 1: shared num = 0
    Reader 1: released lock
    Reader 1: acquired lock
    Reader 1: shared num = 0
    Reader 1: released lock
    Reader 1: acquired lock
    Reader 1: shared num = 0
    Reader 1: released lock
    Reader 1: acquired lock
    Reader 1: shared num = 0
    Reader 1: released lock
    Reader 1: acquired lock
    Reader 1: shared num = 0
    Reader 1: released lock
    Reader 1: acquired lock
    Reader 1: shared num = 0
    Reader 1: released lock
    Reader 1: acquired lock
    Reader 1: shared num = 0
    Reader 1: released lock
    Reader 1: acquired lock
    Reader 1: shared num = 0
    Reader 1: released lock
    Reader 1: acquired lock
    Reader 1: shared num = 0
    Reader 1: released lock
    Reader 1: acquired lock
    Reader 1: shared num = 0
    Reader 1: released lock
    Reader 1: acquired lock
    Reader 1: shared num = 0
    Reader 1: released lock
    Reader 2: acquired lock
    Reader 2: shared num = 0
    Reader 2: released lock
    Reader 2: acquired lock
    Reader 2: shared num = 0
    Reader 2: released lock
    Reader 2: acquired lock
    Reader 2: shared num = 0
    Reader 2: released lock
    Reader 2: acquired lock
    Reader 2: shared num = 0
    Reader 2: released lock
    Reader 2: acquired lock
    Reader 2: shared num = 0
    Reader 2: released lock
    Reader 2: acquired lock
    Reader 2: shared num = 0
    Reader 2: released lock
    Reader 2: acquired lock
    Reader 2: shared num = 0
    Reader 2: released lock
    Reader 2: acquired lock
    Reader 2: shared num = 0
    Reader 2: released lock
    Reader 2: acquired lock
    Reader 2: shared num = 0
    Reader 2: released lock
    Reader 2: acquired lock
    Reader 2: shared num = 0
    Reader 2: released lock
    Reader 2: acquired lock
    Reader 2: shared num = 0
    Reader 2: released lock
    Reader 2: acquired lock
    Reader 2: shared num = 0
    Reader 2: released lock
    Reader 2: acquired lock
    Reader 2: shared num = 0
    Reader 2: released lock
    Reader 2: acquired lock
    Reader 2: shared num = 0
    Reader 2: released lock
    Reader 2: acquired lock
    Reader 2: shared num = 0
    Reader 2: released lock
    Reader 2: acquired lock
    Reader 2: shared num = 0
    Reader 2: released lock
    Reader 2: acquired lock
    Reader 2: shared num = 0
    Reader 2: released lock
    Reader 2: acquired lock
    Reader 2: shared num = 0
    Reader 2: released lock
    Reader 2: acquired lock
    Reader 2: shared num = 0
    Reader 2: released lock
    Reader 2: acquired lock
    Reader 2: shared num = 0
    Reader 2: released lock
    Writer 1: acquired lock
    Writer 1: incremented shared num = 1
    Writer 1: released lock
    Writer 1: acquired lock
    Writer 1: incremented shared num = 2
    Writer 1: released lock
    Writer 1: acquired lock
    Writer 1: incremented shared num = 3
    Writer 1: released lock
    Writer 1: acquired lock
    Writer 1: incremented shared num = 4
    Writer 1: released lock
    Writer 1: acquired lock
    Writer 1: incremented shared num = 5
    Writer 1: released lock
    Writer 1: acquired lock
    Writer 1: incremented shared num = 6
    Writer 1: released lock
    Writer 1: acquired lock
    Writer 1: incremented shared num = 7
    Writer 1: released lock
    Writer 1: acquired lock
    Writer 1: incremented shared num = 8
    Writer 1: released lock
    Writer 1: acquired lock
    Writer 1: incremented shared num = 9
    Writer 1: released lock
    Writer 1: acquired lock
    Writer 1: incremented shared num = 10
    Writer 1: released lock
    Writer 1: acquired lock
    Writer 1: incremented shared num = 11
    Writer 1: released lock
    Writer 1: acquired lock
    Writer 1: incremented shared num = 12
    Writer 1: released lock
    Writer 1: acquired lock
    Writer 1: incremented shared num = 13
    Writer 1: released lock
    Writer 1: acquired lock
    Writer 1: incremented shared num = 14
    Writer 1: released lock
    Writer 1: acquired lock
    Writer 1: incremented shared num = 15
    Writer 1: released lock
    Writer 1: acquired lock
    Writer 1: incremented shared num = 16
    Writer 1: released lock
    Writer 1: acquired lock
    Writer 1: incremented shared num = 17
    Writer 1: released lock
    Writer 1: acquired lock
    Writer 1: incremented shared num = 18
    Writer 1: released lock
    Writer 1: acquired lock
    Writer 1: incremented shared num = 19
    Writer 1: released lock
    Writer 1: acquired lock
    Writer 1: incremented shared num = 20
    Writer 1: released lock
*/