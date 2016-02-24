#include "session.h"
#include <stdio.h>
#include <unistd.h>
#include <iostream>
#include <errno.h>
#include <cstring>

Session & Session::operator<<(const char * buf){
 	msg+=buf;		
	return *this;
}

std::string Session::get_response()const{
	/*404*/
	std::string res;
	res="HTTP/1.0 404 NOT FOUND\r\n"
	    "Content-Type: text/html\r\n"
	    "\r\n";
	std::cout<<msg<<std::endl;
	size_t pos=msg.find(' ',0);
	/*определяем метод*/
	if(pos>0 && "GET"==msg.substr(0,pos)){
		/*определяем запрашиваемый файл*/
		size_t posf=msg.find(' ',pos+1);	
		std::string path=msg.substr(pos+1+1,posf-pos-1-1);//т.к. мы в каталоге www, то добавим точку, для ссылки на текущий каталог
		pos=path.find('?',0);
		if(pos!=-1)
			path=path.substr(0,path.find('?',0));//отрежем все параметры, т.к. не обрабатываются они
	//	std::cout<<"Окончательный путь path = \""<<path<<"\""<<std::endl;

		/*проверяем наличие файла*/
		if(path.length() && path[path.length()-1]!='/'){
			FILE * file=fopen(path.c_str(),"r");
			if(file!=NULL){
				int n,len=0;
		       	 	std::string res_;	
				while ((n = std::fgetc(file)) != EOF) { // standard C I/O file reading loop
					res_+=(char)n;
					++len;
				}
				fclose(file);
				/*200*/
 				res= "HTTP/1.0 200 OK\r\n"
            		     	     "Content-length: ";
				res+=std::to_string(len);
				res+="\r\n"
 	    		     	     "Connection: close\r\n"
	    		    	     "Content-Type: text/html\r\n"
	    		     	     "\r\n";
				res+=res_;
			}/*else{
				char buf[512];
				getcwd(buf,512);
				std::cout<<"Не удалось открыть на чтение файл:\n"<<strerror(errno)<<std::endl<<buf<<std::endl;
			}*/
		}
	}
//	std::cout<<res<<std::endl;
	return res;
}
