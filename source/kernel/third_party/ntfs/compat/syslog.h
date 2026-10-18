#define LOG_ERR 3
#define LOG_WARNING 4
#define LOG_NOTICE 5
#define LOG_INFO 6
#define LOG_DEBUG 7
#define LOG_USER 8
#define LOG_PID 1
#define LOG_CONS 2
#define LOG_DAEMON 24
void openlog(const char*,int,int); void syslog(int,const char*,...); void closelog(void);
