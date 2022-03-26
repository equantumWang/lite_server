#ifndef CONN_H
#define CONN_H

#include <arpa/inet.h>
#include "fdwrapper.h"

class conn
{
public:
    conn();
    ~conn();
    void init_clt( int sockfd, const sockaddr_in& client_addr );
    void init_srv( int sockfd, const sockaddr_in& server_addr );
    void reset();
    RET_CODE read_clt();    //客户端读事件处理
    RET_CODE write_clt();
    RET_CODE read_srv();    //服务器读事件处理
    RET_CODE write_srv();

public:
    static const int BUF_SIZE = 2048;   //统一缓冲区大小

    char* m_clt_buf;        //客户端缓冲区头指针
    int m_clt_read_idx;     //客户目前读取到的位置
    int m_clt_write_idx;    //客户目前写到的位置
    sockaddr_in m_clt_address;  //客户地址
    int m_cltfd;                //客户fd

    char* m_srv_buf;
    int m_srv_read_idx;
    int m_srv_write_idx;
    sockaddr_in m_srv_address;
    int m_srvfd;

    bool m_srv_closed;      //服务器关闭标志位
};

#endif
