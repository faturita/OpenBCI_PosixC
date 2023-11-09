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


void killsignalhandler(sig_t s){
    printf("Caught signal %d\n",s);
    if (cont==0)
    {
        _exit(0);
    }
    cont=0;
}


// Use me to retrieve one frame.
int readblock(int fd, char *eeg, size_t size)
{
    char c;
    static char previousC;

    unsigned long seq=0;

    memset((void *)eeg,0,sizeof(eeg));

    int n = read (fd, &c, 1);
    while (cont && n>=0)
    {
        if (n>0)
        {
            char header[10];
            char footer[10];

            sprintf(footer,"%d",(unsigned int)previousC );

            sprintf(header,"%d",(unsigned int)c );

            printf("<%u>", c);

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
    return n;
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


int openserialport(char *portname)
{
    printf ("Opening Serial Port %s.\n", portname);

    //int fd = open (portname, O_RDWR | O_NOCTTY | O_SYNC);
    int fd = open (portname, O_RDWR | O_NOCTTY | O_NDELAY);
    if (fd < 0)
    {
            printf ("error %d opening %s: %s", errno, portname, strerror (errno));
            return fd;
    }

    usleep ((7 + 25) * 100000);             // sleep just a bit.

    printf ("USB Serial Port Open.\n");

    set_interface_attribs (fd, B115200, 0);  // set speed to 115,200 bps, 8n1 (no parity)
    //set_blocking (fd, 0);                // set no blocking

    printf ("Waiting....\n");
    usleep ((7 + 25) * 100000);             // sleep enough a little more.

    return fd;
}

int beginstreamingdata(int fd)
{
    char buf [256];

    ssize_t nbytes = write (fd, "?", 1);           // send the register query command.
    if (nbytes==-1)
    {
        printf ("error %d writing bytes to port: %s", errno, strerror (errno));
        return -1;
    }


    usleep ((7 + 25) * 100);             // sleep enough to transmit those bytes.

    int n = read (fd, buf, sizeof(buf));  // read up to 100 characters if ready to read

    //printf ("%d bytes received\n", n);

    int stopMark = 0;  // $ counter

    while (true)
    {
        //printf ("Reading:%d\n",n);
        for(int i=0;i<n;i++)
        {
            char c = buf[i];
            printf ("%c",c);

            if (c=='$')
                stopMark++;
        }

        if (stopMark>=3)
            break;

        n = read (fd, buf, sizeof(buf));  // read up to Buf characters if ready to read
    }

    nbytes = write (fd, "b", 1);           // send the b command to start streaming.
    if (nbytes == -1)
    {
        printf ("error %d writing bytes to port: %s", errno, strerror (errno));
        return -1;
    }

    usleep ((7 + 25) * 1000);             // sleep enough to transmit the command

    return 0;
}

        // sendByteString = b'x' + \
        // self.getByteSendStringForChannelNumber(channelNumber) + \
        // (b'0' if self.status[self.getChannelRowByChannelNumber(channelNumber)] == "on" else b'1') + \
        // b'6' + \
        // b'0' + \
        // (b'0' if self.channelNumberConnectIsBimodal(channelNumber) else b'1') + \
        // (b'0' if self.channelNumberConnectIsBimodal(channelNumber) else b'1') + \
        // b'0' + \
        // b'X'
        // return sendByteString

// x (CHANNEL, POWER_DOWN, GAIN_SET, INPUT_TYPE_SET, BIAS_SET, SRB2_SET, SRB1_SET) X 

int enablechannel(int fd, int channel, bool bimodal)
{
    char buf [256];

    if (bimodal)
        sprintf(buf,"x%1d060000X",channel);
    else
        sprintf(buf,"x%1d060110X",channel);


    ssize_t nbytes = write (fd, buf, 9);           // send the register query command.
    if (nbytes==-1)
    {
        printf ("error %d writing bytes to port: %s", errno, strerror (errno));
        return -1;
    }


    usleep ((7 + 25) * 100);             // sleep enough to transmit those bytes.

    int n = read (fd, buf, sizeof(buf));  // read up to 100 characters if ready to read

    //printf ("%d bytes received\n", n);

    int stopMark = 0;  // $ counter

    while (true)
    {
        //printf ("Reading:%d\n",n);
        for(int i=0;i<n;i++)
        {
            char c = buf[i];
            printf ("%c",c);

            if (c=='$')
                stopMark++;
        }

        if (stopMark>=3)
            break;

        n = read (fd, buf, sizeof(buf));  // read up to Buf characters if ready to read
    }

    return 0;
}


int defaultchannelselection(int fd)
{
    char buf [256];

    ssize_t nbytes = write (fd, "d", 1);           // send the register query command.
    if (nbytes==-1)
    {
        printf ("error %d writing bytes to port: %s", errno, strerror (errno));
        return -1;
    }


    usleep ((7 + 25) * 100);             // sleep enough to transmit those bytes.

    int n = read (fd, buf, sizeof(buf));  // read up to 100 characters if ready to read

    //printf ("%d bytes received\n", n);

    int stopMark = 0;  // $ counter

    while (true)
    {
        //printf ("Reading:%d\n",n);
        for(int i=0;i<n;i++)
        {
            char c = buf[i];
            printf ("%c",c);

            if (c=='$')
                stopMark++;
        }

        if (stopMark>=3)
            break;

        n = read (fd, buf, sizeof(buf));  // read up to Buf characters if ready to read
    }

    return 0;
}


int checksamplingfrequency(int fd)
{
    char buf [256];

    ssize_t nbytes = write (fd, "~~", 2);           // send the register query command.
    if (nbytes==-1)
    {
        printf ("error %d writing bytes to port: %s", errno, strerror (errno));
        return -1;
    }


    usleep ((7 + 25) * 100);             // sleep enough to transmit those bytes.

    int n = read (fd, buf, sizeof(buf));  // read up to 100 characters if ready to read

    //printf ("%d bytes received\n", n);

    int stopMark = 0;  // $ counter

    while (true)
    {
        //printf ("Reading:%d\n",n);
        for(int i=0;i<n;i++)
        {
            char c = buf[i];
            printf ("%c",c);

            if (c=='$')
                stopMark++;
        }

        if (stopMark>=3)
            break;

        n = read (fd, buf, sizeof(buf));  // read up to Buf characters if ready to read
    }

    return 0;
}

int logstreamingdata(int fd, char * filename, int duration)
{
    char eeg[35];
    int fs=250;

    int samples=1;
    int n;

    cont=1;

    FILE *pf = fopen(filename, "w");

    n = readblock (fd, eeg, 33);
    while (cont && (samples <= (fs*duration))  )
    {
        if (n>0)
        {
            int x = (
                ((0xFF & eeg[26]) << 8) |
                (0xFF & eeg[27])
              );

            int y = (
                ((0xFF & eeg[28]) << 8) |
                (0xFF & eeg[29])
              );

            int z = (
                ((0xFF & eeg[30]) << 8) |
                (0xFF & eeg[31])
              );

            int channel0 = interpret24bitAsInt32(eeg+2);
            int channel1 = interpret24bitAsInt32(eeg+5);
            int channel2 = interpret24bitAsInt32(eeg+8);
            int channel3 = interpret24bitAsInt32(eeg+11);
            int channel4 = interpret24bitAsInt32(eeg+14);
            int channel5 = interpret24bitAsInt32(eeg+17);
            int channel6 = interpret24bitAsInt32(eeg+20);
            int channel7 = interpret24bitAsInt32(eeg+23);

            printf("(EEG: %d)(X:%d)(Y:%d)(Z:%d)\n", channel7 , x,y,z);

            fprintf(pf,"%d %d %d %d %d %d %d %d %d %d %d %d\n", (int)eeg[1]+128, x,y,z,channel0,channel1,channel2,channel3, channel4, channel5, channel6, channel7);

        }

        n = readblock (fd, eeg, 33);
        samples++;
    }

    fclose(pf);
    return 0;
}

int endstreamingdata(int fd)
{
    // Ready to receive command info.

    ssize_t nbytes = write (fd, "s", 1);
    if (nbytes == -1)
    {
        printf ("error %d writing bytes to port: %s", errno, strerror (errno));
        return -1;
    }

    usleep ((7 + 25) * 100);             // sleep enough to transmit the 7 plus

    return 0;
}


int test()
{
    char *portname = "/dev/ttyUSB0";

    signal (SIGINT,(void (*)(int))killsignalhandler);

    int fd = openserialport(portname);

    checksamplingfrequency(fd);

    //defaultchannelselection(fd);

    for(int i=1;i<=8;i++)
    {
        enablechannel(fd,i,(i==1 || i==2));
    }

    beginstreamingdata(fd); 

    logstreamingdata(fd, "eeg.dat", 100000);

    endstreamingdata(fd);

    close(fd);

    return 1;
}

