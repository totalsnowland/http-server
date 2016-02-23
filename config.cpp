#include "config.h"
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <stdexcept>

Config::Config(int argc, char * const * argv):is_log(false){
	char c;
	/*getopt - выделяет из параметров командной строки команды и их значения (если в форматной строке после имени параметра идет двоеточие)*/
	while((c=getopt(argc,argv,"h:p:d:l?"),c)!=-1)
		switch(c){ /*с - хранит имя параметра*/
			case 'h':
				ip_addr=optarg;/*optarg - хранит указатель на значение*/
				break;
			case 'p':
				port=0;
				for(int i=0,len=std::strlen(optarg);i<len;++i)
					if(std::isdigit(optarg[i]))
						port=port*10+optarg[i]-'0';
					else
						throw std::runtime_error("Не верно задан порт.");
				break;
			case 'd':
				directory=optarg;
				break;
			case 'l':
				is_log=true;
		};
}

void Config::help(std::ostream & os) const{
	os<<"Демон не запущен. Укажите параметры запуска.\n"
 	    "Для запуска сервера используйте:\n"
	    "\t<path_to>/final -h <ip> -p <port> -d <directory>\n"
	    "К примеру:\n"
	    "\t./final -h "<<ip_addr<< " -p "<<port<<" -d "<<directory<<std::endl;
}

std::ostream & operator<<(std::ostream & os,const Config & other){
	return os<<"Настройки конфигурации:\n"
		   "\t          адрес = "<<other.ip_addr<<std::endl
   		 <<"\t           порт = "<<other.port<<std::endl
 		 <<"\tрабочий каталог = "<<other.directory<<std::endl;
}


