# Tinyhttpserver
在David1999年的Tinyhttpd基础上进行完善,使用webbench压力测试工具测试

增加了线程池

处理了部分网络编程中的异常情况,如客户端主动断开连接

make

./server

webbench -c 2500 -t 10 http://127.0.0.1:8080/

 ![image](https://github.com/xiaohu306/Tinyhttpserver/httpdocs/test.png)
