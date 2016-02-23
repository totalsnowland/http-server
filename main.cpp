#include <iostream>
#include <unistd.h>
#include "master.h"
#include "config.h"
#include <stdexcept>

Master * p_mst;

int main(int argc, char * const * argv){
	/*разбор параметров командной строки*/
	if(argc<7){
		Config cfg;
		cfg.help(std::cout);
	}else{
		Config cfg(argc,argv);
		std::cout<<cfg;

		/*разделение на два процесса (родитель вернет управление, а потомок будет выполняться независимо)*/
		int pid=fork();
		if(pid==-1)
			throw std::runtime_error("Не удалось сделать fork(), для запуска демона.");
		else if(pid)
			/*родитель просто запускает потомка и закрывается - это его единственная задача*/
			cfg<<"Запуск демона...\n";
		else{
			/*конфигурирование демона*/
			Master mst(cfg);
			p_mst=&mst;
			/*инициализация демона*/
			mst.start();
			/*запуск цикла работы демона*/
			mst.begin_loop();
			cfg<<"Демон остановился.\n";
		}
	}

	return 0;
}
