> 在 libuv 当中 queue 的使用十分频繁，所以第一节咱们就从 queue 开始，分析一下 queue 在 Libuv 中的实现
## 定义指针数组类型
```c
typedef void *QUEUE[2];
```
 - 使用
	```c
	QUEUE q;	// 相当于 void *q[2]
	```
![在这里插入图片描述](https://p3-juejin.byteimg.com/tos-cn-i-k3u1fbpfcp/9c1dba67a71944ae894ea7aaa87aeab1~tplv-k3u1fbpfcp-zoom-1.image)
## 定义基本操作
```c
#define QUEUE_NEXT(q)       (*(QUEUE **) &((*(q))[0]))
#define QUEUE_PREV(q)       (*(QUEUE **) &((*(q))[1]))
#define QUEUE_PREV_NEXT(q)  (QUEUE_NEXT(QUEUE_PREV(q)))
#define QUEUE_NEXT_PREV(q)  (QUEUE_PREV(QUEUE_NEXT(q)))
```
#### QUEUE_NEXT
 - 使用
	```c
	QUEUE queue;
	// 返回值是下一个节点QUEUE的指针
	QUEUE_NEXT(&queue);
	```
 - `(*(QUEUE **) &((*(q))[0]))` 相当于 `(*q)[0]`，为什么要写的这么复杂呢？主要有两个原因：类型保持、成为左值。
 - `(*(q))[0]`：首先，传入 q 的类型为 &QUEUE，那么 (*(q)) 类型为 QUEUE，`(*(q))[0]` 相当于 queue[0]
 - `*(QUEUE **) &((*(q))[0])`：queue[0] 的类型为 void*，那么 &(queue[0]) 的类型就为 void**，这可不行，明明应该是 QUEUE** 类型，怎么能是 void**，所以要进行 `(QUEUE **) &((*(q))[0])` 类型转换。还是有问题，最后返回的是下一个节点QUEUE的指针，现在变成了指针的指针，所以还要对 `(QUEUE **) &((*(q))[0])` 再次取值 `*(QUEUE **) &((*(q))[0])`
 - 这时候你该问了：为什么不能写成 `(QUEUE*)(*(q))[0]` 这样的呢？这是为了使其成为左值，左值的简单定义是：占用实际的内存、可以对其进行取地址操作的变量都是左值，而c语言中（其实其他语言也是一样），对于一个变量（或者表达式）进行强制类型转换时，其实并不是改变该变量本身的类型，而是产生一个变量的副本，而这个副本并不是左值（因为并不能对其取地址），它是一个右值，举个例子：int a = 1; (char) a = 2;这样会报错。而如果改成这样：int a = 1; (*(char *)(&a)) = 2;就正确了。
![在这里插入图片描述](https://p3-juejin.byteimg.com/tos-cn-i-k3u1fbpfcp/5627764f562147a2ac104a7c3b96ac6b~tplv-k3u1fbpfcp-zoom-1.image)
## 队列操作
#### 队列初始化
```c
#define QUEUE_INIT(q)                                                         \
  do {                                                                        \
    QUEUE_NEXT(q) = (q);                                                      \
    QUEUE_PREV(q) = (q);                                                      \
  }                                                                           \
  while (0)
```
 - 初始化队列q就是将其next和prev的指针指向自己

#### 队列为空判断
```c
#define QUEUE_EMPTY(q)                                                        \
  ((const QUEUE *) (q) == (const QUEUE *) QUEUE_NEXT(q))
```
- 只要q的next指针还是指向自己，就说明队列为空（只有链表头结点）
#### 队列遍历
```c
#define QUEUE_FOREACH(q, h)                                                   \
  for ((q) = QUEUE_NEXT(h); (q) != (h); (q) = QUEUE_NEXT(q))
```
 - 遍历队列q，直到遍历到h为止。注意：在遍历时，不要同时对队列q进行插入、删除操作，否则会出现未知错误
#### 获取队列头
```c
#define QUEUE_HEAD(q)                                                         \
   (QUEUE_NEXT(q))
```
 - 链表头节点的next返回的就是队列的head节点
#### 向队列头插入节点
```c
// prev 是下一个元素
// next 是上一个元素
#define QUEUE_INSERT_HEAD(h, q)                                               \
  do {                                                                        \
    QUEUE_NEXT(q) = QUEUE_NEXT(h);     	                                        \
    QUEUE_PREV(q) = (h);                                                      \
    QUEUE_NEXT_PREV(q) = (q);                                                 \
    QUEUE_NEXT(h) = (q);                                                      \
  }                                                                           \
  while (0)
```
 - 队列 h，p 的起始状态
![在这里插入图片描述](https://p3-juejin.byteimg.com/tos-cn-i-k3u1fbpfcp/5140929e8ffc4ca18291d2319f5e7256~tplv-k3u1fbpfcp-zoom-1.image)

 - 插入【`QUEUE_INSERT_HEAD(h, q)`】
![在这里插入图片描述](https://p3-juejin.byteimg.com/tos-cn-i-k3u1fbpfcp/e6c7aad514a649b08006cd191ef60a18~tplv-k3u1fbpfcp-zoom-1.image)
 - 再插入一个节点【`QUEUE_INSERT_HEAD(h, n)`】
![在这里插入图片描述](https://p3-juejin.byteimg.com/tos-cn-i-k3u1fbpfcp/7659a1490ab7419a85cecdbdf6ada8b4~tplv-k3u1fbpfcp-zoom-1.image)
#### 向队列尾部插入节点
```c
// prev 是下一个元素
// next 是上一个元素
#define QUEUE_INSERT_TAIL(h, q)                                               \
  do {                                                                        \
    QUEUE_NEXT(q) = (h);                                                      \
    QUEUE_PREV(q) = QUEUE_PREV(h);                                            \
    QUEUE_PREV_NEXT(q) = (q);                                                 \
    QUEUE_PREV(h) = (q);                                                      \
  }                                                                           \
  while (0)
```
 -  插入【`QUEUE_INSERT_HEAD(h, q)`】
![在这里插入图片描述](https://p3-juejin.byteimg.com/tos-cn-i-k3u1fbpfcp/77dfb69806df419fbd12f899fa3b0416~tplv-k3u1fbpfcp-zoom-1.image)
> 相信你这时候已经明白了 queue 大体结构是如何实现的了，下面的这些操作自己试着分析一下😀

#### 队列相加
```c
#define QUEUE_ADD(h, n)                                                       \
  do {                                                                        \
    QUEUE_PREV_NEXT(h) = QUEUE_NEXT(n);                                       \
    QUEUE_NEXT_PREV(n) = QUEUE_PREV(h);                                       \
    QUEUE_PREV(h) = QUEUE_PREV(n);                                            \
    QUEUE_PREV_NEXT(h) = (h);                                                 \
  }                                                                           \
  while (0)
```
#### 队列分割
```c
#define QUEUE_SPLIT(h, q, n)                                                  \
  do {                                                                        \
    QUEUE_PREV(n) = QUEUE_PREV(h);                                            \
    QUEUE_PREV_NEXT(n) = (n);                                                 \
    QUEUE_NEXT(n) = (q);                                                      \
    QUEUE_PREV(h) = QUEUE_PREV(q);                                            \
    QUEUE_PREV_NEXT(h) = (h);                                                 \
    QUEUE_PREV(q) = (n);                                                      \
  }                                                                           \
  while (0)
```
 -   队列分割就是上述ADD的逆过程，将队列h以q为分割点进行分割，分割出来的新队列为n(n为分出来的双向循环链表的头结点)
#### 队列移动
```c
#define QUEUE_MOVE(h, n)                                                      \
  do {                                                                        \
    if (QUEUE_EMPTY(h))                                                       \
      QUEUE_INIT(n);                                                          \
    else {                                                                    \
      QUEUE* q = QUEUE_HEAD(h);                                               \
      QUEUE_SPLIT(h, q, n);                                                   \
    }                                                                         \
  }                                                                           \
  while (0)
```
 - 将队列h移动到n队里中，首先如果h队列为空，那么就把n初始化为空；如果h不为空，那么就先取出h队列的head节点，然后调用前面论述过的队列分割宏，从head节点开始分割，等价于把h队列的所有内容（输了h自身，因为它是链表头节点）全部转移到n队里里面
#### 队列删除
```c
#define QUEUE_REMOVE(q)                                                       \
   do {                                                                        \
     QUEUE_PREV_NEXT(q) = QUEUE_NEXT(q);                                       \
     QUEUE_NEXT_PREV(q) = QUEUE_PREV(q);                                       \
   }                                                                           \
   while (0)
```
 - 队列删除的原理很简单，现将q前一个节点的next指针修改为指向q的next指针指向的下一个节点，再q的下一个节点的prev指针修改为指向q当前指向的前一个节点
#### 在队列中存取用户数据
```c
#define QUEUE_DATA(ptr, type, field)                                          \
   ((type *) ((char *) (ptr) - offsetof(type, field)))
```
 - 先讲解一下如何使用
```c
#include "queue.h"
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
    john.name = "john";
    john.age = 44;

    struct user_s henry;
    henry.name = "henry";
    henry.age = 32;
    
    struct user_s willy;
    willy.name = "willy";
    willy.age = 99;

    QUEUE_INIT(&queue);
    QUEUE_INIT(&john.node);
    QUEUE_INIT(&henry.node);
    QUEUE_INIT(&willy.node);

    ((*(&queue))[0]) = john.node;
    (*(QUEUE **) &((*(&queue))[0])) = &john.node;
 
    QUEUE_INSERT_TAIL(&queue, &john.node);
    QUEUE_INSERT_TAIL(&queue, &henry.node);
    QUEUE_INSERT_TAIL(&queue, &willy.node);

    q = QUEUE_HEAD(&queue);
  
    user = QUEUE_DATA(q, struct user_s, node);
    
    printf("Received first inserted user: %s who is %d.\n",
        user->name, user->age);
    return 0;
}
```
 - 现在看上面那段代码
	```c
	#define QUEUE_DATA(ptr, type, field)                                          \
	   ((type *) ((char *) (ptr) - offsetof(type, field)))
	```
![在这里插入图片描述](https://p3-juejin.byteimg.com/tos-cn-i-k3u1fbpfcp/dec7f161bd544fada69889c19bfec716~tplv-k3u1fbpfcp-zoom-1.image)
 - 只要结构体中包含了 QUEUE 节点，可以将他们的node成员组成双向循环链表进行管理，这样就可以以队列方式来管理它们的node成员了。拿到node成员的地址之后，只要将该地址减去node成员在结构体中的偏移，就可以拿到整个结构体的起始地址，也就拿到了用户数据了。
