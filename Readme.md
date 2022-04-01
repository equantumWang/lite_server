# Springsnail 轻量负载均衡服务器

## Version:1.1

1. 添加InetAddress类，负责IP和端口的生成；

2. 添加util类，负责统一错误信息（一些地方未完全更改为util类实现）

3. **添加测试用的服务器server 与 客户端client**

server与client的用法在**使用方法**部分。

## Version: 1.0

2022/3/26: 仅在原有代码上进行注释。

## 框架1.0：

![framework](https://github.com/equantumWang/lite_server/blob/master/img/1.0Framework.png) 

notes:

1) 主进程会创建n个子进程，子进程的数量跟逻辑服务器的数量相同，每个进程负责一个逻辑服务器的通信，所有可读写事件都交由逻辑服务器完成。

2) epoll中监听的描述符有：信号管道的fd、父进程与各个子进程之间的通信管道、建立连接后的服务器socket fd和客户端socket fd、以及负责建立连接的主进程 listen fd。

3) 每个子进程中都有一个连接管理器mgr，负责逻辑服务器的连接池管理，采用了状态机实现三个状态：空闲连接、被释放的连接以及正在工作的连接。mgr负责连接建立、调度、负载度计算以及可读写事件的处理（实际上调用conn类的处理函数委托给了conn类来实现）

4) 目前config.xml中只配置了一个服务器地址（网易云音乐），因此只有一个子进程被创建。实际上后续可以改成自己的服务器地址来测试，并实现某些功能。

## 使用方法：

1. 编译：
   
   `# make all`

2. 运行逻辑服务器:
   
   `# ./server`

3. 运行负载均衡服务器：
   
   `# ./springsnail -f config.xml`

4. 运行测试客户端
   
   `# ./client`

在运行负载均衡服务器时，会提醒连接到逻辑服务器，且逻辑服务器会提醒有连接建立：

**负载均衡服务器：**

```sh
IP: 127.0.0.1 Port: 54321
bind serverfd: 4 IP: 127.0.0.1 Port: 54321
processpool is created
sub_process_0 is created
process idx: -1 is running
process idx: 0 is running
run_child m_idx: 0 ; host name: 127.0.0.2 
[ 04/01/22 20:10:11 ] mgr.cpp:0054 info: logcial srv host info: (127.0.0.2, 1234)
[ 04/01/22 20:10:12 ] mgr.cpp:0066 info: build connection 0 to server success
[ 04/01/22 20:10:13 ] mgr.cpp:0066 info: build connection 1 to server success
[ 04/01/22 20:10:14 ] mgr.cpp:0066 info: build connection 2 to server success
[ 04/01/22 20:10:14 ] mgr.cpp:0082 info: building connections accomplished
```

**逻辑服务器：**

```shell
new client fd 5! IP: 127.0.0.1 Port: 39456
new client fd 6! IP: 127.0.0.1 Port: 39460
new client fd 7! IP: 127.0.0.1 Port: 39466
```

此时可以运行客户端程序，并在客户端程序中输入字符串按回车发送到服务器，逻辑服务器会将其回显：

## 更新Log:

  具体的修改与添加位于文件的log.md

## Todo list:

相关类封装

~~本机服务器、客户端的测试程序~~

~~框架图~~

更改为线程池实现

定时器

HTTP协议支持

Webbench压力测试

服务器数据库实现web注册、登录以及请求服务器数据内容

## More:

copyright: 《Linux高性能服务器编程》 游双

Ref:

1. [GitHub - liu-jianhao/springsnail: 《Linux 高性能服务器》附带的项目程序springsnil详细解读，一个负载均衡服务器](https://github.com/liu-jianhao/springsnail)

2. [GitHub - qinguoyi/TinyWebServer: Linux下C++轻量级Web服务器](https://github.com/qinguoyi/TinyWebServer)
