#ifndef FDWRAPPER_H
#define FDWRAPPER_H

enum RET_CODE { OK = 0, NOTHING = 1, IOERR = -1, CLOSED = -2, BUFFER_FULL = -3, BUFFER_EMPTY = -4, TRY_AGAIN };
enum OP_TYPE { READ = 0, WRITE, ERROR };
int setnonblocking( int fd );   //设置非阻塞
void add_read_fd( int epollfd, int fd );    //添加读事件的描述符
void add_write_fd( int epollfd, int fd );
void removefd( int epollfd, int fd );   //从epoll中移除描述符
void closefd( int epollfd, int fd );    //关闭描述符
void modfd( int epollfd, int fd, int ev );  //编辑描述符

#endif
