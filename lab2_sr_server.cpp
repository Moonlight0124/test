#include <stdlib.h>
#include <time.h>
#include <WinSock2.h>
#include <fstream>
#include <WS2tcpip.h>
#pragma comment(lib,"ws2_32.lib")

#define SERVER_PORT 12340 //端口号
#define SERVER_IP "0.0.0.0" //IP 地址
const int DATA_FRAME_NUM = 12;
const int BUFFER_LENGTH = 1026; //缓冲区大小
const int SEND_WIND_SIZE = 5;//发送窗口大小
const int RCV_WIND_SIZE = 5;//接收窗口大小
const int SEQ_SIZE = 10; 

int curSeq;		   //当前数据包的 seq
int firstSWSeq = 0;  //当前发送窗口内第一个数据帧的序列号
int firstRWSeq = 0;	//目前接收窗口内第一个未收到数据的序列号
int totalSeq;	   //收到的包的总数
int totalPacket;   //需要发送的包总数
int rcvAckNum = 0; //接收到的ack数目
int sendSeq=0;
int ackSeq;

struct send {
	clock_t time;	//定时器
	char buffer[BUFFER_LENGTH];	//数据帧
	send() {
		time = 0;
		ZeroMemory(buffer, BUFFER_LENGTH);
	}
};	//SR协议为每一个发送窗口的数据帧维护一个计时器。
struct send SendWindow[SEND_WIND_SIZE];


//接收窗口的定义
typedef struct  rcvWindow {
	bool rcv;	//是否收到了数据
	char buffer[BUFFER_LENGTH];	//收到的数据
	rcvWindow() {
		rcv = false;
		ZeroMemory(buffer, BUFFER_LENGTH);
	}
}RCVWINDOW;
RCVWINDOW RcvWindow[RCV_WIND_SIZE];

//************************************
// Method: getCurTime
// FullName: getCurTime
// Access: public 
// Returns: void
// Qualifier: 获取当前系统时间，结果存入 ptime 中
// Parameter: char * ptime
//************************************
void getCurTime(char* ptime) {
	char buffer[128];
	memset(buffer, 0, sizeof(buffer));
	time_t c_time;
	struct tm* p;
	time(&c_time);
	p = localtime(&c_time);
	sprintf_s(buffer, "%d/%d/%d %d:%d:%d",
		p->tm_year + 1900,
		p->tm_mon,
		p->tm_mday,
		p->tm_hour,
		p->tm_min,
		p->tm_sec);
	strcpy_s(ptime, sizeof(buffer), buffer);
}

//************************************
// Method: lossInLossRatio
// FullName: lossInLossRatio
// Access: public 
// Returns: BOOL
// Qualifier: 根据丢失率随机生成一个数字，判断是否丢失,丢失则返回TRUE，否则返回 FALSE
// Parameter: float lossRatio [0,1]
//************************************
BOOL lossInLossRatio(float lossRatio) {
	int lossBound = (int)(lossRatio * 100);
	int r = rand() % 101;
	if (r <= lossBound) {
		return TRUE;
	}
	return FALSE;
}

//************************************
// Method: seqIsAvailable
// FullName: seqIsAvailable
// Access: public 
// Returns: bool
// Qualifier: 当前序列号 curSeq 是否可用
//************************************
bool seqIsAvailable() {
	if (totalSeq >= DATA_FRAME_NUM) {	//所有数据帧已经发完的情况
		return false;
	}
	if (SendWindow[sendSeq].time == 0) {	//当前数据帧未发送的情况
		return true;
	}
	return false;
}

void loadFile(char* data)
{
	//将测试数据读入内存
	std::ifstream icin;
	icin.open("./test.txt");
	icin.read(data, 1024 * DATA_FRAME_NUM);
	icin.close();
}

//保存从客户端上传的文件
void SaveFile(char* data)
{
	FILE* fp = fopen("./UploadFile.txt", "w");
	fwrite(data, sizeof(char), strlen(data), fp);
	printf("文件已保存!\n");
	printf("修改内容！\n");
	fclose(fp);
}

void refreshWindows()
{
	curSeq = 0;		//当前数据包的 seq
	totalSeq = 0;	//收到的包的总数
	totalPacket = 0;//需要发送的包总数
	rcvAckNum = 0;			//接收到的ack数目
	firstRWSeq = 0;	//目前接收窗口内第一个未收到数据的序列号
	firstSWSeq = 0;  //当前发送窗口内第一个数据帧的序列号
	sendSeq = 0;
	ackSeq = 0;

	for (int i = 0; i < SEND_WIND_SIZE; i++)
	{
		SendWindow[i].time = 0L;
		ZeroMemory(SendWindow[i].buffer, BUFFER_LENGTH);
	}
	for (int i = 0; i < RCV_WIND_SIZE; i++)
	{
		RcvWindow[i].rcv = FALSE;
		ZeroMemory(RcvWindow[i].buffer, BUFFER_LENGTH);
	}
}
int main(int argc, char* argv[])
{
	//加载套接字库（必须）
	WORD wVersionRequested;
	WSADATA wsaData;
	//套接字加载时错误提示
	int err;
	//版本 2.2
	wVersionRequested = MAKEWORD(2, 2);
	//加载 dll 文件 Scoket 库
	err = WSAStartup(wVersionRequested, &wsaData);
	if (err != 0) {
		//找不到 winsock.dll
		printf("WSAStartup failed with error: %d\n", err);
		return -1;
	}
	if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2)
	{
		printf("Could not find a usable version of Winsock.dll\n");
		WSACleanup();
	}
	else {
		printf("The Winsock 2.2 dll was found okay\n");
	}
	SOCKET sockServer = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	//设置套接字为非阻塞模式

	int iMode = 1; //1：非阻塞，0：阻塞
	ioctlsocket(sockServer, FIONBIO, (u_long FAR*) & iMode);//非阻塞设置
	SOCKADDR_IN addrServer; //服务器地址
	//addrServer.sin_addr.S_un.S_addr = inet_addr(SERVER_IP);
	addrServer.sin_addr.S_un.S_addr = htonl(INADDR_ANY);//两者均可
	addrServer.sin_family = AF_INET;
	addrServer.sin_port = htons(SERVER_PORT);
	err = bind(sockServer, (SOCKADDR*)&addrServer, sizeof(SOCKADDR));
	if (err) {
		err = GetLastError();
		printf("Could not bind the port %d for socket.Error code is % d\n", SERVER_PORT, err);
		WSACleanup();
		return -1;
	}
	SOCKADDR_IN addrClient; //客户端地址
	addrClient.sin_addr.S_un.S_addr = htonl(INADDR_ANY);//两者均可
	addrClient.sin_family = AF_INET;
	addrClient.sin_port = htons(SERVER_PORT);
	int length = sizeof(SOCKADDR);
	char buffer[BUFFER_LENGTH]; //数据发送接收缓冲区
	char data[1024 * DATA_FRAME_NUM];
	ZeroMemory(buffer, sizeof(buffer));
	ZeroMemory(data, sizeof(data));
	float packetLossRatio = 0.1; //默认包丢失率 
	float ackLossRatio = 0.1; //默认 ACK 丢失率 
	int recvSize;
	clock_t curTime;
	unsigned short ack;
	while (true) {
		//非阻塞接收，若没有收到数据，返回值为-1
		recvSize = recvfrom(sockServer, buffer, BUFFER_LENGTH, 0, ((SOCKADDR*)&addrClient), &length);
		if (recvSize < 0 && strcmp(buffer, "-upload")) {
			Sleep(500);
			continue;
		}
		printf("recv from client: %s\n", buffer);
		if (strcmp(buffer, "-time") == 0) {
			getCurTime(buffer);
		}
		else if (strcmp(buffer, "-quit") == 0) {
			strcpy_s(buffer, strlen("Good bye!") + 1, "Good bye!");
		}
		else if (strcmp(buffer, "-download") == 0 || strcmp(buffer, "-upload") == 0) {
			//进入 gbn 测试阶段
			//首先 server（server 处于 0 状态）向 client 发送 205 状态码（server进入 1 状态）
			//server 等待 client 回复 200 状态码，如果收到（server 进入 2 状态），则开始传输文件，否则延时等待直至超时
			//在文件传输阶段，server 发送窗口大小设为10
			char order[30];	//保存来自客户端的指令
			memcpy(order, buffer, sizeof(order));
			ZeroMemory(buffer, sizeof(buffer));
			int recvSize;
			int waitCount = 0;

			unsigned short seq;//包的序列号
			unsigned short recvSeq;

			printf("Begin to test SR protocol,please don't abort the process\n");
			//加入了一个握手阶段
			//首先服务器向客户端发送一个 205 大小的状态码，表示服务器准备好了，可以发送数据
				//客户端收到 205 之后回复一个 200 大小的状态码，表示客户端准备好了，可以接收数据了
				//服务器收到 200 状态码之后，就开始使用 SR 发送数据了
			printf("Shake hands stage\n");
			int stage = 0;
			bool runFlag = true;
			while (runFlag) {
				switch (stage) {
				case 0://发送 205 阶段
					buffer[0] = 205;
					sendto(sockServer, buffer, strlen(buffer) + 1, 0, (SOCKADDR*)&addrClient, sizeof(SOCKADDR));
					Sleep(100);
					stage = 1;
					break;
				case 1://等待接收 200 阶段，没有收到则计数器+1，超时则放弃此次“连接”，等待从第一步开始
					recvSize = recvfrom(sockServer, buffer, BUFFER_LENGTH, 0, ((SOCKADDR*)&addrClient), &length);
					if (recvSize < 0) {
						++waitCount;
						if (waitCount > 20) {
							runFlag = false;
							printf("Timeout error\n");
							break;
						}
						Sleep(500);
						continue;
					}
					else {
						if ((unsigned char)buffer[0] == 200) {
							if (strcmp(order, "-download") == 0) {
								loadFile(data); //读取文件
								totalPacket = sizeof(data) / 1024;
								printf("Begin a file transfer\n");
								printf("File size is %dB, each packet is 1024B and packet total num is %d\n", (int)sizeof(data), totalPacket);
								stage = 2;
								curSeq = 0;
								totalSeq = 0;
								waitCount = 0;
							}
							else {
								printf("Begin receiving a file from Client!\n");
								stage = 3;
								recvSeq = 0;
							}
						}
					}
					break;
				case 2://服务器向客户端数据传输阶段，即客户端从服务器下载文件
					if (seqIsAvailable()) {
						//发送给客户端的序列号从 1 开始
						SendWindow[sendSeq].buffer[0] = curSeq + 1;
						memcpy(&SendWindow[sendSeq].buffer[1], data + 1024 * totalSeq, 1024);
						printf("send a packet with a seq of %d\n", curSeq + 1);
						sendto(sockServer, SendWindow[sendSeq].buffer, BUFFER_LENGTH, 0, (SOCKADDR*)&addrClient, sizeof(SOCKADDR));
						SendWindow[sendSeq].time = clock();	//定时器起始时间
						++curSeq;
						++sendSeq;
						sendSeq %= SEND_WIND_SIZE;
						curSeq %= SEQ_SIZE;
						++totalSeq;	//发送的总数据帧数目加1
						Sleep(500);
					}
					//等待 Ack，若没有收到，则返回值为-1，计数器+1
					recvSize = recvfrom(sockServer, buffer, BUFFER_LENGTH, 0, ((SOCKADDR*)&addrClient), &length);
					if (recvSize > 0 && strlen(buffer)) { //收到 ack
						ack = (unsigned short)buffer[0] - 1;
						ackSeq = ack % SEND_WIND_SIZE;
						if (SendWindow[ackSeq].time > 0) {
							rcvAckNum++;
						}
						SendWindow[ackSeq].time = -1L;		//设置计时器为-1，表示收到ACK
						printf("Receive ack %d\n", ack + 1);
						//如果窗口内第一个数据帧被确认收到，则进行窗口移动
						if (ackSeq == firstSWSeq) {
							while (SendWindow[firstSWSeq].time < 0) {
								SendWindow[firstSWSeq].time = 0;	//重置计时器
								firstSWSeq++;
								firstSWSeq = firstSWSeq % SEND_WIND_SIZE;
							}
						}
						if (rcvAckNum >= DATA_FRAME_NUM) {//传输完毕
							ZeroMemory(buffer, sizeof(buffer));
							buffer[0] = 127;	//给客户端的状态码，表示传输完毕
							buffer[1] = '\0';
							sendto(sockServer, buffer, BUFFER_LENGTH, 0, (SOCKADDR*)&addrClient, sizeof(SOCKADDR));
							printf("文件成功传输给客户端！\n");
							goto exit;	//退出传输状态，断开连接
						}
					}
					curTime = clock();
					for (int i = 0; i < SEND_WIND_SIZE; i++)
					{
						//printf("packet %d time :%d\n", i, SendWindow[i].time);
						//对发送窗口内所有的发送未确认的数据帧进行超时检测
						if (SendWindow[i].time >0  && curTime - SendWindow[i].time > 3000L) {
							SendWindow[i].time = curTime;
							sendto(sockServer, SendWindow[i].buffer, BUFFER_LENGTH, 0, (SOCKADDR*)&addrClient, sizeof(SOCKADDR));
							printf("TIMEOUT!Resend the packet with a seq of %d\n", SendWindow[i].buffer[0]);
						}
					}
					break;

				case 3://客户端向服务器上传文件阶段
					do {
						recvSize = recvfrom(sockServer, buffer, BUFFER_LENGTH, 0, (SOCKADDR*)&addrClient, &length);
					} while (recvSize < 0);

					seq = (unsigned short)buffer[0];
					if (seq == 127) {	//服务器成功将报文传输
						printf("文件传输完毕！\n");
						SaveFile(data);
						goto exit;
					}
					//随机法模拟包是否丢失
					BOOL b = lossInLossRatio(packetLossRatio);
					if (b) {
						printf("The packet with a seq of %d loss\n", seq);
						continue;
					}
					printf("recv a packet with a seq of %d\n", seq);
					if (!RcvWindow[(seq - 1) % SEND_WIND_SIZE].rcv) {	//序列号从1开始，但接收窗口数组从0开始，故数据帧对应序列号减1的窗口项

						memcpy(RcvWindow[(seq - 1) % SEND_WIND_SIZE].buffer, buffer + 1, sizeof(buffer) - 1);
						RcvWindow[(seq - 1) % SEND_WIND_SIZE].rcv = TRUE;	//表示该数据帧已经收到
						//printf("%s\n", RcvWindow[(seq - 1) % SEND_WIND_SIZE].buffer);
						buffer[0] = seq;
						recvSeq = seq;
						buffer[1] = '\0';
					}
					else {
						//如果当前一个包都没有收到，则等待 Seq 为 1 的数据包，不是则不返回 ACK（因为并没有上一个正确的 ACK）
						if (!recvSeq) {
							continue;
						}
						buffer[0] = recvSeq;
						buffer[1] = '\0';
					}
					b = lossInLossRatio(ackLossRatio);
					//模拟ACK报文丢包
					if (b) {
						printf("The ack of %d loss\n", (unsigned char)buffer[0]);
						continue;
					}
					sendto(sockServer, buffer, 2, 0, (SOCKADDR*)&addrClient, sizeof(SOCKADDR));
					printf("send a ack of %d\n", seq);

					if ((seq - 1) % SEND_WIND_SIZE == firstRWSeq) {	    //若接收到的数据帧恰好是接收窗口的第一个
						while (RcvWindow[firstRWSeq].rcv) {
							strcat(data, RcvWindow[firstRWSeq].buffer);	//拼接获取到的每一个数据帧的数据
							RcvWindow[firstRWSeq].rcv = FALSE;
							firstRWSeq++;
							firstRWSeq = firstRWSeq % RCV_WIND_SIZE;
						}
					}
					break;
				}
			}
		}
		sendto(sockServer, buffer, strlen(buffer) + 1, 0, (SOCKADDR*)&addrClient,sizeof(SOCKADDR));
	exit:
		refreshWindows();
		Sleep(500);
	}
	//关闭套接字，卸载库
	closesocket(sockServer);
	WSACleanup();
	return 0;
}