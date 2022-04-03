## Error handling
 - 可以使用uv_strerror(int)和uv_err_name(int)分别获取const char *格式的错误信息和错误名字

## Storing context
 - 在基于回调函数的编程风格中，你可能会需要在调用处和回调函数之间，传递一些上下文等特定的应用信息。所有的handle和request都有一个data域，可以用来存储信息并传递。这是一个c语言库中很常见的模式。即使是uv_loop_t也有一个相似的data域