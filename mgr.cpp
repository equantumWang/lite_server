#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/stat.h>

#include <exception>
#include "log.h"
#include "mgr.h"
#include"InetAddress.h"

using std::pair;

int mgr::m_epollfd = -1;
int mgr::conn2srv( const sockaddr_in& address )
{
    int sockfd = socket( PF_INET, SOCK_STREAM, 0 );
    if( sockfd < 0 )
    {
        return -1;
    }

    if ( connect( sockfd, ( struct sockaddr* )&address, sizeof( address ) ) != 0  )
    {
        close( sockfd );
        return -1;
    }
    return sockfd;
}

mgr::mgr( int epollfd, const host& srv ) : m_logic_srv( srv )
{
    m_epollfd = epollfd;
    int ret = 0;
    // struct sockaddr_in address;
    // bzero( &address, sizeof( address ) );
    // address.sin_family = AF_INET;
    // inet_pton( AF_INET, srv.m_hostname, &address.sin_addr );
    // address.sin_port = htons( srv.m_port );

    InetAddress *addr = new InetAddress(srv.m_hostname, srv.m_port);//在函数结束时释放
    socklen_t addr_len = addr->getAddr_len();

    log( LOG_INFO, __FILE__, __LINE__, "logcial srv host info: (%s, %d)", srv.m_hostname, srv.m_port );

    for( int i = 0; i < srv.m_conncnt; ++i )
    {
        sleep( 1 );
        int sockfd = conn2srv( addr->getAddr() );
        if( sockfd < 0 )
        {
            log( LOG_ERR, __FILE__, __LINE__, "build connection %d failed", i );
        }
        else
        {
            log( LOG_INFO, __FILE__, __LINE__, "build connection %d to server success", i );
            conn* tmp = NULL;
            try
            {
                tmp = new conn;
            }
            catch( ... )
            {
                close( sockfd );
                continue;
            }
            tmp->init_srv( sockfd, addr->getAddr() );
            //在连接池中[空闲的连接]部分加入服务器端提前开好的sockfd和tmp
            m_conns.insert( pair< int, conn* >( sockfd, tmp ) );
        }
    }
    log( LOG_INFO, __FILE__, __LINE__, "building connections accomplished" );
    
    delete addr;
}

mgr::~mgr()
{
}

int mgr::get_used_conn_cnt()
{
    return m_used.size();
}

conn* mgr::pick_conn( int cltfd  )
{
    //一旦有客户连接到来，从空闲连接池中选取一个空闲连接，并将其对应的clientfd srvfd 加入使用连接池中
    
    if( m_conns.empty() )
    {
        log( LOG_ERR, __FILE__, __LINE__, "%s", "not enough srv connections to server" );
        return NULL;
    }

    map< int, conn* >::iterator iter =  m_conns.begin();
    int srvfd = iter->first;
    conn* tmp = iter->second;
    if( !tmp )
    {
        log( LOG_ERR, __FILE__, __LINE__, "%s", "empty server connection object" );
        return NULL;
    }
    m_conns.erase( iter );
    m_used.insert( pair< int, conn* >( cltfd, tmp ) ); 
    m_used.insert( pair< int, conn* >( srvfd, tmp ) );
    add_read_fd( m_epollfd, cltfd );    //并将对应的客户端fd和服务器fd加入epoll
    add_read_fd( m_epollfd, srvfd );
    log( LOG_INFO, __FILE__, __LINE__, "bind client sock %d with server sock %d", cltfd, srvfd );
    return tmp;
}

void mgr::free_conn( conn* connection )
{
    int cltfd = connection->m_cltfd;
    int srvfd = connection->m_srvfd;
    closefd( m_epollfd, cltfd );
    closefd( m_epollfd, srvfd );
    m_used.erase( cltfd );
    m_used.erase( srvfd );
    connection->reset();
    m_freed.insert( pair< int, conn* >( srvfd, connection ) );
}

void mgr::recycle_conns()
{
    if( m_freed.empty() )
    {
        return;
    }
    for( map< int, conn* >::iterator iter = m_freed.begin(); iter != m_freed.end(); iter++ )
    {
        sleep( 1 );
        int srvfd = iter->first;
        conn* tmp = iter->second;
        srvfd = conn2srv( tmp->m_srv_address );
        if( srvfd < 0 )
        {
            log( LOG_ERR, __FILE__, __LINE__, "%s", "fix connection failed");
        }
        else
        {
            log( LOG_INFO, __FILE__, __LINE__, "%s", "fix connection success" );
            tmp->init_srv( srvfd, tmp->m_srv_address );
            m_conns.insert( pair< int, conn* >( srvfd, tmp ) );
        }
    }
    m_freed.clear();
}

RET_CODE mgr::process( int fd, OP_TYPE type )
{
    conn* connection = m_used[ fd ];    //从正在使用的连接中找到fd对应的连接
    if( !connection )
    {
        return NOTHING;
    }
    if( connection->m_cltfd == fd )     //连接上发生的是客户端的事件(例如客户端有读/写请求)
    {
        int srvfd = connection->m_srvfd;
        switch( type )
        {
            case READ:
            {
                RET_CODE res = connection->read_clt();  //委托给conn类实现的处理函数
                switch( res )
                {
                    case OK:
                    {
                        log( LOG_DEBUG, __FILE__, __LINE__, "content read from client: %s", connection->m_clt_buf );
                    }
                    case BUFFER_FULL:
                    {
                        modfd( m_epollfd, srvfd, EPOLLOUT );    //读缓冲区满，先写
                        break;
                    }
                    case IOERR:
                    case CLOSED:
                    {
                        free_conn( connection );
                        return CLOSED;
                    }
                    default:
                        break;
                }
                if( connection->m_srv_closed )
                {
                    free_conn( connection );
                    return CLOSED;
                }
                break;
            }
            case WRITE:
            {
                RET_CODE res = connection->write_clt();
                switch( res )
                {
                    case TRY_AGAIN:
                    {
                        modfd( m_epollfd, fd, EPOLLOUT );
                        break;
                    }
                    case BUFFER_EMPTY:
                    {
                        modfd( m_epollfd, srvfd, EPOLLIN );     //啥都没有，先读点东西吧
                        modfd( m_epollfd, fd, EPOLLIN );
                        break;
                    }
                    case IOERR:
                    case CLOSED:
                    {
                        free_conn( connection );
                        return CLOSED;
                    }
                    default:
                        break;
                }
                if( connection->m_srv_closed )
                {
                    free_conn( connection );
                    return CLOSED;
                }
                break;
            }
            default:
            {
                log( LOG_ERR, __FILE__, __LINE__, "%s", "other operation not support yet" );
                break;
            }
        }
    }
    else if( connection->m_srvfd == fd )//连接上发生的是服务器的事件
    {
        int cltfd = connection->m_cltfd;
        switch( type )
        {
            case READ:
            {
                RET_CODE res = connection->read_srv();
                switch( res )
                {
                    case OK:
                    {
                        log( LOG_DEBUG, __FILE__, __LINE__, "content read from server: %s", connection->m_srv_buf );
                    }
                    case BUFFER_FULL:
                    {
                        modfd( m_epollfd, cltfd, EPOLLOUT );    //缓缓吧 缓冲区都满了
                        break;
                    }
                    case IOERR:
                    case CLOSED:
                    {
                        modfd( m_epollfd, cltfd, EPOLLOUT );
                        connection->m_srv_closed = true;    //服务器关闭
                        break;
                    }
                    default:
                        break;
                }
                break;
            }
            case WRITE:
            {
                RET_CODE res = connection->write_srv();
                switch( res )
                {
                    case TRY_AGAIN:
                    {
                        modfd( m_epollfd, fd, EPOLLOUT );
                        break;
                    }
                    case BUFFER_EMPTY:
                    {
                        modfd( m_epollfd, cltfd, EPOLLIN );
                        modfd( m_epollfd, fd, EPOLLIN );
                        break;
                    }
                    case IOERR:
                    case CLOSED:
                    {
                        /*
                        if( connection->m_srv_write_idx == connection->m_srvread_idx )
                        {
                            free_conn( connection );
                        }
                        else
                        {
                            modfd( m_epollfd, cltfd, EPOLLOUT );
                        }
                        */
                        modfd( m_epollfd, cltfd, EPOLLOUT );
                        connection->m_srv_closed = true;
                        break;
                    }
                    default:
                        break;
                }
                break;
            }
            default:
            {
                log( LOG_ERR, __FILE__, __LINE__, "%s", "other operation not support yet" );
                break;
            }
        }
    }
    else
    {
        return NOTHING;
    }
    return OK;
}
