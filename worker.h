#ifndef WORKER_H
#define WORKER_H

#include <sys/socket.h>
#include <cstring>
#include "config.h"

class Worker{
private:
	enum {MAX_THREADS=7,BUF_LEN=512,MAX_EVENTS=256};
	int ppid;
	const int m_socket;
	int e_poll;
	int num;
	Config & cfg;
	size_t rcv_socket(int socket,void * buf,size_t bufsize,int * fd);
public:
	Worker(int m_socket,int ppid, int num, Config & cfg):ppid(ppid),m_socket(m_socket),e_poll(-1),num(num),cfg(cfg){}
	void start();
	~Worker();
};

#endif
