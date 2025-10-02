/* includes generally required for socket operations */


#include        <arpa/inet.h>   /* inet(3) functions */
#include        <errno.h>
#include        <fcntl.h>               /* for nonblocking */
#include        <netdb.h>
#include        <signal.h>
#include        <stdlib.h>
#include        <string.h>
#include        <sys/stat.h>    /* for S_xxx file mode constants */
#include        <sys/uio.h>             /* for iovec{} and readv/writev */
#include	<sys/time.h>
#include        <unistd.h>
#include        <sys/wait.h>
#include        <sys/un.h>              /* for Unix domain sockets */
#include        <sys/uio.h>
#ifdef  __bsdi__
#include        <machine/endian.h>      /* required before tcp.h, for BYTE_ORDER */
#endif
#include        <netinet/tcp.h>         /* TCP_NODELAY */
#include        <netdb.h>               /* getservbyname(), gethostbyname() */



#include <time.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>

typedef struct {
	int max_fd;
	fd_set readfds;
	fd_set writefds;
	fd_set exceptfds;
	struct timeval timeout;
} socket_status;

#define MAXHOSTNAME 1024
#define MAXBUFSIZE 1024
#define COMMAND_TIMEOUT_SEC 120

#if 0

int send_command(char *command, char *reply, char *machine, int port);
void init_socket_status(int s,socket_status *s_status);
int read_data(int s, char *buf, int n);
int write_data(int s, char *buf, int n);   
int call_socket(char *hostname, u_short portnum);
int establish(u_short portnum);
int get_connection(int s);

#endif
