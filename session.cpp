#include "session.h"
#include <stdio.h>
#include <iostream>

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
	//std::cout<<msg<<std::endl;
	/*определяем метод*/
	size_t pos=msg.find(' ',0);
	if(pos>0 && "GET"==msg.substr(0,pos)){
		/*определяем запрашиваемый файл*/
		size_t posf=msg.find(' ',pos+1);	
		std::string path="."+msg.substr(pos+1,posf-pos-1);//т.к. мы в каталоге www, то добавим току, для ссылки на текущий каталог
		path=path.substr(0,path.find('?',0));//отрежем все параметры, т.к. не обрабатываются они
		/*проверяем наличие файла*/
		if(path[path.length()-1]!='/'){
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
			}
		}
	}
	//std::cout<<res<<std::endl;
	return res;
}
