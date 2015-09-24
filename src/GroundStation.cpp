//============================================================================
// Name        : GroundStation.cpp
// Author      : Chris Spinner
// Version     :
// Copyright   : Your copyright notice
// Description : Hello World in C++, Ansi-style
//============================================================================

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <err.h>
#include <termios.h>
#include <fcntl.h>
#include "spnQC.h"

#define HEARTBEAT 0xFF
#define CONSOLE_STRING "BEGINCONOUT"

static int32_t sockfd = -1;
static bool isTerminalStateSet = false;
static struct termios ttystateold;
static char charInput = 0;

static void onExit(void);

static bool clientInit(struct hostent *server, uint16_t portno);
static uint32_t clientReadMessage(char* buf, uint32_t maxReadBytes);
static void clientWriteMessage(char* buf, uint32_t sizeBytes);

static uint32_t getKeyboardHit(void);
static void setTerminalState(void);
static bool userInputInit(void);
static void userInputUpdate(void);
static char userInputCharGet(bool consume);
static void restoreTerminalState(void);

static void pollForMessages(void);
static void processConsoleMessage(char* buffer);

int main(int argc, char *argv[])
{
	uint16_t portno;
	struct hostent *server;

	if (argc < 3) {
	   printf("usage %s hostname port\n", argv[0]);
	   return EXIT_FAILURE;
	}

	portno = atoi(argv[2]);
    server = gethostbyname(argv[1]);
	if (server == NULL) {
		printf("ERROR, no such host\n");
		return EXIT_FAILURE;
	}

	if((clientInit(server, portno) == EXIT_FAILURE) ||
	   (userInputInit() == EXIT_FAILURE))
	{
		return EXIT_FAILURE;
	}

	char heartbeat = HEARTBEAT;

	while(1)
	{
		char input;
		userInputUpdate();
		input = userInputCharGet(true);

		if(input != 0)
		{
			// write user input, if any
			clientWriteMessage(&input, sizeof(input));
		}
		else
		{
			// write heartbeat
			clientWriteMessage(&heartbeat, sizeof(heartbeat));

			// poll for and process any messages in the queue
			pollForMessages();

			spnUtilsWaitUsec(30000);
		}
	}

	return 0;
}

static void pollForMessages(void)
{
	char readBuffer[65536];

	while(clientReadMessage(readBuffer, sizeof(readBuffer)) != -1)
	{
		if(0 == strncmp(readBuffer, CONSOLE_STRING, strlen(CONSOLE_STRING)))
		{
			processConsoleMessage(readBuffer + strlen(CONSOLE_STRING));
		}
	}
}

static void processConsoleMessage(char* buffer)
{
	printf("%s", buffer);
}

bool clientInit(struct hostent *server, uint16_t portno)
{
	struct sockaddr_in serv_addr;

	// create the socket
	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd < 0)
	{
		perror("ERROR opening socket");
		return EXIT_FAILURE;
	}


	bzero((char *) &serv_addr, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	bcopy((char *)server->h_addr,
		 (char *)&serv_addr.sin_addr.s_addr,
		 server->h_length);
	serv_addr.sin_port = htons(portno);

	if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
	{
		perror("ERROR connecting");
		return EXIT_FAILURE;
	}

	// make receives non-blocking
	fcntl(sockfd, F_SETFL, O_NONBLOCK);

	atexit(&onExit);

	return EXIT_SUCCESS;
}

static uint32_t clientReadMessage(char* buf, uint32_t maxReadBytes)
{
	bzero(buf, maxReadBytes);

	return read(sockfd, buf, maxReadBytes);
}

static void clientWriteMessage(char* buf, uint32_t sizeBytes)
{
//	printf("%c, %i\n", *buf, write(sockfd, buf, sizeBytes));
	write(sockfd, buf, sizeBytes);
}

static void onExit(void)
{
	printf("Closing Sockets...\n");
	if(sockfd != -1) close(sockfd);

	printf("Restoring Terminal...\n");
	restoreTerminalState();
}

static bool userInputInit(void)
{
	// Set terminal to non-canonical mode, no echo
	setTerminalState();

	if(isTerminalStateSet)
	{
		return EXIT_SUCCESS;
	}
	else
	{
		return EXIT_FAILURE;
	}
}

static void userInputUpdate(void)
{
	if(getKeyboardHit() != 0)
	{
		charInput = fgetc(stdin);
	}
}

static char userInputCharGet(bool consume)
{
	char rtnChar = charInput;

	// Consume the input by clearing the character
	if(consume) charInput = 0;

	return rtnChar;
}

static uint32_t getKeyboardHit(void)
{
	struct timeval tv;
	fd_set fds;
	tv.tv_sec = 0;
	tv.tv_usec = 0;
	FD_ZERO(&fds);
	FD_SET(STDIN_FILENO, &fds); //STDIN_FILENO is 0
	select(STDIN_FILENO+1, &fds, NULL, NULL, &tv);

	return FD_ISSET(STDIN_FILENO, &fds);
}

static void setTerminalState(void)
{
	if(!isTerminalStateSet)
	{
		struct termios ttystate;

		//get the terminal state
		tcgetattr(STDIN_FILENO, &ttystate);

		// Save to "old" state data struct to be used for restore
		ttystateold = ttystate;

		//turn off canonical mode and echo
		ttystate.c_lflag &= ~(ICANON|ECHO|ECHOE|ECHOK|ECHONL);

		//minimum of number input read.
		ttystate.c_cc[VMIN] = 1;

		//set the terminal attributes.
		tcsetattr(STDIN_FILENO, TCSANOW, &ttystate);

		isTerminalStateSet = true;
	}
}

static void restoreTerminalState(void)
{
	if(isTerminalStateSet)
	{
		//restore the terminal attributes.
		printf("Restoring STDIN state...\n");
		tcsetattr(STDIN_FILENO, TCSANOW, &ttystateold);
	}
}
