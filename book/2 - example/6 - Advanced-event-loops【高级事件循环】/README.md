## Advanced event loops
#### 停止一个 event loop
 - uv_stop()用来终止 event loop。loop会停止的最早时间点是在下次循环的时候，或者稍晚些的时候。这也就意味着在本次循环中已经准备被处理的事件，依然会被处理，uv_stop不会起到作用

#### uvstop/main.c
 - 编译执行
    `gcc main.c -L/usr/local/lib/ -luv -o main`