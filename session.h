#ifndef SESSION_H
#define SESSION_H

#include <string>

class Session{
private:
	std::string msg;
public:
	Session(std::string msg):msg(msg){}
	Session & operator<<(const char * buf);
	std::string get_response() const;
};

#endif
