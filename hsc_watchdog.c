#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <string.h>
#include <dirent.h>
#include <libconfig.h>
#include <signal.h>
#include <time.h>

pid_t		daemonPID;
unsigned char wait = 1;
unsigned int time_seq;
unsigned char Visual = 0;
char Config_Path[100] = "hsc_watchdog.conf";

typedef struct proc_stat_t {
	char Name[100];			// имя процесса
	char State;				// состояние процесса R (running), Z (zombie),
							// S (sleeping ), T (stopped ), D (глубокий сон)
	char Exec[100];
	double Time;
	unsigned short Pid;		// номер процесса
	unsigned short Ppid;	//номер родительского процесса
} proc_stat_t;

proc_stat_t proc_stat;
proc_stat_t proc_list[100];
int Num;

void proc_print() {
	// вывод состояний процессов
	int i;
	printf("\n\n%s |%15s |%5s |%31s |%s |%4s |%4s\n","No","Name","Time"," Exec","State","Pid","PPid");
	for (i = 0; i < Num; i++) {
	   printf(" %d |%15s | ",i,	proc_list[i].Name);
	   printf(" %2.2f| ",			proc_list[i].Time);
	   printf(" %30s| ",			proc_list[i].Exec);
	   printf(" %4c|",				proc_list[i].State);
	   printf(" %4u ",			proc_list[i].Pid);
	   printf(" %4u \n",			proc_list[i].Ppid);
	}
}

long mtime() // функция счета времени, возвращает время в микросекундах
{
	struct timespec t;
	clock_gettime(CLOCK_REALTIME,&t);
	return (long)t.tv_sec*1000000 + t.tv_nsec/1000;
}

int readconfig(proc_stat_t list[]) { // чтение конфигурационного файла
	config_t cfg;
	config_setting_t *settings;
	config_init(&cfg);
	if(! config_read_file(&cfg, Config_Path)) {
			printf("hsc_watchdog.conf:%d - %s\n",  config_error_line(&cfg), config_error_text(&cfg));
		    config_destroy(&cfg);
		    return(EXIT_FAILURE);
	}
	settings = config_lookup(&cfg, "application.procwatch");
	if (settings !=NULL) {
		puts("\nconfig                                                     [  \e[32mOK  \e[0m]");
	}else {
		puts("\nconfig                                                     [  \e[31mNO  \e[0m]");
	}
	int i;
	for (i=0;i<config_setting_length(settings);i++) {
		config_setting_t *process = config_setting_get_elem(settings,i);
		const char* Name;
		const char* Time;
		const char* exec;

		config_setting_lookup_string(process,"Name",&Name);
		config_setting_lookup_string(process,"Time",&Time);
		config_setting_lookup_string(process,"Exec",&exec);

		memcpy(&list[i].Name,Name,strlen(exec));
		list[i].Time = atof(Time);
		memcpy(&list[i].Exec,exec,strlen(exec));
	}
	return i;
}

void time_init_(proc_stat_t list[]) {
	int i;
	double buf = 0;
	if(Num > 0) {
		for (i = 0; i < Num; i++) {
			if(list[i].Time != 0) {
				buf = list[i].Time;
				break;
			}
		}
		if (buf != 0) {
			for(i = 1; i < Num; i++) {
				if(list[i].Time != 0 && list[i].Time < buf) {
					buf = list[i].Time;
				}
			}
		}
		else {
			buf = 0.1;
		}
	}
	else {
		buf = 0.1;
	}
	time_seq = buf * 1000;
	printf("takt                                                       [\e[32m%dmls\e[0m]\n",time_seq);
	//time_seq = time_seq * 1000;
}

void strrm(char* str,const char* Del) { // функция удаления строки из строки
	char* bufstr;
	bufstr = strstr(str,Del);
	strcpy(bufstr,bufstr+strlen(Del));
}

void charrm(char* str){ // функция удаления символа из строки
	int i,j;
	for (i = 0; i < sizeof(str); i++) {
		if (str[i] == ' '){
			for (j = i; j < sizeof(str); j++){
				str[j]=str[j+1];
			}
			i--;
		}
	}
}

proc_stat_t proc_info(char *PID) { // поиск информации о процессе по номеру pid
	proc_stat_t proc;
	memset(&proc,0,sizeof(proc));
	FILE *f;
	char s[30];
	char c[100];
	sprintf(s,"/proc/%s/status",PID);
	if((f = fopen(s,"rb"))) {
			while (fgets(c,sizeof(c),f))
			{
				if (strstr(c,"State:")!=0) {
					if (strstr(c,"R")!=0) {
						proc.State = 'R'; // процесс running
					}else if (strstr(c,"Z")!=0) {
										proc.State = 'Z'; // процесс zombie
					}else if (strstr(c,"S")!=0) {
						proc.State = 'S'; // процесс спит
					}else if (strstr(c,"T")!=0) {
						proc.State = 'T'; // процесс остановлен
					}else if (strstr(c,"D")!=0) {
						proc.State = 'D'; // глубокий сон
					}else{
						proc.State = 'N'; // статус не найден
					}
				}

				if (strstr(c,"Name:")!=0) {
					strrm(c,"Name:");
					int i,j;
					j=0;
					for( i=0;i < sizeof(c);i++) {
						if (c[i]!=' ' && c[i]!='\n') {
							proc.Name[j]=c[i];
							j++;
						}
					}
				}
				if (strstr(c,"Pid:")!=0 && c[1] == 'i') {
					strrm(c,"Pid:");
					int i,j;
					j=0;
					char buffer[20];
					for( i=0;i < sizeof(c);i++) {
						if (c[i]!=' ' && c[i]!='\n') {
							buffer[j]=c[i];
							j++;
						}
					}
					proc.Pid = atoi(buffer);
				}
				if (strstr(c,"PPid:")!=0 && c[1] == 'P') {
					strrm(c,"PPid:");
					int i,j;
					j=0;
					char buffer[20];
					for( i=0;i < sizeof(c);i++) {
						if (c[i]!=' ' && c[i]!='\n') {
							buffer[j]=c[i];
							j++;
						}
					}
					proc.Ppid = atoi(buffer);
				}

			}
			fclose(f);
	}
	return proc;
}

void watchdog() {
	int i;
	memset(&proc_stat,0,sizeof(proc_stat));
	int n;	// поиск всех запущенных процессов по директориям proc/
	struct dirent **namelist;
	n = scandir("/proc", &namelist, 0, alphasort);
	if (n < 0) {
		perror("scandir");
	} else {
		while (n--) {
			if (namelist[n]->d_name[0] >= '0' && namelist[n]->d_name[0] <= '9') {
				proc_stat = proc_info(namelist[n]->d_name);	// функция сбора информации о процессе proc_stat_t proc_info(char* pid)
				for (i = 0; i < Num; i++) {	// поиск заданный процессов
					if (strstr(proc_stat.Name,proc_list[i].Name)!=0) {
						if (proc_list[i].Pid != proc_stat.Ppid) {
							proc_list[i].Pid = proc_stat.Pid;
							proc_list[i].Ppid = proc_stat.Ppid;
							proc_list[i].State = proc_stat.State;
						}
					}
				}
			}
		    free(namelist[n]);
	   }
	   free(namelist);

	   for (i = 0; i < Num; i++) {
		   if (!proc_list[i].Pid) {
			   proc_list[i].State = 'N';
		   }
		   if(proc_list[i].State != 'R' && proc_list[i].Exec) { // если статус процесса не R перезапускаем
			   system(proc_list[i].Exec);
		   }
	   }
	}
}

void stop() {
	wait = 0;
	printf("SIGINT");
}

int takt_seq = 0;

void handler(int signo, siginfo_t *info, void *context) {
	//printf("signal details: signal (%d), code (%d) \n",info->si_signo,info->si_code);
	watchdog();
	if(takt_seq >= 1000/time_seq) {
		proc_print();
		takt_seq = 0;
	}
	takt_seq++;
}

void my_timer(long time, void fun()) { // время в миллисекундах, функция
	struct timespec ts,tm,sleep;
	sigset_t mask;
	struct sigevent sev;
	timer_t timerid;
	struct sigaction sa;

	clock_getres(CLOCK_MONOTONIC, &ts);
	clock_gettime(CLOCK_MONOTONIC, &tm);
	printf("CLOCK_MONOTONIC res: [%ld] sec [%ld] nsec \n",ts.tv_sec,ts.tv_nsec);
	printf("system up time res: [%ld] sec [%ld] nsec \n",tm.tv_sec,tm.tv_nsec);


	sigemptyset(&mask);
	sigprocmask(SIG_SETMASK, &mask, NULL);

	sa.sa_flags = SA_SIGINFO;
	sigemptyset(&sa.sa_mask);
	sa.sa_sigaction = fun;
	if(sigaction(SIGRTMIN, &sa, NULL) == -1)	{
		perror("sigaction failed");
	}

	sev.sigev_notify = SIGEV_SIGNAL;
	sev.sigev_signo = SIGRTMIN;
	sev.sigev_value.sival_ptr = &timerid;
	if (timer_create(CLOCK_MONOTONIC, &sev, &timerid) == -1) {
	   	perror("timer_create");
	}

	struct itimerspec ival;
	ival.it_value.tv_sec = 1;
	ival.it_value.tv_nsec = 0;
	ival.it_interval.tv_sec = time / 1000;
	ival.it_interval.tv_nsec = (time % 1000) * 1000000;
	if (timer_settime(timerid,0,&ival,NULL) == -1) {
	   	perror("timer_settime");
	}
	sleep.tv_sec = 1;
	sleep.tv_nsec = 0;
	while(wait) clock_nanosleep(CLOCK_MONOTONIC, 0, &sleep, NULL);
}

int work_daemon(void ) { // старт процесса в режиме демона
	memset(&proc_stat,0,sizeof(proc_stat));

	if (Visual == 0) {
		daemonPID = fork();
		if (daemonPID == 0) {
			signal(SIGINT,stop);
			signal(SIGHUP,stop);
			signal(SIGTERM,stop);
			umask(0);
			chdir("/");	//переводим программу в корневую директорию
			setsid();
			close(STDIN_FILENO);
			close(STDOUT_FILENO);
			close(STDERR_FILENO);

			my_timer(time_seq,watchdog);

		}
	}else if (Visual == 1) {
		signal(SIGINT,stop);
		signal(SIGHUP,stop);
		signal(SIGTERM,stop);
		close(STDERR_FILENO);
		chdir("/");	// перевод директории программы в корень
		printf("My pid = %d\n",getpid());
		int i;
		printf("include processes: \n");	// вывод найденных процессов
		printf("%s |%15s |%5s |%s\n","No","Name","Time"," Exec");
		for (i = 0; i < Num; i++) {
			printf(" %d |%15s | ",i,proc_list[i].Name);
			printf(" %2.2f| ",proc_list[i].Time);
			printf(" %s \n",proc_list[i].Exec);
		}
		my_timer(time_seq,handler);
	}
	return EXIT_SUCCESS;
}

int parc_argv(int argc,char *argv[]){
	switch(argc) {
		case 2:
			if(strstr(argv[1],"-v")) {
				Visual = 1;
				printf("visual mod start \n");
			}
			if(strstr(argv[1],"--help")) {
				Visual = 2;
				printf("\n\nwatchdog program manual: \n\n \t -v - Visual mod \n \t"
						" -cf <path>/name.conf - your path to config file \n \t\t\t\t standart path = program folder\n"
						"\t -vcf - Visual mod + your path ( - cfv)\n\n");
			}
			break;
		case 3:
			if(strstr(argv[1],"-cf")) {
				if (strstr(argv[2],".conf") && sizeof(argv[2])<=sizeof(Config_Path)){
					memset(&Config_Path,0,strlen(Config_Path));
					memcpy(Config_Path,argv[2],strlen(argv[2]));
				//	printf("Config_Path = %s \n",Config_Path);
				}
			}else if(strstr(argv[1],"-vcf") || strstr(argv[1],"-cfv")) {
				if (strstr(argv[2],".conf") && sizeof(argv[2])<=sizeof(Config_Path)){
					memset(&Config_Path,0,strlen(Config_Path));
					memcpy(Config_Path,argv[2],strlen(argv[2]));
				//	printf("Config_Path = %s \n",Config_Path);
				}
				Visual = 1;
			}
			break;
		default:  break;
	}
	return 0;
}

int main(int argc,char *argv[]) {

	parc_argv(argc,argv);
	if (Visual <= 1) {
		Num = readconfig(proc_list); // считывание конфигурационного файла
		time_init_(proc_list);
		sleep(3);
		work_daemon();
	}

	return EXIT_SUCCESS;
}
