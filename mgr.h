#ifndef SRVMGR_H
#define SRVMGR_H

#include <map>
#include <arpa/inet.h>
#include "fdwrapper.h"
#include "conn.h"

using std::map;

class host
{
public:
    char m_hostname[1024];  //主机名
    int m_port;             //端口
    int m_conncnt;          //连接到服务器的数量
};

class mgr
{
public:
    mgr( int epollfd, const host& srv );        //构造函数，创建n个连接到服务器的客户；
    ~mgr();
    int conn2srv( const sockaddr_in& address ); //客户端连接服务器封装函数
    conn* pick_conn( int sockfd );              //从空闲连接池中选择一个连接到服务器
    void free_conn( conn* connection );         //断开连接，并归还连接资源到回收池
    int get_used_conn_cnt();                    //当前正在使用的连接数，用于计算当前进程的忙绿度
    void recycle_conns();                       //从回收池中回收连接到连接池
    RET_CODE process( int fd, OP_TYPE type );   //工作处理进程

private:
    static int m_epollfd;
    map< int, conn* > m_conns;  //分配的连接池
    map< int, conn* > m_used;   //正在使用的连接
    map< int, conn* > m_freed;  //回收池
    host m_logic_srv;           //逻辑服务器
};

#endif
