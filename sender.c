/* A simple server in the internet domain using TCP
 The port number is passed as an argument
 This version runs forever, forking off a separate
 process for each connection
 */
#include <sys/types.h>   // definitions of a number of data types used in socket.h and netinet/in.h
#include <sys/socket.h>  // definitions of structures needed for sockets, e.g. sockaddr
#include <arpa/inet.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/wait.h>	/* for the waitpid() system call */
#include <signal.h>	/* signal name macros, and the kill() prototype */
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>

#define CHUNKSIZE   900
#define PACKETSIZE  920
#define TIMEOUT     3
#define ATTEMPTS    5

#define REQUEST 1
#define DATA 2
#define ACK 3
#define LAST 4


//int send = 1;
int attempts = 0;
int base = 0;

int loops = 0;


void error(char *msg)
{
    perror(msg);
    exit(1);
}


//Parses the request line for the file name being requested
void parse_request(char* input_buffer, char* file)
{
    int k = 0;
    int i = 0;
    
    //loop through the input_buffer (the request line) until we find a '/'
    for(i=0;i<=1023;i++)
    {
        if(input_buffer[i]=='/') //Once we find a '/' we parse the buffer until
        {                        // there is a ' '. This is returned as the file!
            i++;
            while(input_buffer[i]!=' ')
            {
                file[k] = input_buffer[i];
                k++;
                i++;
            }
            file[k]= '\0'; //Append null character as last character of file
            break;
        }
    }
}

//parses filename for the file extension to specify the content-type
void get_type(char* filename, char* ext)
{
    int size = strlen(filename);
    //Copies the last 4 characters of the filename to ext
    ext[0] = filename[size-4];
    ext[1] = filename[size-3];
    ext[2] = filename[size-2];
    ext[3] = filename[size-1];
    ext[4] = '\0';
}

int keep_going = 1;

void catch_alarm(int sig)
{
    keep_going = 0;
}

void dostuff(int); /* function prototype */

struct Packet
{
    int source;
    int dest_port;
    int dgtype;
    int seq;
    int len;
    char data[CHUNKSIZE];
};

int main(int argc, char *argv[])
{
    int sockdg, portno, pid, Cwnd;
    float probLoss, probCrpt;
    socklen_t clilen;
    struct sockaddr_in serv_addr, cli_addr;
    struct sigaction sa;          // for signal SIGCHLD
    
    srand48(time(NULL));
    
    if (argc < 5 || argc > 5) {
        fprintf (stderr, "Usage:  %s <Port Number> <Cwnd> <Prob Loss> <Prob Corr>\n", argv[0]);
        exit (1);
    }
    
    portno = atoi(argv[1]);
    Cwnd = atoi(argv[2]);
    probLoss = atof(argv[3]);
    probCrpt = atof(argv[4]);
    
    sockdg = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sockdg < 0)
        error("ERROR opening socket");
    
    bzero((char *) &serv_addr, sizeof(serv_addr));
    
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(portno);
    
    if (bind(sockdg, (struct sockaddr *) &serv_addr,
             sizeof(serv_addr)) < 0)
        error("ERROR on binding");
    
    clilen = sizeof(cli_addr);
    
    //printf("current stats:\n port number: %d\n window size: %d\n", portno, Cwnd);
    printf("current stats:\nport number: %u\nwindow size: %d\n", ntohs(serv_addr.sin_port), Cwnd);
    //printf("IP test: %d\n", inet_addr("127.0.0.1"));
    char inet_string[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(serv_addr.sin_addr), inet_string, INET_ADDRSTRLEN);
    //printf("IP: %s\n", inet_string);
    
    
    
    sa.sa_handler = catch_alarm; // reap all dead processes
    sigfillset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGALRM, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }
    
    char initial_buf[1000];
    char file_to_send[256];
    
    //printf("notice: %s\n", "about to enter main while loop");
    
    while (1)
    {
      //  printf("notice: %s\n", "main while loop entered");
        int lastRecv = 0; //highest acked packet we have sent
        int lastSent = 0; //seq_no of last sent packet
        struct Packet packetdg;
        struct Packet ackdg;
        
        memset(&packetdg, 0, sizeof(packetdg));
        memset(&ackdg, 0, sizeof(ackdg));
        
        int request_len = recvfrom (sockdg, &packetdg, sizeof(packetdg), 0,
                                    (struct sockaddr *) &cli_addr, &clilen);
        
        //printf("notice: %s\n", "recvfrom done");
        printf("%d bytes received\n", request_len);
        
        packetdg.dgtype = ntohl(packetdg.dgtype);
        packetdg.len = ntohl(packetdg.len);
        packetdg.seq = ntohl(packetdg.seq);
        //packetdg.data = ntohl(packetdg.data);
        
        memcpy (initial_buf, packetdg.data, CHUNKSIZE);
        printf("filename: %s\n", packetdg.data);
        //parse_request(initial_buf,file_to_send);
        
        char location[CHUNKSIZE] = "";
        strcat(location,"sender_files/");
        strcat(location,packetdg.data);
        //strcat(location,"\0");
        
        /*
         FILE * pFile;
         pFile = fopen ("sender_files/myfile.txt","w");
         if (pFile!=NULL)
         {
         fputs ("fopen example",pFile);
         fclose (pFile);
         }
         */
        
        //GET FILE SIZE based on filename//
        char* filename;
        int sock;
        FILE *fp;
        int file_size;
        char* file_data;
        printf("Sending FILE: %s\n", location);
        fp = fopen(location, "rb"); //open the file for readbyte
        
        if(!fp){
            //send a NOT_FOUND packet?
            printf("FILE: %s not found\n", location);
            return -1;
        }
        
        fseek(fp, 0, SEEK_END);
        file_size = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        
        //FIND NUMBER OF PACKETS//
        int numPackets;
        int lastPacketLength;
        
        printf("file length: %d\n", file_size);
        
        numPackets = file_size/CHUNKSIZE; //number of packets with CHUNKSIZE data
        lastPacketLength = file_size%CHUNKSIZE;
        
        //Initialize array of data packets//
        
        //allocate array of packets
        struct Packet *packetArray;
        packetArray = (struct Packet*) malloc((numPackets+1)*sizeof(struct Packet));
        
        printf("making %d packets\n", numPackets+1);
        
        int i=0;
        for(i=0; i<numPackets; i++) //handle packets with full data sections
        {
            //packets are created ahead of time but not sent
            //read into the data part of the packets
            printf("PACKET %d created\n", i+1);
            fread(packetArray[i].data, 1, CHUNKSIZE, fp);
            packetArray[i].dgtype = htonl(DATA);
            packetArray[i].seq = htonl(i+1);
            packetArray[i].len = htonl(sizeof(struct Packet)); //or maybe sizeof data?
            
        }
        
        fread(packetArray[numPackets].data, 1, CHUNKSIZE, fp);
        packetArray[numPackets].dgtype = htonl(LAST);
        packetArray[numPackets].seq = htonl(numPackets+1);
        packetArray[numPackets].len = htonl(lastPacketLength);
        
        /*
         fread(packetArray[numPackets].data, 1, CHUNKSIZE, fp);
         packetArray[numPackets].dgtype = LAST;
         packetArray[numPackets].seq = numPackets;
         packetArray[numPackets].len = sizeof(struct Packet);
         */
        
        printf("PACKET %d created\n", numPackets+1);
        
        while (lastRecv < numPackets+1)
        {
            //printf("sender while loop entry.\n");
            int j=0;
            keep_going = 1;
            alarm(TIMEOUT); //alarm set
            for (j=0; j<Cwnd && j+lastRecv < numPackets + 1; j++)
            {
                
                //send as many packets as possible given Cwnd
                //printf("Cwnd: %d.\n", Cwnd);
                //printf("sending packets, j=%d.\n", j);
                //packetdg = packetArray[j+lastRecv];
                //printf("SENDING DATA: %d\n", (j+lastRecv));
                printf("PACKET SEQ NO: %d\n", ntohl(packetArray[j+lastRecv].seq));
                //printf("MESSAGE: %s\n", packetdg.data);
                int dgbytes_sent = sendto(sockdg, &packetArray[j+lastRecv], sizeof(packetdg), 0, (struct sockaddr *) &cli_addr, sizeof (serv_addr));
                //int dgbytes_sent = sendto(sockdg, &packetdg, sizeof(packetdg), 0, (struct sockaddr *) &cli_addr, sizeof (serv_addr));
                //int bytes_sent = sendto(sockdg, &packetArray[j+lastRecv], sizeof (packetArray[j+lastRecv]), 0, (struct sockaddr *) &cli_addr,sizeof (serv_addr));
                printf("bytes sent: %d\n", dgbytes_sent);
                if(dgbytes_sent != sizeof(packetdg))
                {
                    printf("error with sendto.\n");
                    loops++;
                    if(loops > 5)
                    {
                        exit(0);
                    }
                }
                lastSent = (j+lastRecv);
                
            }
            //printf("For loop exit.\n");
            //loops++;
            if(loops > 5)
            {
                exit(0);
            }
            
            //printf("waiting for ACK.\n");
            
            
            //while recvfrom yields nothing keep reading
            while((recvfrom (sockdg, &ackdg, sizeof(ackdg), 0,
                             (struct sockaddr *) &cli_addr, &clilen) <= 0))
            {
                //printf("attempts: %d\n", attempts);
                if (keep_going == 0)        /* Alarm went off  */
                {
                    keep_going = 1;
                    if(attempts < ATTEMPTS)
                    {
                        attempts++;
                        printf ("timed out: %d tries remain.\n", ATTEMPTS - attempts);
                        alarm(TIMEOUT);
                        break;
                    }
                    else
                        error("No Response");
                }
                else
                    error ("recvfrom() failed");
            }
            //printf("Recieved ACK: %d\n", ntohl (ackdg.seq));
            float lossrv;
            float corrrv;
            
            while(1)
            {
                if (keep_going == 0)
                {
                    printf ("timed out\n");
                    keep_going = 1;
                    break;
                }
                
                int ackdgType = ntohl (ackdg.dgtype);
                int ackdgSeq = ntohl(ackdg.seq);
                
                
                lossrv = drand48();
                corrrv = drand48();
                //printf("%f\n",corrrv);
                if (probLoss > lossrv)
                {
                    printf("Lost Packet %d\n",ackdg.seq);
                }
                else if(probCrpt > corrrv)
                {
                    printf("Corrupt Packet %d\n", ackdg.seq);
                }
                
                if (ntohl(ackdg.seq) > 0 && ntohl(ackdg.dgtype)== ACK && probLoss <= lossrv && probCrpt <= corrrv)//when we get something
                {
                    //printf("Got Something\n");
                    
                    if (ackdg.seq > lastRecv && ackdgType == ACK)
                    {
                        int packets_to_send = ackdg.seq - lastRecv;
                        if (keep_going == 0)
                        {
                            printf ("timed out\n");
                            keep_going = 1;
                            break;
                        }
                        printf("Recieved ACK: %d\n", ackdg.seq);
                        lastRecv = ackdg.seq;
                        loops=0;
                        attempts=0;
                        
                        keep_going = 1;
                        alarm(TIMEOUT);
                        int p;
                        if(lastRecv+Cwnd-1 <= numPackets)
                        {
                            for (p = 0; p < packets_to_send; p++) {
                                int dgbytes_sent = sendto(sockdg, &packetArray[Cwnd-packets_to_send+lastRecv+p], sizeof(packetdg), 0, (struct sockaddr *) &cli_addr, sizeof (serv_addr));
                            
                            printf("PACKET SEQ NO: %d\n", ntohl(packetArray[Cwnd-packets_to_send+lastRecv+p].seq));
                            printf("bytes sent: %d\n", dgbytes_sent);
                            if(dgbytes_sent != sizeof(packetdg))
                            {
                                printf("error with sendto.\n");
                            }
                            lastSent = (Cwnd-1+lastRecv);
                            }
                            
                        }
                        if (ackdg.seq == (numPackets+1))
                        {
                            
                            //file completelly sent
                            
                            free(packetArray);
                            printf("Array freed\n");
                            exit(0); //
                        }
                        
                        
                        
                        
                    }
                }
                recvfrom (sockdg, &ackdg, sizeof(ackdg), 0,
                          (struct sockaddr *) &cli_addr, &clilen);
            }
            
        }
        
        
        
    } /* end of while */
    
    return 0; /* we never get here */
}


