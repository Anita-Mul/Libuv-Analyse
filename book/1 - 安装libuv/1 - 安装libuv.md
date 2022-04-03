## 安装 libuv （下面两种方法任选一种）
 - 环境 `centos 7`
#### yum 命令安装
 - yum install -y libuv libuv-devel
 - 可以在 `/usr/local/lib` 中看到安装好的文件

#### 压缩包安装
 - [Libuv 版本为 1.9.1](https://github.com/libuv/libuv/archive/refs/heads/v1.x.zip)
 - 解压
    `unzip libuv-1.x.zip`
 - `sh autogen.sh`
 - 生成 `makefile` 文件
    `./configure`
 - 编译，编译之后可以在 `libuv-1.x.zip/.libs` 文件夹下看到编译好的文件
    `make`
 - 测试
    `make check`
 - 安装编译好的程序
    `make install`
 - 运行完之后输入 `echo $?` 检查看看是否有错误，只要输出结果为0，那就说明我们的安装成功。如果在`/usr/local/lib/`目录下有`libuv`的静态库、动态库等内容，也可以说明安装成功

## 测试
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
 - 编译程序【动态库链接（-l:指定库名 -L:指定库路径）】
    ```
    gcc hello.c -L/usr/local/lib/ -luv -o hello 
    ```
    > 解决 `error while loading shared libraries: libuv.so.1: cannot open shared object file: No such file or directory` 问题
    > - 在 `./etc/ld.so.conf` 文件中添加 `/usr/local/lib/`
    > - 之后切换用户为 `root`，执行 `ldconfig`