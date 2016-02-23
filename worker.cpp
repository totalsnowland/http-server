#include "worker.h"
#include "master.h"
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
#include <string>
#include <sys/epoll.h>
#include <stdexcept>
#include <iostream>
#include <omp.h>
#include "session.h"

extern Master * p_mst;

Worker::~Worker(){
	
}

size_t Worker::rcv_socket(int socket,void * buf,size_t bufsize,int * p_fd){
	size_t size;
	if(p_fd!=nullptr){
		struct iovec iov;
		iov.iov_base=buf;
		iov.iov_len =bufsize;

		struct cmsghdr * cmsg;

		struct msghdr msg;
		msg.msg_name   =nullptr;
		msg.msg_namelen=0;
		msg.msg_iov    =&iov;//адрес первого элемента (если массив)
		msg.msg_iovlen =1;   //будем получать один элемент

		union {
			struct cmsghdr cmsghdr;
			char   control[CMSG_SPACE(sizeof(int))];
		} cmsgu;

		msg.msg_control   =cmsgu.control;
		msg.msg_controllen=sizeof(cmsgu.control);

		size = recvmsg(socket,&msg,0);

		if(size){
			cmsg = CMSG_FIRSTHDR (&msg);
			if(cmsg->cmsg_level==SOL_SOCKET
			   && cmsg->cmsg_type==SCM_RIGHTS)
				*p_fd=*((int*)CMSG_DATA(cmsg));
			else /*что-то не то или нет прав*/
				*p_fd=-1;


		}else /*дексриптор не пришел*/
			*p_fd=-1;
	}else{ /*передан нулевой указатель*/

	}

	return size;
}

void Worker::start(){
	{
		/*создаем в ядре epoll (изночально пустой)*/
		e_poll=epoll_create1(0);
		if(-1==e_poll)
			throw std::runtime_error("Не удалось создать в ядре epoll для worker.");
		int res=p_mst->set_non_block(m_socket);
		if(-1==res)
			throw std::runtime_error("Не удалось перевести слейв сокет worker в неблокирующий режим.");
	
		/*добавляем в epoll мастер сокет worker*/
		epoll_event event;
		event.data.fd=m_socket;
		event.events =EPOLLIN; //есть не прочитанные данные
		res=epoll_ctl(e_poll,EPOLL_CTL_ADD,m_socket,&event);
		if(-1==res)
	        throw std::runtime_error("Не удается добавить мастер сокет worker в epoll.");
	}

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
							                        

            /*совместно обрабатываемый цикл*/
	    #pragma omp for
            for(int i=0;i<cnt_events;++i){
		if(m_enents[i].data.fd==m_socket){                                  /*событие на мастер сокете*/
			if(m_enents[i].events & EPOLLERR || m_enents[i].events & EPOLLHUP){
				/*утрачена связь с мастер процессом*/
				is_repeat=false;
			}else{
				/*пытаемся принять новый сокет*/
				int s_socket=-1;
				char buf[BUF_LEN];
				rcv_socket(m_socket,buf,BUF_LEN,&s_socket);
				if(-1!=s_socket){
					/*перевод сокета в неблокирующий режим*/
					int res=p_mst->set_non_block(s_socket);
					if(-1==res) /*не удалось*/
						p_mst->shutdown_close(s_socket);
					else{
						/*регистрация слейв сокета в epoll worker*/
						epoll_event event_;
						event_.data.fd=s_socket;//здесь указан сокет с которым будет связано событие на нашем дескрипторе (т.е. можно по дури указать другой дескриптор)
						event_.events=EPOLLIN; /*есть не прочитанные*/
						int res=epoll_ctl(e_poll,EPOLL_CTL_ADD,s_socket,&event_);
						if(res==-1)/*не удалась регистрация*/
							p_mst->shutdown_close(s_socket);
					}
				}
			 }
		}else if(m_enents[i].events & EPOLLERR || m_enents[i].events & EPOLLHUP){ /*событие на слейв сокете (ошибка или разрыв)*/
		    	epoll_ctl(e_poll,EPOLL_CTL_DEL,m_enents[i].data.fd,nullptr); /*удаляем регистрацию*/
		    	shutdown(m_enents[i].data.fd,SHUT_RDWR);
			close(m_enents[i].data.fd);
		}else{                                                                    /*остальные события на слейв сокете*/ 
			/*чтение из сокета*/
			std::string str_request, str_respons;
			int fd=m_enents[i].data.fd;
			//do{
				char rbuf[BUF_LEN];
				int size=recv(fd,rbuf,BUF_LEN-1,MSG_NOSIGNAL);
				if(0==size && errno != EAGAIN){
					/*закрываем соединение*/
					p_mst->shutdown_close(fd);
				}else if(size>0){
					rbuf[size]='\0';
					str_request+=rbuf;
					Session ss(str_request);
					str_respons=ss.get_response();
					/*запись в сокет*/
					for(int i=0,l=str_respons.length();i<l;i += BUF_LEN){
						int len = i+BUF_LEN<l ? BUF_LEN : l-i;
						send(fd,str_respons.substr(i,len).c_str(),len,MSG_NOSIGNAL);
					}
					/*закрываем соединение*/
					int res=epoll_ctl(e_poll,EPOLL_CTL_DEL,m_enents[i].data.fd,nullptr); /*удаляем регистрацию*/
					res=shutdown(m_enents[i].data.fd,SHUT_RDWR);
					res=close(m_enents[i].data.fd);
				}
			//}while(!is_ready);	
		}
           }
           /*потомок выходит из цикла*/
	    #pragma omp flush(is_repeat)
	}while(is_repeat);
    }
}
