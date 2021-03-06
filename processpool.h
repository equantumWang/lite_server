#ifndef PROCESSPOOL_H
#define PROCESSPOOL_H

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
#include <vector>
#include "log.h"
#include "fdwrapper.h"

using std::vector;

class process
{
public:
    process() : m_pid( -1 ){}   //只有父进程的Pid是-1，子进程的pid非负

public:
    int m_busy_ratio;
    pid_t m_pid;
    int m_pipefd[2];    //父子进程通信管道
};

template< typename C, typename H, typename M >
class processpool
{
private:
    processpool( int listenfd, int process_number = 8 );    //私有，用create()来创建实例
public:
    static processpool< C, H, M >* create( int listenfd, int process_number = 8 )
    {
        if( !m_instance )   //只允许一进程池实例存在
        {
            m_instance = new processpool< C, H, M >( listenfd, process_number );
        }
        return m_instance;
    }
    ~processpool()
    {
        delete m_instance;
        delete [] m_sub_process;
    }
    void run( const vector<H>& arg );   //进程池启动

private:
    void notify_parent_busy_ratio( int pipefd, M* manager );    //通知父进程当前子进程的忙碌度
    int get_most_free_srv();    //根据progress类的busy_ratio来遍历进程池选择最闲的进程，返回进程序号
    void setup_sig_pipe();      //设置信号管道
    void run_parent();
    void run_child( const vector<H>& arg );

private:
    static const int MAX_PROCESS_NUMBER = 16;   //进程池最大子进程数量
    static const int USER_PER_PROCESS = 65536;
    static const int MAX_EVENT_NUMBER = 10000;  //epoll事件数上限
    int m_process_number;                       //进程池中的进程总数
    int m_idx;                                  //进程池序号
    int m_epollfd;                              //epoll内核事件表的标识
    int m_listenfd;                             //监听socket
    int m_stop;                                 //子进程通过m_stop决定是否停止运行
    process* m_sub_process;
    static processpool< C, H, M >* m_instance;
};
template< typename C, typename H, typename M >
processpool< C, H, M >* processpool< C, H, M >::m_instance = NULL;

static int EPOLL_WAIT_TIME = 5000;
static int sig_pipefd[2];           //用于统一处理信号的管道
static void sig_handler( int sig )
{
    int save_errno = errno;
    int msg = sig;
    send( sig_pipefd[1], ( char* )&msg, 1, 0 );
    errno = save_errno;
}

static void addsig( int sig, void( handler )(int), bool restart = true )
{
    struct sigaction sa;
    memset( &sa, '\0', sizeof( sa ) );
    sa.sa_handler = handler;
    if( restart )
    {
        sa.sa_flags |= SA_RESTART;
    }
    sigfillset( &sa.sa_mask );
    assert( sigaction( sig, &sa, NULL ) != -1 );
}

template< typename C, typename H, typename M >
processpool< C, H, M >::processpool( int listenfd, int process_number )     //构造函数
    : m_listenfd( listenfd ), m_process_number( process_number ), m_idx( -1 ), m_stop( false )
{
    assert( ( process_number > 0 ) && ( process_number <= MAX_PROCESS_NUMBER ) );

    printf("processpool is created\n");
    m_sub_process = new process[ process_number ];  //创建n个进程子实例
    assert( m_sub_process );

    for( int i = 0; i < process_number; ++i )   //创建n个子进程，并建立与父进程的管道
    {
        int ret = socketpair( PF_UNIX, SOCK_STREAM, 0, m_sub_process[i].m_pipefd ); //创建的是全双工通道
        assert( ret == 0 );

        printf("sub_process_%d is created\n", i);
        m_sub_process[i].m_pid = fork();
        assert( m_sub_process[i].m_pid >= 0 );
        if( m_sub_process[i].m_pid > 0 )
        {
            close( m_sub_process[i].m_pipefd[1] );
            m_sub_process[i].m_busy_ratio = 0;
            continue;
        }
        else
        {
            close( m_sub_process[i].m_pipefd[0] );  
            m_idx = i;
            break;
        }
    }
}

template< typename C, typename H, typename M >
int processpool< C, H, M >::get_most_free_srv()
{
    int ratio = m_sub_process[0].m_busy_ratio;
    int idx = 0;
    for( int i = 0; i < m_process_number; ++i )
    {
        if( m_sub_process[i].m_busy_ratio < ratio )
        {
            idx = i;
            ratio = m_sub_process[i].m_busy_ratio;
        }
    }
    return idx;
}

template< typename C, typename H, typename M >
void processpool< C, H, M >::setup_sig_pipe()
{
    //创建epoll事件监听表和信号管道
    m_epollfd = epoll_create( 5 );  //注意：该处即是epoll创建的地方
    assert( m_epollfd != -1 );

    int ret = socketpair( PF_UNIX, SOCK_STREAM, 0, sig_pipefd );
    assert( ret != -1 );

    setnonblocking( sig_pipefd[1] );
    add_read_fd( m_epollfd, sig_pipefd[0] );

    addsig( SIGCHLD, sig_handler );
    addsig( SIGTERM, sig_handler );
    addsig( SIGINT, sig_handler );
    addsig( SIGPIPE, SIG_IGN );
}

template< typename C, typename H, typename M >
void processpool< C, H, M >::run( const vector<H>& arg )    //由idx决定运行父还是子
{
    printf("process idx: %d is running\n", m_idx);
    if( m_idx != -1 )
    {
        run_child( arg );
        return;
    }
    run_parent();
}

template< typename C, typename H, typename M >
void processpool< C, H, M >::notify_parent_busy_ratio( int pipefd, M* manager )
{
    int msg = manager->get_used_conn_cnt(); //这里是由mgr.h类来定义该函数，由具体实现决定
    send( pipefd, ( char* )&msg, 1, 0 );    
}

template< typename C, typename H, typename M >
void processpool< C, H, M >::run_child( const vector<H>& arg )
{
    setup_sig_pipe();

    int pipefd_read = m_sub_process[m_idx].m_pipefd[ 1 ];
    add_read_fd( m_epollfd, pipefd_read );

    epoll_event events[ MAX_EVENT_NUMBER ];

    printf("run_child m_idx: %d ; host name: %s \n",m_idx, arg[m_idx].m_hostname);

    M* manager = new M( m_epollfd, arg[m_idx] );
    assert( manager );

    int number = 0;
    int ret = -1;

    while( ! m_stop )
    {
        number = epoll_wait( m_epollfd, events, MAX_EVENT_NUMBER, EPOLL_WAIT_TIME );
        if ( ( number < 0 ) && ( errno != EINTR ) )
        {
            log( LOG_ERR, __FILE__, __LINE__, "%s", "epoll failure" );
            break;
        }

        if( number == 0 )
        {
            manager->recycle_conns();
            continue;
        }

        for ( int i = 0; i < number; i++ )
        {
            int sockfd = events[i].data.fd;
            if( ( sockfd == pipefd_read ) && ( events[i].events & EPOLLIN ) )
            {
                //从父子进程间管道读取数据并将结果存在变量Client中，若读取成功ret>0则说明有新连接到来
                int client = 0;
                ret = recv( sockfd, ( char* )&client, sizeof( client ), 0 );
                if( ( ( ret < 0 ) && ( errno != EAGAIN ) ) || ret == 0 ) 
                {
                    continue;
                }
                else
                {
                    struct sockaddr_in client_address;
                    socklen_t client_addrlength = sizeof( client_address );
                    int connfd = accept( m_listenfd, ( struct sockaddr* )&client_address, &client_addrlength );
                    if ( connfd < 0 )
                    {
                        log( LOG_ERR, __FILE__, __LINE__, "errno: %s", strerror( errno ) );
                        continue;
                    }
                    printf("new client fd %d! IP: %s PortL %d\n", connfd,
						inet_ntoa(client_address.sin_addr), ntohs(client_address.sin_port));

                    add_read_fd( m_epollfd, connfd );
                    C* conn = manager->pick_conn( connfd );
                    if( !conn ) 
                    {
                        closefd( m_epollfd, connfd );//当前没有空闲可用连接，从epoll中移除connfd
                        continue;
                    }
                    conn->init_clt( connfd, client_address );
                    notify_parent_busy_ratio( pipefd_read, manager );
                }
            }
            else if( ( sockfd == sig_pipefd[0] ) && ( events[i].events & EPOLLIN ) )
            {   //子进程的信号管道上有新消息到来
                int sig;
                char signals[1024];
                ret = recv( sig_pipefd[0], signals, sizeof( signals ), 0 );
                if( ret <= 0 )
                {
                    continue;
                }
                else
                {
                    for( int i = 0; i < ret; ++i )
                    {
                        switch( signals[i] )
                        {
                            case SIGCHLD:
                            {
                                pid_t pid;
                                int stat;
                                while ( ( pid = waitpid( -1, &stat, WNOHANG ) ) > 0 )
                                {
                                    continue;
                                }
                                break;
                            }
                            case SIGTERM:
                            case SIGINT:
                            {
                                m_stop = true;
                                break;
                            }
                            default:
                            {
                                break;
                            }
                        }
                    }
                }
            }
            else if( events[i].events & EPOLLIN )
            {   //其他可读数据，那就是客户请求到来，调用逻辑处理对象的process方法处理
                 RET_CODE result = manager->process( sockfd, READ );    //这里由mgr::process()实现
                 switch( result )
                 {
                     case CLOSED:
                     {
                         notify_parent_busy_ratio( pipefd_read, manager );  //使用全双工通道通知父进程当前进程负载
                         break;
                     }
                     default:
                         break;
                 }
            }
            else if( events[i].events & EPOLLOUT )
            {   //可写事件
                 RET_CODE result = manager->process( sockfd, WRITE );
                 switch( result )
                 {
                     case CLOSED:
                     {
                         notify_parent_busy_ratio( pipefd_read, manager );
                         break;
                     }
                     default:
                         break;
                 }
            }
            else
            {
                continue;
            }
        }
    }

    delete manager;
    close( pipefd_read );
    close( m_epollfd );
}

template< typename C, typename H, typename M >
void processpool< C, H, M >::run_parent()
{
    setup_sig_pipe();

    for( int i = 0; i < m_process_number; ++i ) //将来自子进程的管道信息加入epoll
    {
        add_read_fd( m_epollfd, m_sub_process[i].m_pipefd[ 0 ] );
    }

    add_read_fd( m_epollfd, m_listenfd );   //监听socket加入epoll

    epoll_event events[ MAX_EVENT_NUMBER ];
    int sub_process_counter = 0;
    int new_conn = 1;
    int number = 0;
    int ret = -1;

    while( ! m_stop )
    {
        number = epoll_wait( m_epollfd, events, MAX_EVENT_NUMBER, EPOLL_WAIT_TIME );
        if ( ( number < 0 ) && ( errno != EINTR ) )
        {
            log( LOG_ERR, __FILE__, __LINE__, "%s", "epoll failure" );
            break;
        }

        for ( int i = 0; i < number; i++ )
        {
            int sockfd = events[i].data.fd;
            if( sockfd == m_listenfd )  //有新连接到来
            {
                /* 原有的round robin方式的子进程分配方案（现已改成最低负载进程选择算法）
                int i =  sub_process_counter;
                do
                {
                    if( m_sub_process[i].m_pid != -1 )
                    {
                        break;
                    }
                    i = (i+1)%m_process_number;
                }
                while( i != sub_process_counter );
                
                if( m_sub_process[i].m_pid == -1 )
                {
                    m_stop = true;
                    break;
                }
                sub_process_counter = (i+1)%m_process_number;
                */
                int idx = get_most_free_srv();
                //通知idx进程有新连接到来。
                send( m_sub_process[idx].m_pipefd[0], ( char* )&new_conn, sizeof( new_conn ), 0 );
                log( LOG_INFO, __FILE__, __LINE__, "send request to child %d", idx );
            }
            else if( ( sockfd == sig_pipefd[0] ) && ( events[i].events & EPOLLIN ) )
            {   //信号通道上有子进程的信号到来，处理子进程信号，存储在signals中
                int sig;
                char signals[1024];
                ret = recv( sig_pipefd[0], signals, sizeof( signals ), 0 );
                if( ret <= 0 )
                {
                    continue;
                }
                else
                {
                    for( int i = 0; i < ret; ++i )
                    {
                        switch( signals[i] )
                        {
                            case SIGCHLD:   //子进程结束时，向父进程发送SIGCHLD信号，捕获之，并使用waitpid“彻底结束”子进程
                            {
                                pid_t pid;
                                int stat;
                                while ( ( pid = waitpid( -1, &stat, WNOHANG ) ) > 0 )
                                {
                                    for( int i = 0; i < m_process_number; ++i )
                                    {
                                        if( m_sub_process[i].m_pid == pid )
                                        {
                                            log( LOG_INFO, __FILE__, __LINE__, "child %d join", i );
                                            close( m_sub_process[i].m_pipefd[0] );  //关闭相应通信管道
                                            m_sub_process[i].m_pid = -1;            //标记-1表示已退出
                                        }
                                    }
                                }
                                m_stop = true;//若所有子进程都退出了，则父进程也退出
                                for( int i = 0; i < m_process_number; ++i )
                                {   
                                    if( m_sub_process[i].m_pid != -1 )  //只有还有一个子进程还没有退出，则继续
                                    {
                                        m_stop = false;
                                    }
                                }
                                break;
                            }
                            case SIGTERM:
                            case SIGINT:
                            {   //父进程收到终止信号，杀死所有子进程并等待他们结束
                                log( LOG_INFO, __FILE__, __LINE__, "%s", "kill all the clild now" );
                                for( int i = 0; i < m_process_number; ++i )
                                {
                                    int pid = m_sub_process[i].m_pid;
                                    if( pid != -1 )
                                    {
                                        kill( pid, SIGTERM );
                                    }
                                }
                                break;
                            }
                            default:
                            {
                                break;
                            }
                        }
                    }
                }
            }
            else if( events[i].events & EPOLLIN )
            {   //除以上之外的可读事件，即是子进程通知父进程关于子进程当前的负载情况。（从父子通信管道来的信息）
                int busy_ratio = 0;
                ret = recv( sockfd, ( char* )&busy_ratio, sizeof( busy_ratio ), 0 );
                if( ( ( ret < 0 ) && ( errno != EAGAIN ) ) || ret == 0 )
                {
                    continue;
                }
                for( int i = 0; i < m_process_number; ++i )
                {
                    if( sockfd == m_sub_process[i].m_pipefd[0] )
                    {
                        m_sub_process[i].m_busy_ratio = busy_ratio;
                        break;
                    }
                }
                continue;
            }
        }
    }

    //父进程退出，关闭父进程对各个子进程的通信通道，并关闭epoll标识符
    for( int i = 0; i < m_process_number; ++i )
    {
        closefd( m_epollfd, m_sub_process[i].m_pipefd[ 0 ] );
    }
    close( m_epollfd );
}

#endif
