#ifndef CONFIG_H
#define CONFIG_H

#include <iostream>

class Config { 
private:
	const char * ip_addr;
	int port;                                                                                                                                     
	const char * directory;
	bool is_log;
public:	
	 /*конструктор экземпляра*/
	 Config():ip_addr("0.0.0.0"),port(80),directory("www"),is_log(false){}
	 Config(int argc,char * const * argv);
	 /*методы*/
	 int get_port() const {return port;}
	 const char * get_addr() const {return ip_addr;}
	 const char * work_directory() const {return directory;}
	 void help(std::ostream & os) const;
	 friend std::ostream & operator<<(std::ostream & os,const Config & other);
	 Config & operator<<(const char * msg) {
		 if(is_log)
			 std::cout << msg;
		 return *this;
	 };
	 Config & operator<<(int msg) {
	 		 if(is_log)
	 			 std::cout << msg;
	 		 return *this;
	 	 };
};

#endif
