## 安装 libuv （下面两种方法任选一种）
 - 环境 `centos 7`
#### yum 命令安装
 - yum install -y libuv libuv-devel
 - 可以在 `/usr/local/lib` 中看到安装好的文件

#### 压缩包安装
 - [Libuv 版本为 1.9.1](https://dist.libuv.org/dist/v1.9.1/libuv-v1.9.1.tar.gz)
 - 解压
    `tar xzvf libuv-v1.9.1.tar.gz`
 - `sh autogen.sh`
 - 生成 `makefile` 文件
    `./configure --enable-static=yes`
 - 编译
    `make`
 - 测试
    `make check`
 - 安装编译好的程序
    `make install`
 - 运行完之后输入 `echo $?` 检查看看是否有错误，只要输出结果为0，那就说明我们的安装成功


## 测试（下面两种方法任选一种）
#### 方法一
 - `hello.c`
    ```c
    #include <stdio.h>
    #include <stdlib.h>
    #include <uv.h>

    int main() {
        uv_loop_t *loop = malloc(sizeof(uv_loop_t));
        uv_loop_init(loop);

        printf("Now quitting.\n");
        uv_run(loop, UV_RUN_DEFAULT);

        uv_loop_close(loop);
        free(loop);
        return 0;
    }
    ```
 - 编译运行
    ```
    gcc hello.c -L/usr/local/lib/ -luv -o hello 
    ```
    > 解决 `error while loading shared libraries: libuv.so.1: cannot open shared object file: No such file or directory` 问题
    > - 在 `./etc/ld.so.conf` 文件中添加 `/usr/local/lib/`
    > - 之后切换用户为 `root`，执行 `ldconfig`

#### 方法二：用编译好的代码进行测试
 - 新建 `project` 目录，将 `libuv-v1.9.1` 目录下 `.libs/libua.a` 和文件夹 `include` 复制到 `project` 中。
 - `main.c`
    ```c
    #include <stdio.h>
    #include <stdlib.h>
    #include "./include/uv.h"
    
    int main()
    {
            uv_loop_t *loop = malloc( sizeof( uv_loop_t ) );
            uv_loop_init( loop );
            printf("Now quitting\n");
            uv_run( loop, UV_RUN_DEFAULT );
            uv_loop_close( loop );
            free( loop );
            return 0;
    }
    ```
 - 编译运行
    ```
    gcc main.c -L ./base/libuv/ -lpthread -lrt -luv
    ```