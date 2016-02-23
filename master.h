#ifndef MASTER_H
#define MASTER_H

#include <sys/socket.h>
#include <signal.h>
#include "config.h"
#include <cstring>

class Master{
private:
	enum {MAX_THREADS=2, MAX_EVENTS=256, COUNT_WORKER=4};

	struct inf_worker{
		int w_socket=-1;
		int w_pid   =-1;
	};

	friend class m_worker;
	class m_worker{
	private:
		inf_worker m[COUNT_WORKER];
		bool init(int i);
	public:
		m_worker(Config & cfg);
		bool reload(int w_pid);
		void kill_by_socket(int w_socket);
		int get_w_socket();
		void killall();
	};

	int pid; 
	Config & cfg;
	int m_socket=-1;
	int e_poll=-1;
	m_worker * p_m_workers;
	/*закрытые методы*/
	int set_repeated(int socket) const;
	void epoll_close();
	size_t send_socket(int socket, void * buf, size_t buf_len, int fd)const;
public:
	Master(Config & cfg);
	~Master();
	void start();
	void begin_loop();
	friend void hndl_SIGCHLD(int signum, siginfo_t * inf, void * content);
	Config & get_cfg() {return cfg;}
	int set_non_block(int socket) const;
	void shutdown_close(int & socket);
	bool reload_worker(int w_pid);
};


#endif

