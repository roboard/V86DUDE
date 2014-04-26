/*
  v86dude.cpp - 86Duino uploader
  Part of the 86Duino project - http://www.86duino.com/

  Copyright (c) 2013
  Android Lin <acen@dmp.com.tw>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software Foundation,
  Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#include "dmpcfg.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#define UART_ACTIVE      (0x01)
#define UART_INACTIVE    (0x02)
#define COM_TIMEOUT      (8000L)  // timeout = 8s

#if defined (DMP_PCUNIX_GCC)
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/times.h>
typedef struct usbcom {
    char* port;
    int fp;
    termios newstate;
    termios oldstate;
} USBCOM_t;
#elif defined (DMP_WIN32_MSVC)
#include <conio.h>
#include <windows.h>
typedef struct usbcom {
    char* port;
    HANDLE ur;
    int char_size;
    WCHAR use_port[64];
} USBCOM_t;
#endif

unsigned long timer_nowtime(void) { //in ms
#if defined(DMP_WIN32_MSVC) || defined(DMP_WINCE_MSVC)
	return GetTickCount();
#elif defined(DMP_PCUNIX_GCC)
    static bool usetimer = false;
    static unsigned long long inittime;
    struct tms cputime;
    
    if (usetimer == false)
    {
        inittime  = (unsigned long long)times(&cputime);
        usetimer = true;
    }
    
    return (unsigned long)((times(&cputime) - inittime)*1000UL/sysconf(_SC_CLK_TCK));
#endif
}

void wait_uart_state(USBCOM_t* com, int state) {

    unsigned long now = timer_nowtime() + COM_TIMEOUT;
	
#if defined (DMP_WIN32_MSVC)
    MultiByteToWideChar(0, 0, com->port, com->char_size, com->use_port, sizeof(com->use_port));
#endif
    
	while(1)
	{
		if(timer_nowtime() > now) {printf("Wait COM port to timeout\n"); free((void*)com); exit(1);}
        
#if defined (DMP_PCUNIX_GCC)
        com->fp = open(com->port, O_RDWR | O_NOCTTY | O_NONBLOCK);
        if(state == UART_INACTIVE)
        {if(com->fp < 0) break; else close(com->fp);}
        else if(com->fp >= 0) break;
#elif defined (DMP_WIN32_MSVC)
        com->ur = CreateFile(com->use_port,
                             GENERIC_READ | GENERIC_WRITE,
                             0,
                             NULL,
                             OPEN_EXISTING,
                             0,
                             NULL
                             );
        if(state == UART_INACTIVE)
        {if(com->ur == INVALID_HANDLE_VALUE) break; else CloseHandle(com->ur);}
        else
        {if(com->ur != INVALID_HANDLE_VALUE) break;}
#endif
	}
}

USBCOM_t* Init_UART(USBCOM_t* com) {
	wait_uart_state(com, UART_ACTIVE);
#if defined (DMP_PCUNIX_GCC)
    if (tcgetattr(com->fp, &(com->oldstate)) < 0)
        printf("fail to get termios settings\n");
    
    memcpy(&(com->newstate), &(com->oldstate), sizeof(termios));
    
    // set new termios settings
    com->newstate.c_cflag     |= CLOCAL | CREAD;
    com->newstate.c_cflag     &= ~CRTSCTS;                 // disable H/W flow control
    com->newstate.c_lflag     &= ~(ICANON |                // raw mode
                                   ISIG   |                // disable SIGxxxx signals
                                   IEXTEN |                // disable extended functions
                                   ECHO | ECHOE);          // disable all auto-echo functions
    com->newstate.c_iflag     &= ~(IXON | IXOFF | IXANY);  // disable S/W flow control
    com->newstate.c_oflag     &= ~OPOST;                   // raw output
    com->newstate.c_cc[VTIME]  = 0;                        // no waiting to read
    com->newstate.c_cc[VMIN]   = 0;
    
    // set format(baudrate, stopbit, parity)
    com->newstate.c_cflag = (com->newstate.c_cflag & ~CSIZE) | CS8;
    com->newstate.c_cflag &= ~CSTOPB;
    com->newstate.c_cflag &= ~PARENB;
    com->newstate.c_iflag &= ~(INPCK | ISTRIP | PARMRK);
    cfsetospeed(&(com->newstate), B115200);
    cfsetispeed(&(com->newstate), B115200);
    
	if(tcsetattr(com->fp, TCSANOW, &(com->newstate)) < 0)
        printf("fail to set termios settings\n");
    
    // clear input/output buffers
    tcflush(com->fp, TCIOFLUSH);
#elif defined (DMP_WIN32_MSVC)
    /* set baudrate, size, stopbit, parity */
    DCB dcbSerialParams = {0};
    
    if(!GetCommState(com->ur, &dcbSerialParams))
        printf("error getting state\n");
	
    dcbSerialParams.BaudRate = 115200;
    dcbSerialParams.ByteSize = 8;
    dcbSerialParams.StopBits = ONESTOPBIT;
    dcbSerialParams.Parity   = NOPARITY;
	
    if(!SetCommState(com->ur, &dcbSerialParams))
        printf("setting error!!\n");
	
    COMMTIMEOUTS timeouts                = {0};
    timeouts.ReadIntervalTimeout         = MAXWORD;
    timeouts.ReadTotalTimeoutConstant    = 20000;
    timeouts.ReadTotalTimeoutMultiplier  = 20;
    timeouts.WriteTotalTimeoutConstant   = 20000;
    timeouts.WriteTotalTimeoutMultiplier = 0;
	
    if (!SetCommTimeouts(com->ur, &timeouts))
        printf("timeout error!\n");
    
#endif
    
	return com;
}

void Close_UART(USBCOM_t* com) {
	if(com == NULL) return;
#if defined (DMP_PCUNIX_GCC)
    //tcsetattr(com->fp, TCSANOW, &(com->oldstate));
    close(com->fp);
#elif defined (DMP_WIN32_MSVC)
	CloseHandle(com->ur);
#endif
}

static void softreset_86duino(USBCOM_t* com) {
	if(com == NULL) return;
#if defined (DMP_PCUNIX_GCC)
    //tcsetattr(com->fp, TCSANOW, &(com->oldstate));
	cfsetospeed(&(com->newstate), B1200);
	cfsetispeed(&(com->newstate), B1200);
	if(tcsetattr(com->fp, TCSANOW, &(com->newstate)) < 0)
        printf("fail to set termios settings\n");
	close(com->fp);
#elif defined (DMP_WIN32_MSVC)
    DCB dcbSerialParams = {0};
	
	if(!GetCommState(com->ur, &dcbSerialParams))
	{
		printf("error getting com port state");
	}

	dcbSerialParams.BaudRate  = 1200;
	dcbSerialParams.ByteSize  = 8;
	dcbSerialParams.StopBits  = ONESTOPBIT;
	dcbSerialParams.Parity    = NOPARITY;
	
	if(!SetCommState(com->ur, &dcbSerialParams)) 
	{
		// printf("com port setting error!!\n");
	}

	CloseHandle(com->ur);
#endif
}


#define MAXRWSIZE    (500000)

// the size of member in the package
#define OLDCOMM_BYTE          (1)
#define PSIZE_BYTES           (4)
#define COMM_BYTES            (2)
#define FNAME_BYTES           (32)
#define FSIZE_BYTES           (4)
#define LAST_BYTES            (1)
#define CHSUM_BYTES           (1)
#define ERRCODE_BYTES         (2)
#define VERSION_BYTES         (32)
#define WFDONE_BYTES          (9)
#define VSDONE_BYTES          (41)

// command
#define GET_BIOS_VER          (0x0010)
#define GET_BOOTLOADER_VER    (0x0011)
#define BURN_BOOTLOADER       (0x0012)
#define BURN_BIOS             (0x0013)
#define BURN_PROGRAM          (0x0014)
#define BURN_OTHERFILE        (0x0015)

// for old command ack
#define ACK                   (0xff)
#define NOACK                 (0x55)
static bool send_package(USBCOM_t* com, unsigned char* buf, int bsize) {
    int error = 0, bsize2, numbytes = 0;
    unsigned long nowtime;
    
	while (bsize > 0)
    {
        bsize2 = (bsize <= MAXRWSIZE)? bsize : MAXRWSIZE;
        
        for (nowtime = timer_nowtime(); bsize2 > 0; buf += numbytes, bsize2 -= numbytes, bsize -= numbytes)
        {
#if defined (DMP_PCUNIX_GCC)
            if((numbytes = write(com->fp, buf, bsize2)) == -1) numbytes = 0;
#elif defined (DMP_WIN32_MSVC)
            WriteFile(com->ur, buf, bsize2, (LPDWORD)&numbytes, NULL);
            if (numbytes != bsize2)
            {
                printf("Write file fail, please check COM port status\n");
                error = 1;
                return false;
            }
#endif
			if ((timer_nowtime() - nowtime) > COM_TIMEOUT)
            {
                printf("time-out to write bytes\n");
                error = 1;
                return false;
            }
		}
	}
	
#if defined (DMP_PCUNIX_GCC)
    if(tcdrain(com->fp) < 0)
    {
        printf("tcdrain() fails\n");
        return false;
    }
#elif defined (DMP_WIN32_MSVC)
    if (FlushFileBuffers(com->ur) == FALSE)
    {
        printf("FlushFileBuffers() fails\n");
        return false;
    }
#endif

	return true;
}

static bool receive_package(USBCOM_t* com, unsigned char* buf, int bsize) {
    int numbytes = 0;

#if defined (DMP_PCUNIX_GCC)
    unsigned long nowtime = timer_nowtime();
    while(bsize > 0)
    {
        numbytes = read(com->fp, buf, bsize);
        if(numbytes >= 1)
		{
			buf += numbytes;
			bsize -= numbytes;
			numbytes = 0;
		}
        if ((timer_nowtime() - nowtime) > COM_TIMEOUT)
        {
            printf("...time-out to read bytes\n");
            return false;
        }
    }
#elif defined (DMP_WIN32_MSVC)
    ReadFile(com->ur, buf, bsize, (LPDWORD)&numbytes, NULL);
    if(numbytes < bsize)
    {
        printf("...fail, bootloader no response!!\n");
		return false;
    }
#endif
    
    return true;
}


#define EMALLOC               (0x0001)
#define ECOMMAND              (0x0002)
#define EFILENAME             (0x0004)
#define EFILELENGTH           (0x0008)
#define EOPENFILE             (0x0010)
#define EWRITEFILE            (0x0020)
#define EISLAST               (0x0040)
#define ECHECKSUM             (0x0080)
void check_error(unsigned short errorcode) {
	if((errorcode & EMALLOC) > 0)
		printf("Bootloader: Malloc fail, file size too long\n");
	else if((errorcode & ECOMMAND) > 0)
		printf("Bootloader: Receive a unavailable command\n");
	else if((errorcode & EFILENAME) > 0)
		printf("Bootloader: File name too long\n");
	else if((errorcode & EOPENFILE) > 0)
		printf("Bootloader: Open file fail\n");
	else if((errorcode & EWRITEFILE) > 0)
		printf("Bootloader: Write file fail\n");
	else if((errorcode & EISLAST) > 0)
		printf("Bootloader: isLast flag error\n");
	else if((errorcode & ECHECKSUM) > 0)
		printf("Bootloader: Checksum error\n");
	else
		printf("Bootloader: unknown error code\n");
}

#define FNAME_BYTES    (32)
void getFilename(char* exe_file_path, char* filename) {
	int i, j;
	for(i=0; i<FNAME_BYTES; i++) filename[i] = '\0';
	for(i=0, j=0; exe_file_path[i] != '\0'; i++, j++)
	{
		if(exe_file_path[i] == '/' || exe_file_path[i] == '\\') {j = -1; continue;}
		filename[j] = exe_file_path[i];
	}
	for(;j < FNAME_BYTES; j++)
		filename[j] = '\0';
}

void getFilepath(char* path, char* real_path) {
	int i, j;
	for(i=0, j=0; path[i] != '\0'; i++)
		if(path[i] == '/' || path[i] == '\\') j++;
	for(i=0; j > 0; i++)
	{
		if(path[i] == '/' || path[i] == '\\') j--;
		real_path[i] = path[i];
	}
	real_path[i] = '\0';
}

void insertFilename(char* real_path, char* filename) {
	int i, j;
	for(i=0; real_path[i] != '\0'; i++);
	for(j=0; filename[j] != '\0'; i++, j++)
	    real_path[i] = filename[j];
	real_path[i] = '\0';
}

unsigned char Cal_checksum(unsigned char* cch, unsigned long packagesize) {
	unsigned long i;
	unsigned char checksum = 0;
	for(i=0; i<(packagesize - 1); i++)
		checksum = checksum + cch[i];
	checksum = ~(checksum);
	return checksum;
}

unsigned long Cal_filesize(char* fpath) {
    unsigned long fszie;
	FILE *src = fopen(fpath, "rb");
	if(src == NULL) {printf("Open src file: fail, fp = NULL\n"); return 0L;}
	for(fszie=0L; getc(src) != EOF; fszie++);
	fclose(src);
	return fszie;
}


int oldversion_writefile(USBCOM_t* com, FILE* p, unsigned short command, unsigned long filesize) {
	unsigned long packagesize = 0L;
	unsigned char number = 0;
	unsigned char *ch;
	int error = 0, i, j;
	 
    ch = (unsigned char*) malloc(OLDCOMM_BYTE + PSIZE_BYTES + filesize);
    if(ch == NULL) {printf("malloc fail\n"); error = 1; return error;}
	
	ch[0] = (unsigned char) command;
	for(i=1, j=0; j<PSIZE_BYTES; i++, j++)
		ch[i] = (unsigned char)((filesize & (0xff << (8*j))) >> (8*j));
	for(j=0; j<filesize; i++, j++)
		ch[i] = getc(p);
	
	if(send_package(com, ch, i) == false) error = 1;
	free((void*)ch);
	
	if(command == 1)           printf("Uploading the binary sketch ");
	else if(command == 2)      printf("Uploading the bootloader ");
 
	while(number != '1' && number != '2')
	{
		if(receive_package(com, &number, 1) == false) {error = 1; goto WFERR_END;}
		if(number == 'z')
			printf(".");
		else if(number != '1' && number != '2')
		{
			printf("receive an error: %c\n", number);
			error = 1;
			goto WFERR_END;
		}
	}
    printf(" Done\n");
    
WFERR_END:
	return error;
}


int write_file(USBCOM_t* com, FILE* p, unsigned short command, char* filename, unsigned long filesize, unsigned char isLast) {
	unsigned long packagesize = 0L;
	unsigned char number = 0, old_command = NOACK;
	unsigned char *ch;
	int error = 0, i, j;
	
    packagesize = COMM_BYTES + FNAME_BYTES + FSIZE_BYTES + filesize + LAST_BYTES + CHSUM_BYTES; 
    ch = (unsigned char*) malloc(OLDCOMM_BYTE + PSIZE_BYTES + packagesize);
    if(ch == NULL) {printf("malloc fail\n"); error = 1; return error;}
	
	ch[0] = old_command;
	for(i=1, j=0; j<PSIZE_BYTES; i++, j++)
		ch[i] = (unsigned char)((packagesize & (0xff << (8*j))) >> (8*j));
	for(j=0; j<COMM_BYTES; i++, j++)
		ch[i] = (unsigned char)((command & (0xff << (8*j))) >> (8*j));
	for(j=0; j<FNAME_BYTES; i++, j++)
		ch[i] = filename[j];
	for(j=0; j<FSIZE_BYTES; i++, j++)
		ch[i] = (unsigned char)((filesize & (0xff << (8*j))) >> (8*j));
	for(j=0; j<filesize; i++, j++)
		ch[i] = getc(p);
	ch[i] = isLast;
	ch[i+1] = Cal_checksum(ch+OLDCOMM_BYTE+PSIZE_BYTES, packagesize);
	
	if(send_package(com, ch, i+2) == false) error = 1;

	free((void*)ch);
	if(error == 1) goto WFERR_END;
	
	while(number != '1')
	{
		if(receive_package(com, &number, 1) == false) {error = 1; goto WFERR_END;}
		if(number == 'z')
			printf(".");
		else if(number != '1')
		{
			printf("receive an error: %c\n", number);
			error = 1;
			goto WFERR_END;
		}
	}
    
WFERR_END:
	return error;
}

int get_result_package(USBCOM_t* com) {
	unsigned short command, errorcode; 
    unsigned long packagesize = 0L;
	unsigned char checksum;
	unsigned char *ch;
	int error = 0, i, j;
	 
    ch = (unsigned char*) malloc(WFDONE_BYTES);
    if(ch == NULL) {printf("malloc fail\n"); error = 1; return error;}
    
	if(receive_package(com, ch, WFDONE_BYTES) == false) {error = 1; goto WFACKERR_END;}
	
	for(i=0, j=0, packagesize=0L; j<PSIZE_BYTES; i++, j++)
		packagesize = packagesize + ((unsigned long)ch[i] << (8*j));
	for(j=0, command=0; j<COMM_BYTES; i++, j++)
		command = command + ((unsigned short)ch[i] << (8*j));
	for(j=0, errorcode=0; j<ERRCODE_BYTES; i++, j++)
		errorcode = errorcode + ((unsigned short)ch[i] << (8*j));
	checksum = Cal_checksum(ch + PSIZE_BYTES, packagesize);
	
	if(errorcode != 0)
	{
		check_error(errorcode);
		error = 1;
	}
	
	if(checksum != ch[i])
	{
		printf("PC: Checksum error\n");
		error = 1;
	}
	
WFACKERR_END:
	free((void*)ch);
	return error;
}



#define OLD_BOOTLOADER       (0x99)
#define NORMAL_BOOTLOADER    (0x01)
#define VERSION_ERROR        (0xFF)
static int special_get_version(USBCOM_t* com, unsigned short command, unsigned char isLast) {
	unsigned long packagesize = 0L, dummy_size = 0L;
	unsigned char number = 0;
	unsigned char* ch;
	int i, j;
	unsigned char old_command = ACK;
	
    packagesize = COMM_BYTES + LAST_BYTES + CHSUM_BYTES; 
    dummy_size = packagesize | 0x90000000L;
    ch = (unsigned char*) malloc(OLDCOMM_BYTE + PSIZE_BYTES);
    if(ch == NULL) {printf("malloc fail\n"); return VERSION_ERROR;}
    
	ch[0] = old_command; // it is named "type" in old version
    for(i=1, j=0; j<PSIZE_BYTES; i++, j++)
		ch[i] = (unsigned char)((dummy_size & (0xff << (8*j))) >> (8*j));
	if(send_package(com, ch, i) == false) printf("send_package fail\n");
	free((void*)ch);
	
	if(receive_package(com, &number, 1) == false) {printf("receive_package fail\n"); return VERSION_ERROR;}
	if(number == '3') return OLD_BOOTLOADER;

	// Hehuan or up bootloader version
	ch = (unsigned char*) malloc(packagesize);
    if(ch == NULL) {printf("malloc fail\n"); return VERSION_ERROR;}
	
	for(i=0, j=0; j<COMM_BYTES; i++, j++)
		ch[i] = (unsigned char)((command & (0xff << (8*j))) >> (8*j));
	ch[i] = isLast;
	ch[i+1] = Cal_checksum(ch, packagesize);
	
	if(send_package(com, ch, i+2) == false) return VERSION_ERROR;
	free((void*)ch);
	return NORMAL_BOOTLOADER;
}


char version[VERSION_BYTES];
int send_command(USBCOM_t* com, unsigned short command, unsigned char isLast) {
	unsigned long packagesize = 0L;
	int error = 0, i, j;
	unsigned char *ch, old_command = NOACK;
	
    packagesize = COMM_BYTES + LAST_BYTES + CHSUM_BYTES; 
    ch = (unsigned char*) malloc(OLDCOMM_BYTE + PSIZE_BYTES + packagesize);
    if(ch == NULL) {printf("malloc fail\n"); error = 1; return error;}
	
	ch[0] = old_command;
	for(i=1, j=0; j<PSIZE_BYTES; i++, j++)
		ch[i] = (unsigned char)((packagesize & (0xff << (8*j))) >> (8*j));
	for(j=0; j<COMM_BYTES; i++, j++)
		ch[i] = (unsigned char)((command & (0xff << (8*j))) >> (8*j));
	ch[i] = isLast;
	ch[i+1] = Cal_checksum(ch+OLDCOMM_BYTE+PSIZE_BYTES, packagesize);
	
	if(send_package(com, ch, i+2) == false) error = 1;
	free((void*)ch);
	return error;
}

int get_version_ack(USBCOM_t* com, char *v) {
	unsigned short command, errorcode; 
    unsigned long packagesize = 0L;
	unsigned char checksum;
	unsigned char *ch;
	int error = 0, i, j;
	 
    ch = (unsigned char*) malloc(VSDONE_BYTES);
    if(ch == NULL) {printf("malloc fail\n"); error = 1; return error;}
    
	if(receive_package(com, ch, VSDONE_BYTES) == false) {error = 1; goto WFACKERR_END;}
	
	for(i=0, j=0, packagesize=0L; j<PSIZE_BYTES; i++, j++)
		packagesize = packagesize + ((unsigned long)ch[i] << (8*j));
	for(j=0, command=0; j<COMM_BYTES; i++, j++)
		command = command + ((unsigned short)ch[i] << (8*j));
	for(j=0, command=0; j<VERSION_BYTES; i++, j++)
		v[j] = ch[i];
	for(j=0, errorcode=0; j<ERRCODE_BYTES; i++, j++)
		errorcode = errorcode + ((unsigned short)ch[i] << (8*j));
	checksum = Cal_checksum(ch + PSIZE_BYTES, packagesize);
	
	if(errorcode != 0)
	{
		check_error(errorcode);
		error = 1;
	}
	
	if(checksum != ch[i])
	{
		printf("PC: Checksum error\n");
		error = 1;
	}

WFACKERR_END:
	free((void*)ch);
	return error;
}


#define YES         (0x01)
#define NO          (0x02)
int main(int argc, char* argv[]) {
	int i, error = 0, _version;
	unsigned short command;
	unsigned long filesize = 0L;
	unsigned char isLast = NO;
	USBCOM_t* Serial;
	char* exe_file_path = NULL, *versionfilepath = NULL;
	char win_port[20];
    char filename[FNAME_BYTES];
    FILE* src, *versionfile;
    bool standalone_mode = false;
	
	if(argc < 4) return 1;
    if(argc == 5 && strcmp("standalone", argv[4]) == 0) standalone_mode = true;
    
	Serial = (USBCOM_t*) malloc(sizeof(USBCOM_t));
	if(Serial == NULL) return 1;
#if defined (DMP_PCUNIX_GCC)
	Serial->port = argv[1];    
#elif defined (DMP_WIN32_MSVC)
    win_port[0] = '\\'; win_port[1] = '\\'; win_port[2] = '.'; win_port[3] = '\\';
	for(i=0; argv[1][i] != '\0'; i++)
		win_port[4+i] = argv[1][i];
	win_port[4+i] = '\0';
	Serial->port = win_port;
	Serial->char_size = i+5;
#endif

	command = (unsigned short) atoi(argv[2]);
	exe_file_path = argv[3];
	src = fopen(exe_file_path, "rb");
	
	if((filesize = Cal_filesize(exe_file_path)) == 0L) return 1;
	getFilename(exe_file_path, filename);
	
	Init_UART(Serial);
	
	if(standalone_mode == true)
	{
		printf(" == Standalone mode == \n");
		softreset_86duino(Serial);
		wait_uart_state(Serial, UART_INACTIVE);
		Init_UART(Serial);
	}
	
	// Send a special filesize package to distinguish old/Hehuan bootloader
	_version = special_get_version(Serial, GET_BOOTLOADER_VER, isLast);
	if(_version == OLD_BOOTLOADER)
	{
		Close_UART(Serial);
		wait_uart_state(Serial, UART_INACTIVE);
		
		// For old bootloader version, reset it again and use old protocol to write file
		// The 86Duino is running use program, so first need to reboot it
		// Since 86Duino usb connect again, need to call Init_UART()
		Init_UART(Serial);
	    printf("Bootloader version: Beta 0.9\n");
	    softreset_86duino(Serial);
		wait_uart_state(Serial, UART_INACTIVE);
		Init_UART(Serial);
		if(command == BURN_PROGRAM)         command = 1;
		else if(command == BURN_BOOTLOADER) command = 2;
		oldversion_writefile(Serial, src, command, filesize);
	}
	else if(_version == NORMAL_BOOTLOADER)
	{
		get_version_ack(Serial, version);
		if(strcmp(version, "Hehuan 1.0") == 0)
		{
			printf("Bootloader version: %s\n", version);
			send_command(Serial, GET_BIOS_VER, isLast);
			get_version_ack(Serial, version);
			printf("BIOS version: %s\n\n", version);
			
			if(command == BURN_PROGRAM)
			{
				isLast = YES;
				printf("Uploading the binary sketch ");
				error = write_file(Serial, src, command, filename, filesize, isLast);
		    	error = get_result_package(Serial);
				if(error == 0) printf(" Done\n");	
		    }
			else if(command == BURN_BOOTLOADER)
			{
				printf("Uploading the bootloader ");
				error = write_file(Serial, src, command, filename, filesize, isLast);
		    	error = get_result_package(Serial);
		    	if(error == 0) printf(" Done\n"); else goto MAIN_END;
		    	
		    	printf("Prepare the bootloader version file ... ");
			    versionfilepath = (char*) malloc(strlen(exe_file_path) + FNAME_BYTES);
				getFilepath(exe_file_path, versionfilepath);
		    	insertFilename(versionfilepath, "_blver.v86");
				if((filesize = Cal_filesize(versionfilepath)) == 0L)
				{
					printf("fail\n");
					goto MAIN_END;
				}
				printf("Done\n");
				
				isLast = YES;
				printf("Upload the bootloader version file ");
				versionfile = fopen(versionfilepath, "rb");
				error = write_file(Serial, versionfile, BURN_OTHERFILE, "_blver.v86", filesize, isLast);
				error = get_result_package(Serial);
				fclose(versionfile);
				if(error == 0) printf(" Done\n"); else goto MAIN_END;
				
				versionfile = fopen(versionfilepath, "r");
				fgets(version, VERSION_BYTES, versionfile);
				printf("The bootloader is updated to version: %s\n", version);
				fclose(versionfile);
			}
			else if(command == BURN_BIOS) // incomplete: need to add 86Duino EduCake board
			{
			    printf("Prepare the burnning tool ... ");
				versionfilepath = (char*) malloc(strlen(exe_file_path) + FNAME_BYTES);
				getFilepath(exe_file_path, versionfilepath);
		    	insertFilename(versionfilepath, "anybios.exe");
				if((filesize = Cal_filesize(versionfilepath)) == 0L)
				{
					printf("fail\n");
					goto MAIN_END;
				}
				printf("Done\n");
				
				printf("Upload the burnning tool to 86Duino ");
				versionfile = fopen(versionfilepath, "rb");
				error = write_file(Serial, versionfile, BURN_OTHERFILE, "anybios.exe", filesize, isLast);
				error = get_result_package(Serial);
				fclose(versionfile);
				if(error == 0) printf(" Done\n"); else goto MAIN_END;
				
				printf("Prepare the ROM file ... ");
			    getFilepath(exe_file_path, versionfilepath);
		    	insertFilename(versionfilepath, "coreboot.rom");
		    	if((filesize = Cal_filesize(versionfilepath)) == 0L)
				{
					printf("fail\n");
					goto MAIN_END;
				}
				printf("Done\n");
				
				printf("Upload the ROM file to 86Duino ");
				versionfile = fopen(versionfilepath, "rb");
				error = write_file(Serial, versionfile, BURN_OTHERFILE, "coreboot.rom", filesize, isLast);
				error = get_result_package(Serial);
				fclose(versionfile);
				if(error == 0) printf(" Done\n"); else goto MAIN_END;
				
				
				printf("Burnnig BIOS ... ");   
				error = send_command(Serial, BURN_BIOS, isLast);
				error = get_result_package(Serial);
				if(error == 0) printf("Done\n"); else goto MAIN_END;
				
				printf("Prepare the BIOS version file ... ");
				getFilepath(exe_file_path, versionfilepath);
		    	insertFilename(versionfilepath, "_bver.v86");
				if((filesize = Cal_filesize(versionfilepath)) == 0L)
				{
					printf("fail\n");
					goto MAIN_END;
				}
				printf("Done\n");
				
				printf("Upload the BIOS version file ");	
				versionfile = fopen(versionfilepath, "rb");
				error = write_file(Serial, versionfile, BURN_OTHERFILE, "_bver.v86", filesize, isLast);
				error = get_result_package(Serial);
				fclose(versionfile);
				if(error == 0) printf(" Done\n"); else goto MAIN_END;
				
				// check BIOS version
				isLast = YES;
				error = send_command(Serial, GET_BIOS_VER, isLast);
				error = get_version_ack(Serial, version);
				if(error == 0)
					printf("The BIOS is updated to version: %s\n", version);
				else 
					printf("Get the BIOS version fail\n");
			}
			else
			{
				printf("Error command\n");
				goto MAIN_END;
			}
		}
		else
		{
			//...
		}
	}
	else if(_version == VERSION_ERROR)
	{
		printf("Some error during getting bootlaoder version\n");
		goto MAIN_END;
	}
	
    
MAIN_END:    
	if(versionfilepath != NULL) free((void*)versionfilepath);
	Close_UART(Serial);
	fclose(src);
	free((void*)Serial);
    
	return error;
}
