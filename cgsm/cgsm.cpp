using namespace std;

#include <iostream>
#include <string.h>
#include <stdio.h>
// headers needed for serial
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>


// serial defines
#define BAUDRATE B9600            
#define MODEMDEVICE "/dev/ttyS2"
#define _POSIX_SOURCE 1 // POSIX compliant source


// prototypes
int sendText(int*, char*, char*); // number and message, returns > 0 if all went ok
int openNonCanonicalUART(int *);
void closeNonCanonicalUART(int *);
void testForSerialIn(int *);

// global VARS
struct termios oldtio, newtio;
bool STOP = false;
struct gsmFlagStructure{
	int sendText;
	int readMsg;
} gsmFlags;

struct powerCharsIn{
	bool gotGTHAN;
	int gotCRLF; // 0, 1 =<CR>, 2=<CR><LF>
} rxedChars;

int sendText(int *fd,char *destNumber, char *messageToGo){
	// this is a two part sequence we need to wait for a < back
	char ATmessageString[200];
	int result;
	switch (gsmFlags.sendText)
	{
	case 0:
		// haven't sent anything yet
		strcpy(ATmessageString, "AT+CMGS=\"");
		strcat(ATmessageString, destNumber);
		strcat(ATmessageString, "\"\r"); // "<CR>
	
		// should probably error check for valid number
		result = write(*fd, ATmessageString, strlen(ATmessageString));

		if (result > 0) gsmFlags.sendText = 1;
		else gsmFlags.sendText = -1;

		break;
	case 1:
		// ok so we must of received the correct >
		strcpy(ATmessageString,"\"");	// "
		strcat(ATmessageString, messageToGo);
		strcat(ATmessageString, "\"\x1a"); // "<sub>
		result = write(*fd, ATmessageString, strlen(ATmessageString));
		if (result > 0) gsmFlags.sendText = 2;
		else gsmFlags.sendText = -1;
		break;
	default:
		gsmFlags.sendText = -1;
		break;
	}
	return gsmFlags.sendText;
}

void testForSerialIn(int *fd)
{
	// function will modify the flags 
	int res;
	char buf[2];
	// clear the buffer
	buf[0] = '\0';

	res = read(*fd, buf, 1);
	if (res > 0){
		// for now just see if we got '>'
		switch (buf[0])
		{
		case '>':
			rxedChars.gotGTHAN = true;
			break;
		default:
			break;
		}
	}
}

// initalise the serial port
int openNonCanonicalUART(int *fd){
	int result = false;
	*fd = open(MODEMDEVICE, O_RDWR | O_NONBLOCK | O_NOCTTY | O_NDELAY);
	tcgetattr(*fd, &oldtio); // save current serial port settings 
	bzero(&newtio, sizeof(newtio)); // clear struct for new port settings 

	/*
	BAUDRATE: Set bps rate. You could also use cfsetispeed and cfsetospeed.
	CS8     : 8n1 (8bit,no parity,1 stopbit)
	CLOCAL  : local connection, no modem contol - ignore Carrier detect
	CREAD   : enable receiving characters
	*/
	newtio.c_cflag = BAUDRATE | CS8 | CLOCAL | CREAD;
	// if we want to map CR to LF then c_iflag | ICRNL
	// otherwise leave it alone and noncanonically test for CR
	newtio.c_iflag = IGNPAR;//IGNPAR  : ignore bytes with parity errors
	newtio.c_oflag = 0;
	newtio.c_lflag = 0;// ECHO off
	// settings only used in Non-can
	newtio.c_cc[VTIME] = 0;     // blocking time
	newtio.c_cc[VMIN] = 0;     // or block until numb of chars

	tcflush(*fd, TCIFLUSH);	// flush the buffer
	tcsetattr(*fd, TCSANOW, &newtio);	// get current attributes

	// output some welcome stuff
	char welcomeMsg[] = "Hello, 9600n81 UEXT2\n";

	int n = write(*fd, welcomeMsg, strlen(welcomeMsg));
	if (n<0)
	{
		cout << "we got a problem, port not open\n";
	}
	if (*fd <0) { perror(MODEMDEVICE); return (-1); }
	result = true;


	return result;
}

void closeNonCanonicalUART(int *fd){
	// do a flush of both RX and TX sata
	tcflush(*fd, TCIOFLUSH);
	tcsetattr(*fd, TCSANOW, &oldtio);
}


int main(int argc, char *argv[])
{
	char sz[] = "Hello, World!";	//Hover mouse over "sz" while debugging to see its contents
	cout << sz << endl;	//<================= Put a breakpoint here
	// lets try open the serial port
	int fd;	// serial port
	if (int serialPort = openNonCanonicalUART(&fd) == false) return -1;
	char textTo[] = "+353857771366";
	char textMessage[] = "Hello World :-)";
	int messageSent = sendText(&fd, textTo, textMessage);
	if (messageSent) printf("Sent OK\n");
	else printf("We couldn't send that\n");
	while (STOP == false)
	{
		// check for char
		testForSerialIn(&fd);
		// did we get anything in
		if (rxedChars.gotGTHAN == true & gsmFlags.sendText == 1)
		{
			if (sendText(&fd, textTo, textMessage) == 2) gsmFlags.sendText = 0; // second part gone
			rxedChars.gotGTHAN = false;
		}
		sleep(1);
	}
	closeNonCanonicalUART(&fd);
	return 0;
}