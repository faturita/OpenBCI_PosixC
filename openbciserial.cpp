#include <stdio.h>
#include <errno.h>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <signal.h>

int
set_interface_attribs (int fd, int speed, int parity)
{
        struct termios tty;
        memset (&tty, 0, sizeof(tty));
        if (tcgetattr (fd, &tty) != 0)
        {
                printf ("error %d from tcgetattr", errno);
                return -1;
        }

        cfsetospeed (&tty, speed);
        cfsetispeed (&tty, speed);

        tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;     // 8-bit chars
        // disable IGNBRK for mismatched speed tests; otherwise receive break
        // as \000 chars
        tty.c_iflag &= ~IGNBRK;         // disable break processing
        tty.c_lflag = 0;                // no signaling chars, no echo,
                                        // no canonical processing
        tty.c_oflag = 0;                // no remapping, no delays
        tty.c_cc[VMIN]  = 0;            // read doesn't block
        tty.c_cc[VTIME] = 5;            // 0.5 seconds read timeout

        tty.c_iflag &= ~(IXON | IXOFF | IXANY); // shut off xon/xoff ctrl

        tty.c_cflag |= (CLOCAL | CREAD);// ignore modem controls,
                                        // enable reading
        tty.c_cflag &= ~(PARENB | PARODD);      // shut off parity
        tty.c_cflag |= parity;
        tty.c_cflag &= ~CSTOPB;
        tty.c_cflag &= ~CRTSCTS;

        if (tcsetattr (fd, TCSANOW, &tty) != 0)
        {
                printf ("error %d from tcsetattr", errno);
                return -1;
        }
        return 0;
}

void
set_blocking (int fd, int should_block)
{
        struct termios tty;
        memset (&tty, 0, sizeof(tty));
        if (tcgetattr (fd, &tty) != 0)
        {
                printf ("error %d from tggetattr", errno);
                return;
        }

        tty.c_cc[VMIN]  = should_block ? 1 : 0;
        tty.c_cc[VTIME] = 5;            // 0.5 seconds read timeout

        if (tcsetattr (fd, TCSANOW, &tty) != 0)
                printf ("error %d setting term attributes", errno);
}


int cont=1;


void my_handler(sig_t s){
           printf("Caught signal %d\n",s);
           if (cont==0) _exit(0);
           cont=0;
}


int readblock(int fd, char *eeg, size_t size)
{
    char c;
    static char previousC;

    int seq=0;

    memset(eeg,0,sizeof(eeg));

    int n = read (fd, &c, 1);
    while (cont && n>=0)
    {
        if (n>0)
        {
            char header[10];
            char footer[10];

            sprintf(footer,"%d",(unsigned int)previousC );

            sprintf(header,"%d",(unsigned int)c );

            //printf("<%d>", c);

            if (seq>0)
            {
                eeg[seq++] = c;
            }
            if (strncmp(footer,"-64",3)==0 && strncmp(header,"-96",3)==0)
            {
                eeg[seq] = c;
                seq=1;
            }
        }

        previousC = c;
        if (seq==size)
        {
            printf("\n");
            return seq;
        }

        n = read (fd, &c, 1);
    }
    return 0;
}


int interpret24bitAsInt32(char *eeg) {
    int newInt = (
        ((0xFF & eeg[0]) << 16) |
        ((0xFF & eeg[1]) << 8) |
        (0xFF & eeg[2])
      );
    if ((newInt & 0x00800000) > 0) {
      newInt |= 0xFF000000;
    } else {
      newInt &= 0x00FFFFFF;
    }
    return newInt;
  }


int test()
{
    char *portname = "/dev/tty.usbserial-DN0096Q1";

    int n;

    signal (SIGINT,(void (*)(int))my_handler);

    printf ("Starting...\n");

    //int fd = open (portname, O_RDWR | O_NOCTTY | O_SYNC);
    int fd = open (portname, O_RDWR | O_NOCTTY | O_NDELAY);
    if (fd < 0)
    {
            printf ("error %d opening %s: %s", errno, portname, strerror (errno));
            return 2;
    }

    usleep ((7 + 25) * 100000);             // sleep enough to transmit the 7 plus

    printf ("USB Serial Port Open\n");

    set_interface_attribs (fd, B115200, 0);  // set speed to 115,200 bps, 8n1 (no parity)
    //set_blocking (fd, 0);                // set no blocking

    printf ("Waiting....\n");
    usleep ((7 + 25) * 100000);             // sleep enough to transmit the 7 plus


    printf ("Sending command\n");

    char buf [256];

    write (fd, "?", 1);           // send 7 character greeting

    usleep ((7 + 25) * 100);             // sleep enough to transmit the 7 plus
                                     // receive 25:  approx 100 uS per char transmit

    n = read (fd, buf, sizeof(buf));  // read up to 100 characters if ready to read

    printf ("%d bytes received\n", n);

    int timeout = 2500000;

    while (cont)
    {
        //printf ("Reading:%d\n",n);
        for(int i=0;i<n;i++)
        {
            char c = buf[i];
            printf ("%c",c);
        }
        timeout--;
        n = read (fd, buf, sizeof(buf));  // read up to 100 characters if ready to read
    }

    write (fd, "b", 1);           // send 7 character greeting

    usleep ((7 + 25) * 1000);             // sleep enough to transmit the 7 plus

    char eeg[35];

    cont=1;

    n = readblock (fd, eeg, 33);
    while (cont)
    {
        if (n>0)
        {
            printf ("[%d][%d][%d][%d][%d]\n", eeg[0],eeg[0+1],eeg[32], eeg[26],eeg[27]);

            int x=(int)(( (int)eeg[26] << 8) || eeg[27]);
            int y=(int)(( (int)eeg[28] << 8) || eeg[29]);
            int z=(int)(( (int)eeg[30] << 8) || eeg[31]);

            printf("(EEG: %d)(X:%d)(Y:%d)(Z:%d)\n", interpret24bitAsInt32(eeg+23), x,y,z);
        }

        n = readblock (fd, eeg, 33);
    }

    write (fd, "s", 1);           // send 7 character greeting

    usleep ((7 + 25) * 100);             // sleep enough to transmit the 7 plus

    close(fd);

    return 1;
}


/**
Header
Byte 0: 0xA0
Bytes 1: Packet counter

EEG Data
Note: values are 24-bit unsigned int, MSB first

Bytes 2-4: Data value for EEG channel 0
Bytes 5-7: Data value for EEG channel 1
Bytes 8-10: Data value for EEG channel 2
Bytes 11-13: Data value for EEG channel 3
Bytes 14-16: Data value for EEG channel 4
Bytes 17-19: Data value for EEG channel 5
Bytes 20-22: Data value for EEG channel 6
Bytes 23-25: Data value for EEG channel 7

Accelerometer Data
Note: values are 16-bit int, MSB first

Bytes 26-27: Data value for accelerometer channel X
Bytes 28-29: Data value for accelerometer channel Y
Bytes 30-31: Data value for accelerometer channel Z

Footer
Byte 32: 0xC0
**/
