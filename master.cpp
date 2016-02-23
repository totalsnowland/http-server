#include "master.h"
#include "worker.h"
#include "config.h"
#include <unistd.h>
#include <stdexcept>
#include <iostream>
#include <string>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <sys/epoll.h>
#include <signal.h>
#include <wait.h>
#include <functional>
#include <ctime>
#include <cstdlib>
#include <omp.h>


extern Master * p_mst;
void hndl_SIGCHLD(int signum, siginfo_t * inf, void * content){
	/*забираем код возврата потомка*/
	int res;
	do{
		res=-1;
		int w_pid=waitpid(-1,&res,WNOHANG);
		if(res!=-1){
			p_mst->reload_worker(w_pid);
		}
	}while(res!=-1);
}

/*определение методов Master*/
Master::Master(Config & cfg):cfg(cfg),p_m_workers(nullptr){
	if((pid=setsid(),pid)==-1) /*отделение от текущего сеанса, путем создания нового*/
		throw std::runtime_error("Не удалось создать отдельный сеанс.");
	if(chdir(cfg.work_directory())==-1){
		std::string str_err="Не удалось зайти в рабочий каталог: ";
		str_err+=cfg.work_directory();
		throw std::runtime_error(str_err);
	}
}

Master::~Master(){
	if(pid==getpid()){
		/*закрываем мастер сокет и epoll только, если это процесс мастера*/
		shutdown_close(m_socket);
		epoll_close();
		p_m_workers->killall();
	}
	delete p_m_workers;
}

void Master::epoll_close(){
	if(-1!=e_poll){
		close(e_poll);
		e_poll=-1;
	}
}

int Master::set_repeated(int socket) const{
	/*повторное использование сокета*/
	int optval = 1;                                                                                                  
	return setsockopt(socket,SOL_SOCKET,SO_REUSEADDR,&optval,sizeof(optval));  
}

int Master::set_non_block(int socket) const{
	int flags;  
	#if defined(O_NONDLOCK)                                                                                                  
		if(-1==(flags = fcntl(socket,F_GETFL,0)))       
			flags = 0;                                                                                               
		return fcntl(socket,F_SETFL, flags | O_NONBLOCK);
	#else
	        flags = 1;
	        return ioctl(socket,FIONBIO,&flags);
	#endif
}

void Master::shutdown_close(int & socket){
	if(-1!=socket){
		shutdown(socket,SHUT_RDWR);
		close(socket);
		socket=-1;
	}
}

void Master::start(){
	/*обработчик сигнала завершения потомка*/
	struct sigaction acc,old_acc;
	acc.sa_flags=SA_SIGINFO;
	acc.sa_sigaction=hndl_SIGCHLD;
	sigaction(SIGCHLD,&acc,&old_acc);

	/*делаем мастер сокет*/
	m_socket=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
	
	/*биндим*/
	sockaddr_in saddr;
	saddr.sin_family=AF_INET;
	saddr.sin_port=htons(cfg.get_port());
	inet_pton(AF_INET,cfg.get_addr(),&(saddr.sin_addr));
	int res=bind(m_socket,(sockaddr*)&saddr,sizeof(saddr));
	if(-1==res){
		std::string str_err="Не удалось выполнить bindi: ";
		str_err+=cfg.get_addr();
		str_err+=":"+cfg.get_port();
		throw std::runtime_error(str_err);
	}
	
	/*начинаем слушать порт на указанном в конфигурации адресе*/
	res=listen(m_socket,SOMAXCONN);
	if(-1==res){
		std::string str_err="Не удалось выполнить listen: ";
		str_err+=cfg.get_addr();
		str_err+=":"+cfg.get_port();
		throw std::runtime_error(str_err);
	}
	
	/*включаем режим повторного использования сокета*/
	res=set_repeated(m_socket);
	
	/*делаем не блокирующий сокет*/
	res=set_non_block(m_socket);
	if(-1==res)
		throw std::runtime_error("Не удалось перевести мастер сокет в неблокирующий режим.");
	
	/*создаем в ядре epoll (изночально пустой)*/
	e_poll=epoll_create1(0);
	if(-1==e_poll)
		throw std::runtime_error("Не удалось создать в ядре epoll.");

	/*добавляем в epoll мастер сокет*/
	epoll_event event;
	event.data.fd=m_socket;
	event.events=EPOLLIN; //есть не прочитанные данные
	res=epoll_ctl(e_poll,EPOLL_CTL_ADD,m_socket,&event);
	if(-1==res)
		throw std::runtime_error("Не удается добавить мастер соект в epoll.");

	/*запускаем отдельный процесс-обработчик*/
	p_m_workers=new m_worker(cfg);
}

void Master::begin_loop(){
	if(pid!=getpid())
		return;//потомки не заходят в цикл

	/*массив, для хранения событий*/
	epoll_event m_enents[MAX_EVENTS];
	int cnt_events;	
	bool is_repeat=true;
	/*параллельная секция*/
	#pragma omp parallel num_threads(MAX_THREADS)
	{
		/*выделяем один поток, который будет выполнять главную функцию*/
		bool is_master=false;
		#pragma omp master
		is_master=true;
		
		/*запускаем цикл (потоки будут засыпать и пробуждаться)*/
		do{
			/*один потоко получает из ядра события, для текущего epoll*/
			if(is_master)
				cnt_events=epoll_wait(e_poll,m_enents,MAX_EVENTS,-1);


			/*остальные ждут на барьере*/
			#pragma omp barrier

			/*все актуализируют значение своей общей переменной*/
			//#pragma omp flush(cnt_events)

			/*совместно обрабатываемый цикл*/
			#pragma omp for
			for(int i=0;i<cnt_events;++i){
				if(m_enents[i].data.fd==m_socket){                                  /*событие на мастер сокете*/
					/*пытаемся принять соединения*/
					sockaddr_in saddr;
					socklen_t saddr_len=sizeof(sockaddr_in);
					int s_socket=accept(m_socket,(sockaddr*)&saddr,&saddr_len);
					if(-1!=s_socket){
						/*отправляем сокет одному из worker*/
						send_socket(p_m_workers->get_w_socket(),(void*)&s_socket,sizeof(s_socket),s_socket);
					}
				}else{/*сломался worker*/
					if(m_enents[i].events & EPOLLERR || m_enents[i].events & EPOLLHUP){
						int fd=m_enents[i].data.fd;
						/*удаляем из epoll*/
						epoll_ctl(e_poll,EPOLL_CTL_DEL,fd,nullptr);
						/*закрываем мастер сокет worker*/
						shutdown_close(fd);
						/*убиваем процесс*/
						p_m_workers->kill_by_socket(m_enents[i].data.fd);
					}
				}
			}

			/*потомок выходит из цикла*/
			#pragma omp flush(is_repeat)
		}while(is_repeat);

	}
}

size_t Master::send_socket(int socket, void * buf, size_t buf_len, int fd)const{
	struct iovec iov;
	iov.iov_base=buf;
	iov.iov_len =buf_len;//нужно передать хотя бы 1 байт информации
	
	struct msghdr msg;
	msg.msg_name   =nullptr;
	msg.msg_namelen=0;
	msg.msg_iov    =&iov;//здесь может стоять адрес массива, тогда его элементы будут отправлены вместе
	msg.msg_iovlen =1;   //передаем ровно один элемент (для массива нужно указать его размер)

	union {
		struct cmsghdr cmsghdr;
		char   control[CMSG_SPACE(sizeof(int))];

	} cmsgu;
	struct cmsghdr * cmsg;

	if(fd!=-1){
		msg.msg_control   =cmsgu.control;
		msg.msg_controllen=sizeof(cmsgu.control);	
		
		cmsg=CMSG_FIRSTHDR(&msg);
		cmsg->cmsg_len  =CMSG_LEN(sizeof(int));
		cmsg->cmsg_level=SOL_SOCKET;
		cmsg->cmsg_type =SCM_RIGHTS;

		*((int*)CMSG_DATA(cmsg))=fd;
	}else{
		/*не указан передаваемый дескриптор*/
		msg.msg_control   =nullptr;
		msg.msg_controllen=0;
	}
	return sendmsg(socket,&msg,0);
}

/*Описание методов класса m_worker*/
Master::m_worker::m_worker(Config & cfg){
	std::srand(time(0));
	bool is_repeat=true;
	for(int i=0;is_repeat && i<Master::COUNT_WORKER;++i){
		is_repeat=init(i);
	}
}
bool Master::reload_worker(int w_pid){
	return p_m_workers->reload(w_pid);
}
bool Master::m_worker::init(int i){
	/*пара сокетов (для обмена с worker)*/
	int fd[2];
	int res=socketpair(AF_UNIX,SOCK_STREAM,0,fd);
	if(-1==res)
		throw std::runtime_error("Не удалось создать пару сокетов, для связи с worker.");

	/*разделение*/
	m[i].w_pid=fork();
	if(m[i].w_pid){
		/*закрываем один из сокетов, т.к. родитель будет работать с одним концом*/
		close(fd[1]);
		m[i].w_socket=fd[0];
		/*делаем не блокирующие сокеты*/
		res=p_mst->set_non_block(m[i].w_socket);
		if(-1==res)
		    throw std::runtime_error("Не удалось перевести мастер сокет worker в неблокирующий режим.");

		/*добавляем в epoll сокет worker*/
		epoll_event event;
		event.data.fd=m[i].w_socket;
		event.events=EPOLLIN; //есть не прочитанные данные
		res=epoll_ctl(p_mst->e_poll,EPOLL_CTL_ADD,m[i].w_socket,&event);
		if(-1==res)
			throw std::runtime_error("Не удается добавить сокет worker в epoll.");

		return true;
	}else{
		/*закрываем один из сокетов, т.к. потомок будет работать с другим концом*/
		close(fd[0]);
		/*потомок обслуживает соединения*/
		Worker wr(fd[1],p_mst->pid,i,p_mst->cfg);

		wr.start(); /*здесь потомок будет уже работать*/
		return false;
	}
}

bool Master::m_worker::reload(int w_pid){
	for(int i=0;i<COUNT_WORKER;++i)
		if(m[i].w_pid==w_pid){
			m[i].w_pid=-1;
			return init(i); /*повторная инициализация worker*/
		}
	return false;
}

int Master::m_worker::get_w_socket(){
	/*случайный выбор worker*/
	int i;
	do{
		i= rand() % COUNT_WORKER;
	}while(m[i].w_socket==-1);

	return m[i].w_socket;
}

void Master::m_worker::kill_by_socket(int w_socket){
	for(int i=0;i<COUNT_WORKER;++i)
			if(m[i].w_socket==w_socket){
				kill(m[i].w_pid,SIGKILL);
				m[i].w_socket=-1;
				break;
			}
}

void Master::m_worker::killall(){
	for(int i=0;i<COUNT_WORKER;++i){
		if(m[i].w_pid!=-1){
			/*закрываем мастер сокет worker*/
			p_mst->shutdown_close(m[i].w_socket);
			/*убиваем worker*/
			kill(m[i].w_pid,SIGKILL);
			m[i].w_pid=-1;
		}
	}
}
