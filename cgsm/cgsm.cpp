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
#define RESET_MODEM 1 // reset flags and AT commands
#define AT_COMMANDS_ONLY 2 // just clear AT commands etc
#define FLAGS_ONLY 3 // just reset flags
#define TX_BUFFER_MAX 500
#define RESPONSE_OK 1
#define RESPONSE_ERR 2
#define RESPONSE_GTHAN 3
// prototypes
int sendText(char*, char*); // number and message, returns > 0 if all went ok
void sendTextError(void); // at some stage during a send something went wrong
int openNonCanonicalUART(void);
void closeNonCanonicalUART(void);
void testForSerialIn(void);
//void resetTxResponseBuffer(void);
bool initaliseModem(int); // full, or partial, partial can be used for routine, doesnt clear flags
void resetModemFlags(void);
void resetModemAT(void);
void handleExpectedResponse(void);

// global VARS
struct termios oldtio, newtio;
bool STOP = false;
char textTo[] = "+353857771366";
char textMessage[] = "Hello World :-)";
int fd;	// serial port

struct gsmFlagStructure{
	int sendText;
	int readMsg;
	int responseType;
	bool responseExpected;
	bool CTS;
} gsmFlags;

struct serialString{
	int MaxLength;
	char buffer[TX_BUFFER_MAX];
	int index;
} TxResponseBuffer;

struct powerCharsIn{
	bool gotGTHAN;
	int gotCRLF; // 0, 1 =<CR>, 2=<CR><LF>
} rxedChars;


bool initaliseModem(int resetLevel)
{
	bool result= true;
	switch (resetLevel)
	{
	case RESET_MODEM:
		resetModemFlags();
		resetModemAT();
		break;
	case AT_COMMANDS_ONLY:
		resetModemAT();
		break;
	case FLAGS_ONLY:
		resetModemFlags();
		break;
	default:
		result = false;
		break;
	}
	return result;
}

void resetModemFlags(void){
	gsmFlags.CTS = true;
	gsmFlags.readMsg = false;
	gsmFlags.responseExpected = false;
	gsmFlags.sendText = 0;
	gsmFlags.responseType = 0;
	// clear the buffer
	TxResponseBuffer.buffer[0] = '\0';
	TxResponseBuffer.index = 0;
	TxResponseBuffer.MaxLength = TX_BUFFER_MAX;
}

void resetModemAT(void){
	;
}

int sendText(char *destNumber, char *messageToGo){
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
		result = write(fd, ATmessageString, strlen(ATmessageString));
		if (result > 0)
		{
			//resetTxResponseBuffer();
			resetModemFlags();
			gsmFlags.sendText = 1;
			gsmFlags.responseExpected = true;
			cout << "Sending Started\n";
		}
		else gsmFlags.sendText = 0;
		break;
	case 1:
		// ok so we must of received the correct >
		strcpy(ATmessageString,"\"");	// "
		strcat(ATmessageString, messageToGo);
		strcat(ATmessageString, "\"\x1a"); // "<sub>
		result = write(fd, ATmessageString, strlen(ATmessageString));
		if (result > 0)
		{
			resetModemFlags();
			gsmFlags.sendText = 2;
			gsmFlags.responseExpected = true;
			cout << "Sending Body\n";
		}
		else gsmFlags.sendText = 0;
		break;
	case 2:
		cout << "Message Sent Log It\n";
		gsmFlags.sendText = 0;
	default:
		gsmFlags.sendText = 0;
		break;
	}
	return gsmFlags.sendText;
}
void sendTextError(void) //error could of occurred at any time how do we handle it
{
	resetModemFlags();
	sendText(textTo, textMessage);
}


/*
Test for a char in, behaviour depends on if we are expecting an answer or not
*/
void testForSerialIn(void){
	if (gsmFlags.responseExpected)
	{
		// so we are waiting dor a response
		char buffer[] = "\0", testSting[20];
		bool gotWhatWeWanted = false;
		//int readResult = read(fd, buffer, 1);
		if (int readResult = read(fd, buffer, 1) > 0)
		{
			TxResponseBuffer.buffer[TxResponseBuffer.index] = buffer[0];
			TxResponseBuffer.index++;
			TxResponseBuffer.buffer[TxResponseBuffer.index] = '\0'; // need this so compare works OK
			// lets do some testing
			if (TxResponseBuffer.buffer[0] == '>')
			{
				gsmFlags.responseType = RESPONSE_GTHAN;// got a GTHAN
				gotWhatWeWanted = true;
			}
			// better to use a string so we can make sure of correct length
			strcpy(testSting, "\r\nOK\r\n");
			if (!strncmp(TxResponseBuffer.buffer,testSting,strlen(testSting)))
			{
				gsmFlags.responseType = RESPONSE_OK; // got an ok
				cout << testSting;
				gotWhatWeWanted = true;
			}
			strcpy(testSting, "\r\nERROR\r\n");
			if (!strncmp(TxResponseBuffer.buffer, testSting, strlen(testSting)))
			{
				gsmFlags.responseType = RESPONSE_ERR; // got an ok
				cout << testSting;
				gotWhatWeWanted = true;
			}
		}
		if (gotWhatWeWanted) handleExpectedResponse();
	}
	else
	{
		// check for ring in etc
	}
}

void handleExpectedResponse(void){
	// this is called if we got a response in, generally will be used to 
	// send second part of command
	switch (gsmFlags.responseType)
	{
	case RESPONSE_GTHAN:
		// this means we already sent number to send text to, now send the body
		if (gsmFlags.CTS == true)
		{
			gsmFlags.CTS = false;
			sendText(textTo, textMessage);
		}
		break;
	case RESPONSE_OK:
		// what was the OK in response to!
		if (gsmFlags.sendText) sendText(textTo, textMessage);
		break;
	case RESPONSE_ERR:
		if (gsmFlags.sendText) sendTextError();
		break;
	default:
		break;
	}
	// clearbuffer
	TxResponseBuffer.buffer[0] = '\0';
}

// initalise the serial port
int openNonCanonicalUART(void){
	int result = false;
	fd = open(MODEMDEVICE, O_RDWR | O_NONBLOCK | O_NOCTTY | O_NDELAY);
	tcgetattr(fd, &oldtio); // save current serial port settings 
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

	tcflush(fd, TCIFLUSH);	// flush the buffer
	tcsetattr(fd, TCSANOW, &newtio);	// get current attributes

	// output some welcome stuff
	char welcomeMsg[] = "Hello, 9600n81 UEXT2\n";

	int n = write(fd, welcomeMsg, strlen(welcomeMsg));
	if (n<0)
	{
		cout << "we got a problem, port not open\n";
	}
	if (fd <0) { perror(MODEMDEVICE); return (-1); }
	result = true;


	return result;
}

void closeNonCanonicalUART(void){
	// do a flush of both RX and TX sata
	tcflush(fd, TCIOFLUSH);
	tcsetattr(fd, TCSANOW, &oldtio);
}



int main(int argc, char *argv[])
{
	int ModemOnline = false;
	char sz[] = "Hello, World!";	//Hover mouse over "sz" while debugging to see its contents
	cout << sz << endl;	//<================= Put a breakpoint here
	// lets try open the serial port
	
	if (int serialPort = openNonCanonicalUART() == false) return -1;
	ModemOnline = initaliseModem(RESET_MODEM);

	
	int messageSent = sendText(textTo, textMessage);
	if (messageSent) printf("Modem Comms UP\n");
	else printf("We couldn't send that\n");
	while (STOP == false)
	{
		// check for char
		testForSerialIn();

		//sleep(1);
	}
	closeNonCanonicalUART();
	return 0;
}