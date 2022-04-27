#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uv.h>

uv_loop_t *loop;

void alloc_buffer(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
  buf->base = malloc(suggested_size);
  buf->len = suggested_size;
}

void on_read(uv_stream_t *client, ssize_t nread, const uv_buf_t *buf) {
    if (nread < 0) {
        if (nread != UV_EOF)
            fprintf(stderr, "Read error %s\n", uv_err_name(nread));
        uv_close((uv_handle_t*) client, NULL);
        free(buf->base);
        free(client);
        return;
    }

    char *data = (char*) malloc(sizeof(char) * (nread+1));
    data[nread] = '\0';
    strncpy(data, buf->base, nread);

    fprintf(stderr, "%s", data);
    free(data);
    free(buf->base);
}

void on_connect(uv_connect_t *req, int status) {
    if (status < 0) {
        fprintf(stderr, "connect failed error %s\n", uv_err_name(status));
        free(req);
        return;
    }

    uv_read_start((uv_stream_t*) req->handle, alloc_buffer, on_read);
    free(req);
}

void on_resolved(uv_getaddrinfo_t *resolver, int status, struct addrinfo *res) {
    if (status < 0) {
        fprintf(stderr, "getaddrinfo callback error %s\n", uv_err_name(status));
        return;
    }

    char addr[17] = {'\0'};
    uv_ip4_name((struct sockaddr_in*) res->ai_addr, addr, 16);
    fprintf(stderr, "%s\n", addr);

    uv_connect_t *connect_req = (uv_connect_t*) malloc(sizeof(uv_connect_t));
    uv_tcp_t *socket = (uv_tcp_t*) malloc(sizeof(uv_tcp_t));
    
    uv_tcp_init(loop, socket);
    uv_tcp_connect(connect_req, socket, (const struct sockaddr*) res->ai_addr, on_connect);

    // 释放结构 addrinfo
    uv_freeaddrinfo(res);
}


/*
    libuv提供了一个异步的DNS解决方案。它提供了自己的getaddrinfo。在回调函数中你可以像使用正常的socket
    操作一样

    根据dns解析地址，将解析后的地址再通过tcp发送
*/
int main() {
    loop = uv_default_loop();

    struct addrinfo hints;
    hints.ai_family = PF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = 0;

    uv_getaddrinfo_t resolver;
    
    fprintf(stderr, "irc.freenode.net is... ");
    // 如果uv_getaddrinfo返回非零值，说明设置错误了，因此也不会激发回调函数。在函数返回后，所有的参数将会
    // 被回收和释放
    int r = uv_getaddrinfo(loop, &resolver, on_resolved, "irc.freenode.net", "6667", &hints);

    if (r) {
        fprintf(stderr, "getaddrinfo call error %s\n", uv_err_name(r));
        return 1;
    }
    
    return uv_run(loop, UV_RUN_DEFAULT);
}
