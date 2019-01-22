#include <signal.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/types.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PRO_SUCCESS             0
#define PRO_FAILED              -1
#define OBJ_NULL                -1
#define COMP_EQ                 0
#define COMP_GT                 1
#define COMP_LT                 -1
#define NODE_NAME_LEN   20
#define BUF_MAX_LEN             1024
#define BUF_HEAD_LEN    4
#define BUF_MAX 1024*32

#define SRV_PORT 65184
#define CLI_PORT 8666
#define CLI_IP "168.2.6.84"
#define CLI_IPS "168.3.26.112 168.5.130.188"

static int serv_fd;
static int cli_port;
static long thread_cnt=0;
static long thread_id=0;

int tcp_send(int socket_fd, char *buf, int len);
void tcp_close(int socket_fd);

/*****************************************************
	网络通讯函数
*****************************************************/
static int _createsocket()
{
	int socket_fd;
	socket_fd = socket(AF_INET, SOCK_STREAM, 0);
	if(socket_fd < 0)
	{
		perror("socket error");
		return PRO_FAILED;
	}
	return socket_fd;
}

int tcp_listen(int port)
{
	int socket_fd;
	struct sockaddr_in serv_addr;
	if((socket_fd = _createsocket()) == PRO_FAILED)
	{
		return PRO_FAILED;
	}

	serv_addr.sin_family=AF_INET;
	serv_addr.sin_addr.s_addr=htonl(INADDR_ANY);
	serv_addr.sin_port=htons(port);

	if(bind(socket_fd,(struct sockaddr *)&serv_addr,sizeof(serv_addr)) < 0)
	{
		perror("socket bind error");
		return PRO_FAILED;
	}
	
	listen(socket_fd, 20);
	return socket_fd;
}

int tcp_connect(char *serv_ip, int port)
{
	int socket_fd;
	struct sockaddr_in serv_addr;

	if((socket_fd = _createsocket()) == PRO_FAILED)
	{
		return PRO_FAILED;
	}

	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(port);
	serv_addr.sin_addr.s_addr = inet_addr(serv_ip);
	inet_aton(serv_ip, &serv_addr.sin_addr);
	memset(serv_addr.sin_zero, 0, sizeof(serv_addr.sin_zero));

	if(connect(socket_fd, (struct sockaddr *)&serv_addr, sizeof(struct sockaddr)) == -1)
	{
		perror("connect error");
		return PRO_FAILED;
	}

	return socket_fd;
}

int tcp_accept(int socket_fd)
{
	int client_fd;
	struct sockaddr_in clit_addr;
	char prts[100];

#ifdef AIX
	unsigned long len;
#else
	int len;
#endif

	len = sizeof(struct sockaddr);

	client_fd = accept(socket_fd, (struct sockaddr*)&clit_addr, &len);
	if(client_fd < 0)
	{
		perror("client connect error");
		return PRO_FAILED;
	}

	cli_port=ntohs(clit_addr.sin_port);

#ifdef CLI_IPS
	if(strstr(CLI_IPS,inet_ntoa(clit_addr.sin_addr))==NULL)
	{
		tcp_send(client_fd,"IP Error!",10);
		tcp_close(client_fd);
		sprintf(prts,"IP ERR[%s]",inet_ntoa(clit_addr.sin_addr));
		perror(prts);
		client_fd=-1;
	}
#endif
	return client_fd;
}

/*发送数据*/
int tcp_send(int socket_fd, char *buf, int len)
{
	int rtn;

	/*发送报文*/
	rtn = send(socket_fd, buf, len, 0);
	if(rtn < 0)
	{
		perror("send buf error");
		return rtn;
	}
	/*printf("send buf[%d]:[%s]\n", strlen(buf), buf);*/
	return rtn;
}

/*接收数据*/
int tcp_recv(int socket_fd, char *buf, int len)
{
	int rtn;

	rtn = recv(socket_fd, buf, len, 0);
	if(rtn <= 0)
	{
		perror("recv error");
		return rtn;
	}
	/*printf("recv buf[%d]:[%s]\n", strlen(buf), buf);*/
	return rtn;
}

void tcp_close(int socket_fd)
{
	if(close(socket_fd)!=0)
	{
		perror("close socket error");
	}
}

/*****************************************************
	信号处理函数
*****************************************************/
void sig_chld()
{
	int ret,status;
	status = 0;
	ret = waitpid(-1, &status, WNOHANG);
	if(ret < 0)
	{
		printf("pid [%d] waitpid error [%d]\n", getpid(), ret);
	}
	else
	{
		printf("pid [%d] waitpid [%d] [%d]\n", getpid(), ret, status);
	}
}

void sig_term()
{
	tcp_close(serv_fd);
	exit(0);
}

/*****************************************************
	通讯子函数
*****************************************************/
void TransferData(int clit_fd, int proxy_fd)
{
	int ret;
	int recv_len1,recv_len2,send_len1,send_len2,tmp_len;
	char recv_buf[BUF_MAX], send_buf[BUF_MAX];
	int max_fd;
	fd_set read_fds,write_fds;
	struct timeval stTimeOut;

	if(proxy_fd > clit_fd)
	{
		max_fd = proxy_fd;
	}
	else
	{
		max_fd = clit_fd;
	}

	printf("max_fd:[%d]\n", max_fd);

	recv_len1 = 0;
	recv_len2 = 0;
	send_len1 = 0;
	send_len2 = 0;
	memset(recv_buf, 0, sizeof(recv_buf));
	memset(send_buf, 0, sizeof(send_buf));

	while(1)
	{
		/*添加文件描述符到fd_set*/
		FD_SET(0, &read_fds);
		FD_SET(0, &write_fds);
		FD_SET(proxy_fd, &read_fds);
		FD_SET(proxy_fd, &write_fds);
		FD_SET(clit_fd, &read_fds);
		FD_SET(clit_fd, &write_fds);
		stTimeOut.tv_sec=15;
		stTimeOut.tv_usec=0;

		ret = select(max_fd+1, &read_fds, &write_fds, NULL, &stTimeOut);
		if(ret==0)
		{
			perror("select time out");
			break;
		}
		else if(ret < 1)
		{
			perror("select error");
			printf("select ret:[%d]\n", ret);
			break;
		}

		printf("select ret:[%d]\n", ret);

		if(FD_ISSET(proxy_fd, &read_fds))
		{
			printf("proxy_fd read ready\n");
			if(recv_len1 < BUF_MAX)
			{
				tmp_len = tcp_recv(proxy_fd, recv_buf+recv_len1, BUF_MAX-recv_len1);
				if(tmp_len <= 0)
				{
					break;
				}
				recv_len1 += tmp_len;
			}
			/*printf("recv from proxy:[%s]\n", recv_buf);*/
		}

		if(FD_ISSET(clit_fd, &write_fds))
		{
			printf("clit_fd write ready\n");
			send_len1 = 0;
			ret = 0;
			while(recv_len1 > 0)
			{
				tmp_len = tcp_send(clit_fd, recv_buf+send_len1, recv_len1);
				if(tmp_len == 0)
				{
					break;
				}
				else if(tmp_len < 0)
				{
					ret = 1;
					break;
				}
				send_len1 += tmp_len;
				recv_len1 -= tmp_len;
			}
			if(ret == 1)
			{
				break;
			}
			else
			{
				if(send_len1 > 0 && recv_len1 > 0)
				{
					memcpy(recv_buf, recv_buf+send_len1, recv_len1);
					memset(recv_buf+recv_len1, 0, BUF_MAX-recv_len1);
				}
				else
				{
					memset(recv_buf, 0, sizeof(recv_buf));
				}
			}
		}

		if(FD_ISSET(clit_fd, &read_fds))
		{
			printf("clit_fd read ready\n");
			if(recv_len2 < BUF_MAX)
			{
				tmp_len = tcp_recv(clit_fd, send_buf+recv_len2, BUF_MAX-recv_len2);
				if(tmp_len <= 0)
				{
					break;
				}
				recv_len2 += tmp_len;
			}
			/*printf("recv from clit:[%s]\n", send_buf);*/
		}

		if(FD_ISSET(proxy_fd, &write_fds))
		{
			printf("proxy_fd write ready\n");
			send_len2 = 0;
			ret = 0;
			while(recv_len2 > 0)
			{
				tmp_len = tcp_send(proxy_fd, send_buf+send_len2, recv_len2);
				if(tmp_len == 0)
				{
					break;
				}
				else if(tmp_len < 0)
				{
					ret = 1;
					break;
				}
				send_len2 += tmp_len;
				recv_len2 -= tmp_len;
			}

			if(ret == 1)
			{
				break;
			}
			else
			{
				if(send_len2 > 0 && recv_len2 > 0)
				{
					memcpy(send_buf, send_buf+send_len2, recv_len2);
					memset(send_buf+recv_len2, 0, BUF_MAX-recv_len2);
				}
				else
				{
					memset(send_buf, 0, sizeof(send_buf));
				}
			}
		}
	}

	return;
}

/*****************************************************
	通讯子线程
*****************************************************/
struct threadstu {int serv_fd;char serv_IP[32];int serv_port;int proxy_fd;int clit_fd;int clit_port;long id;};

void *threadfunc(void *parm) {

	struct threadstu stu;
	char prts[100];

	memcpy(&stu,parm,sizeof(struct threadstu));

	thread_cnt++;
	sprintf(prts,"thread id=[%ld] cnt=[%d] clifd[%d] port[%d] start",stu.id,thread_cnt,stu.clit_fd,stu.clit_port);
	perror(prts);

	/*忽略SIGPIPE信号*/
	signal(SIGPIPE, SIG_IGN);

	/* tcp_close(stu.serv_fd); */

	stu.proxy_fd = tcp_connect(stu.serv_IP, stu.serv_port);
	if(stu.proxy_fd < 0)
	{
		perror("connet to proxy error");
	}
	else
	{
		printf("clit_fd:[%d],proxy_fd:[%d]\n",stu.clit_fd,stu.proxy_fd);
		TransferData(stu.clit_fd, stu.proxy_fd);
		tcp_close(stu.proxy_fd);
	}

	tcp_close(stu.clit_fd);

	thread_cnt--;
	sprintf(prts,"thread id=[%ld] cnt=[%d] clifd[%d] proxyfd[%d] port[%d] end",
		stu.id,thread_cnt,stu.clit_fd,stu.proxy_fd,stu.clit_port);
	perror(prts);
	sleep(10);
	pthread_exit(NULL);
}

/*****************************************************
	主函数
*****************************************************/
void main(int argc, char *argv[])
{
	int fd,fdtablesize;
	int out,error;
	int clit_fd,proxy_fd;
	int pid;
	int proxy_port,serv_port;
	char serv_IP[32];
	struct threadstu stu[100];
	int stuid=0;
	pthread_t p_th;
	pthread_attr_t p_attr;
	char prts[100];

	/* if(argc != 4)
	{
		perror("usage:proxy proxy_port serv_ip serv_port");
		return;
	}
	else
	{
		proxy_port = atoi(argv[1]);
		memset(serv_IP, 0, sizeof(serv_IP));
		strcpy(serv_IP, argv[2]);
		serv_port = atoi(argv[3]);
	}
	*/

	proxy_port = SRV_PORT;
	memset(serv_IP, 0, sizeof(serv_IP));
	strcpy(serv_IP, CLI_IP);
	serv_port = CLI_PORT;

	/* 进程由操作系统管理 */
	if((pid = fork()) < 0)
	{
		perror("fork error");
		exit(1);
	}
	else if(pid > 0)
	{
		exit(0);
	}

	if(setsid() < 0)
	{
		perror("setsid error");
		exit(1);
	}

	/*关闭所有文件描述符*/
	for(fd=0,fdtablesize=getdtablesize();fd<fdtablesize;fd++)
	{
		close(fd);
	}

	/*重定向标准输出和标准错误输出*/
	sprintf(prts,"xy.%d.err",SRV_PORT);
	error = open(prts, O_WRONLY|O_CREAT, 0600);
	/*error = open("/dev/null", O_WRONLY, 0600);*/
	if(dup2(error, 2) == -1)
	{
		perror("dup2 error");
		exit(1);
	}
	close(error);


	/* out = open("xy.log", O_WRONLY|O_CREAT, 0600);*/
	out = open("/dev/null", O_WRONLY|O_CREAT, 0600); 
	if(dup2(out, 1) == -1)
	{
		perror("dup2 out");
		exit(1);
	}
	close(out);

	pid = 0;

	/*处理SIGTERM信号*/
	signal(SIGTERM, sig_term);

	/*忽略SIGCHLD信号*/
	signal(SIGCHLD, SIG_IGN);

	/*忽略SIGPIPE信号*/
	signal(SIGPIPE, SIG_IGN);

	/*忽略SIGTTOU信号*/
	signal(SIGTTOU,SIG_IGN);

	/*忽略SIGTTIN信号*/
	signal(SIGTTIN,SIG_IGN);

	/*忽略SIGTSTP信号*/
	signal(SIGTSTP,SIG_IGN);

	/*忽略SIGHUP信号*/
	signal(SIGHUP,SIG_IGN);

	printf("proxy_port:[%d] serv:[%s:%d]\n", proxy_port, serv_IP, serv_port);

	if((serv_fd = tcp_listen(proxy_port)) == PRO_FAILED)
	{
		perror("tcp_listen error");
		exit(1);
	}

	printf("server socketfd:[%d]\n", serv_fd);

	/** 开始监听 **/

	while(1)
	{
		clit_fd = tcp_accept(serv_fd);
		if(clit_fd < 0)
		{
			perror("accept error");
			continue;
		}

		if(++stuid>=100) stuid=0;

		memset(&stu[stuid],0,sizeof(struct threadstu));
		stu[stuid].serv_fd=serv_fd;
		strcpy(stu[stuid].serv_IP,serv_IP);
		stu[stuid].serv_port=serv_port;
		stu[stuid].clit_fd=clit_fd;
		stu[stuid].id=thread_id++;
		stu[stuid].clit_port=cli_port;
		pthread_attr_init(&p_attr);
		pthread_attr_setdetachstate(&p_attr,PTHREAD_CREATE_DETACHED);

		sprintf(prts,"accept clit_fd:[%d] thread_id:[%d] cnt:[%d] stuid=[%d]", 
			clit_fd, thread_id, thread_cnt ,stuid);
		perror(prts);

		if(pthread_create(&p_th, &p_attr, threadfunc, (void *)&stu[stuid]))
		{
			perror("pthread_create error");
			tcp_close(clit_fd);
		}

		/*忽略SIGCHLD信号，防止僵死进程*/
		signal(SIGCHLD, SIG_IGN);

		/*处理SIGTERM信号*/
		signal(SIGTERM, sig_term);

		/*忽略SIGPIPE信号*/
		signal(SIGPIPE, SIG_IGN);

		/*忽略SIGTTOU信号*/
		signal(SIGTTOU,SIG_IGN);

		/*忽略SIGTTIN信号*/
		signal(SIGTTIN,SIG_IGN);

		/*忽略SIGTSTP信号*/
		signal(SIGTSTP,SIG_IGN);

		/*忽略SIGHUP信号*/
		signal(SIGHUP,SIG_IGN);
	} /* end while */
}

