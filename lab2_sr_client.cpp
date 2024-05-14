#include <stdlib.h>
#include <WinSock2.h>
#include <time.h>
#include <stdio.h>
#include <fstream>
#pragma comment(lib,"ws2_32.lib")

#define SERVER_PORT 12340 //接收数据的端口号
#define SERVER_IP "127.0.0.1" // 服务器的 IP 地址
const int SEQ_SIZE = 10;//接收端序列号个数
const int DATA_FRAME_NUM = 12;
const int BUFFER_LENGTH = 1026; //缓冲区大小
const int SEND_WIND_SIZE = 5;//发送窗口大小
const int RCV_WIND_SIZE = 5;//接收窗口大小

int curSeq;		//当前数据包的 seq
int curAck;		//当前等待确认的 ack
int totalSeq;	//收到的包的总数
int totalPacket;//需要发送的包总数
int rcvAckNum = 0;			//接收到的ack数目
int firstRWSeq = 0;	//目前接收窗口内第一个未收到数据的序列号
int firstSWSeq = 0;  //当前发送窗口内第一个数据帧的序列号
int sendSeq = 0;
int ackSeq;

struct send {
	clock_t time;	//定时器
	char buffer[BUFFER_LENGTH];	//数据帧
	send() {
		time = 0;
		ZeroMemory(buffer, sizeof(buffer));
	}
};	//SR协议为每一个发送窗口的数据帧维护一个计时器。
struct send SendWindow[SEND_WIND_SIZE];

//接收窗口的定义
typedef struct  rcvWindow {
	BOOL rcv;	//是否收到了数据
	char buffer[BUFFER_LENGTH];	//收到的数据
	rcvWindow() {
		rcv = FALSE;
		ZeroMemory(buffer, sizeof(buffer));
	}
}RCVWINDOW;
RCVWINDOW RcvWindow[RCV_WIND_SIZE];

void printTips() {
	printf("*****************************************\n");
	printf("| -time to get current time |\n");
	printf("| -quit to exit client |\n");
	printf("| -download |\n");
	printf("| -upload |\n");
	printf("gitgit");
	printf("*****************************************\n");
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

//判断该序列号对应的报文是否可以发送
bool seqIsAvailable() {
	if (totalSeq >= DATA_FRAME_NUM) {	//所有数据帧已经发完
		return false;
	}
	if (SendWindow[sendSeq].time == 0) {	//当前数据帧未发送
		return true;
	}
	return false;
}

//将测试数据读入内存
void loadFile(char* data)
{
	std::ifstream icin;
	icin.open("./test.txt");
	icin.read(data, 1024 * 25);
	icin.close();
}

//保存从服务器传输的文件
void SaveFile(char* data)
{
	FILE* fp = fopen("./DownloadFile.txt", "w");
	fwrite(data, sizeof(char), strlen(data), fp);
	printf("文件已保存!\n");
	fclose(fp);
}

void refreshWindows()
{
	curSeq = 0;		//当前数据包的 seq
	curAck = 0;		//当前等待确认的 ack
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
		return 1;
	}
	if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2)
	{
		printf("Could not find a usable version of Winsock.dll\n");
		WSACleanup();
	}
	else {
		printf("The Winsock 2.2 dll was found okay\n");
	}
	SOCKET socketClient = socket(AF_INET, SOCK_DGRAM,0);	
	//设置套接字为非阻塞模式

	int iMode = 1; //1：非阻塞，0：阻塞
	ioctlsocket(socketClient, FIONBIO, (u_long FAR*) & iMode);//非阻塞设置

	SOCKADDR_IN addrServer;
	int length = sizeof(SOCKADDR);
	addrServer.sin_addr.S_un.S_addr = inet_addr(SERVER_IP);
	addrServer.sin_family = AF_INET;
	addrServer.sin_port = htons(SERVER_PORT);
	//接收缓冲区
	char buffer[BUFFER_LENGTH];
	ZeroMemory(buffer, sizeof(buffer));
	int len = sizeof(SOCKADDR);
	printTips();
	int ret;
	int interval = 1;//收到数据包之后返回 ack 的间隔，默认为 1 表示每个都返回 ack，0 或者负数均表示所有的都不返回 ack
	char cmd[128];
	float packetLossRatio = 0.2; //默认包丢失率
	float ackLossRatio = 0.1; //默认 ACK 丢失率
	char data[1024 * 25];	//传输的报文
	ZeroMemory(data, sizeof(data));
	int recvSize;
	clock_t curTime;
	//用时间作为随机种子，放在循环的最外面
	srand((unsigned)time(NULL));
	while (true) {
		gets_s(buffer);
		ret = sscanf(buffer, "%s%f%f", &cmd, &packetLossRatio, &ackLossRatio);
		//使用 SR 协议实现**双向** UDP 可靠文件传输
		if (!strcmp(cmd, "-download") || !strcmp(cmd, "-upload")) {
			printf("%s\n", "Begin to test SR protocol!");
			int waitCount = 0;
			int stage = 0;
			BOOL b;
			unsigned char u_code;//状态码
			unsigned short seq;//包的序列号
			unsigned short recvSeq;//接收窗口大小为 10，已确认的序列号
			unsigned short waitSeq;//等待的序列号

			if (!strcmp(cmd, "-download")) {
				sendto(socketClient, "-download", strlen("-download") + 1, 0, (SOCKADDR*)&addrServer, sizeof(SOCKADDR));
			}
			else {
				sendto(socketClient, "-upload", strlen("-upload") + 1, 0, (SOCKADDR*)&addrServer, sizeof(SOCKADDR));
				loadFile(data); //读取文件
			}
			//等待 server 回复205
			do {
				recvSize = recvfrom(socketClient, buffer, BUFFER_LENGTH, 0, (SOCKADDR*)&addrServer, &len);
			} while (recvSize < 0);

			while (true)
			{
				switch (stage) {
				case 0://等待握手阶段
					u_code = (unsigned char)buffer[0];
					if ((unsigned char)buffer[0] == 205)
					{
						printf("Ready for file transmission\n");
						buffer[0] = 200;
						buffer[1] = '\0';
						sendto(socketClient, buffer, 2, 0, (SOCKADDR*)&addrServer, sizeof(SOCKADDR));
						if (!strcmp(cmd, "-download")) {
							printf("Downloading......\n");
							stage = 1;
							recvSeq = 0;
							waitSeq = 1;
						}
						else {
							printf("Uploading......\n");
							stage = 2;
							curSeq = 0;
							curAck = 0;
							totalSeq = 0;
							waitCount = 0;

						}
					}
					break;
				case 1://等待接收数据阶段
					do {
						recvSize = recvfrom(socketClient, buffer, BUFFER_LENGTH, 0, (SOCKADDR*)&addrServer, &len);
					} while (recvSize < 0);

					seq = (unsigned short)buffer[0];
					if (seq == 127) {	//服务器成功将报文传输
						printf("文件传输完毕！\n");
						SaveFile(data);
						goto exit;
					}
					//随机法模拟包是否丢失
					b = lossInLossRatio(packetLossRatio);
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
						buffer[0] = 0 ;
						buffer[1] = '\0';
					}
					b = lossInLossRatio(ackLossRatio);
					//模拟ACK报文丢包
					if (b) {
						printf("The ack of %d loss\n", (unsigned char)buffer[0]);
						continue;
					}
					sendto(socketClient, buffer, 2, 0, (SOCKADDR*)&addrServer, sizeof(SOCKADDR));
					printf("send a ack of %d\n", seq);

					if ((seq - 1) % SEND_WIND_SIZE == firstRWSeq) {
						while (RcvWindow[firstRWSeq].rcv) {
							strcat(data, RcvWindow[firstRWSeq].buffer);	//拼接获取到的每一个数据帧的数据
							RcvWindow[firstRWSeq].rcv = FALSE;
							firstRWSeq++;
							firstRWSeq = firstRWSeq % RCV_WIND_SIZE;
						}
					}
					break;

				case 2 :
					if (seqIsAvailable()) {
						//发送给客户端的序列号从 1 开始
						SendWindow[sendSeq].buffer[0] = curSeq + 1;
						memcpy(&SendWindow[sendSeq].buffer[1], data + 1024 * totalSeq, 1024);
						printf("send a packet with a seq of %d\n", curSeq + 1);
						sendto(socketClient, SendWindow[sendSeq].buffer, BUFFER_LENGTH, 0, (SOCKADDR*)&addrServer, sizeof(SOCKADDR));
						SendWindow[sendSeq].time = clock();	//定时器起始时间
						++curSeq;
						++sendSeq;
						sendSeq %= SEND_WIND_SIZE;
						curSeq %= SEQ_SIZE;
						++totalSeq;	//发送的总数据帧数目加1
						Sleep(500);
					}
					//等待 Ack，若没有收到，则返回值为-1，计数器+1
					recvSize = recvfrom(socketClient, buffer, BUFFER_LENGTH, 0, ((SOCKADDR*)&addrServer), &length);
					if (recvSize > 0 && strlen(buffer)) { //收到 ack
						unsigned short ack = (unsigned short)buffer[0] - 1;
						ackSeq = ack % SEND_WIND_SIZE;
						if (SendWindow[ackSeq].time > 0) {
							rcvAckNum++;
						}
						SendWindow[ackSeq].time = -1L;		//设置计时器为-1，表示这个分组已经确认收到
						printf("Receive ack %d\n", ack + 1);
						//如果窗口内第一个数据帧被确认收到，则进行窗口的移动
						if (ackSeq == firstSWSeq) {
							while (SendWindow[firstSWSeq].time < 0) {
								SendWindow[firstSWSeq].time = 0;	//更新计时器
								firstSWSeq++;
								firstSWSeq = firstSWSeq % SEND_WIND_SIZE;
							}
						}
						if (rcvAckNum == DATA_FRAME_NUM) {//传输完毕
							ZeroMemory(buffer, sizeof(buffer));
							buffer[0] = 127;	//给客户端的状态码，表示传输完毕
							buffer[1] = '\0';
							sendto(socketClient, buffer, BUFFER_LENGTH, 0, (SOCKADDR*)&addrServer, sizeof(SOCKADDR));
							printf("文件成功传输给服务器！\n");
							goto exit;	//退出传输状态，断开连接
						}
					}
					curTime = clock();
					for (int i = 0; i < SEQ_SIZE; i++)
					{
						//printf("packet %d time :%d\n", i, SendWindow[i].time);
						//对发送窗口内所有的发送未确认的数据帧进行超时检测
						if (SendWindow[i].time > 0 && curTime - SendWindow[i].time > 3000L) {
							SendWindow[i].time = curTime;
							sendto(socketClient, SendWindow[i].buffer, BUFFER_LENGTH, 0, (SOCKADDR*)&addrServer, sizeof(SOCKADDR));
							printf("TIMEOUT!Resend the packet with a seq of %d \n", SendWindow[i].buffer[0]);
						}
					}
					break;
				}
				if(!strcmp(cmd, "-download"))
					Sleep(500);
			}
		}
		sendto(socketClient, buffer, strlen(buffer) + 1, 0, (SOCKADDR*)&addrServer, sizeof(SOCKADDR));
		do {
			ret = recvfrom(socketClient, buffer, BUFFER_LENGTH, 0, (SOCKADDR*)&addrServer, &len);
		} while (ret < 0);
		printf("%s\n", buffer);
		if (!strcmp(buffer, "Good bye!")) {
			break;
		}
	exit:
		refreshWindows();
		printTips();
	}
	//关闭套接字
	closesocket(socketClient);
	WSACleanup();
	return 0;
}
