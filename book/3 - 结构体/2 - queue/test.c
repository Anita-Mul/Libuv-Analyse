#include "./queue.h"
#include <stdio.h>

static QUEUE* q;
static QUEUE queue;

struct user_s {
    int age;
    char* name;
    QUEUE node;
};

int main() {
    struct user_s* user;
    struct user_s john;
    struct user_s henry;

    john.name = "john";
    john.age = 44;
    henry.name = "henry";
    henry.age = 32;

    QUEUE_INIT(&queue);
    QUEUE_INIT(&john.node);
    QUEUE_INIT(&henry.node);

    ((*(&queue))[0]) = john.node;
    (*(QUEUE **) &((*(&queue))[0])) = &john.node;

    QUEUE_INSERT_TAIL(&queue, &john.node);
    QUEUE_INSERT_TAIL(&queue, &henry.node);

    q = QUEUE_HEAD(&queue);

    user = QUEUE_DATA(q, struct user_s, node);

    printf("Received first inserted user: %s who is %d.\n",
           user->name, user->age);

    QUEUE_REMOVE(q);

    QUEUE_FOREACH(q, &queue) {
        user = QUEUE_DATA(q, struct user_s, node);

        printf("Received rest inserted users: %s who is %d.\n",
               user->name, user->age);
    }

    return 0;
}