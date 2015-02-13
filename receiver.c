/* A simple server in the internet domain using TCP
 The port number is passed as an argument
 This version runs forever, forking off a separate
 process for each connection
 */
#include <sys/types.h>   // definitions of a number of data types used in socket.h and netinet/in.h
#include <sys/socket.h>  // definitions of structures needed for sockets, e.g. sockaddr
#include <arpa/inet.h>  // constants and structures needed for internet domain addresses, e.g. sockaddr_in
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>	/* for the waitpid() system call */
#include <signal.h>	/* signal name macros, and the kill() prototype */
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>

#define CHUNKSIZE 900
#define PACKETSIZE 920
#define ATTEMPTS  10

#define REQUEST 1
#define DATA 2
#define ACK 3
#define LAST 4

int attempts = 0;



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

void sig_handler(int s)
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
    int sockdg, portno, pid;
    unsigned int rcv_size;
    float probLoss, probCrpt;
    socklen_t clilen;
    struct sockaddr_in serv_addr, recv_from;
    struct sigaction sa;          // for signal SIGCHLD
    char file_to_rcv[CHUNKSIZE];
    
    if (argc < 5 || argc > 6) {
        fprintf (stderr, "Usage:  %s <sender hostname> <Sender Port Number> <filename> <Prob Loss> <Prob Corr>\n", argv[0]);
        exit (1);
    }
    
    serv_addr.sin_addr.s_addr = inet_addr(argv[1]);
    portno = atoi(argv[2]);
    strcpy(file_to_rcv,argv[3]);
    probLoss = atof(argv[4]);
    probCrpt = atof(argv[5]);
    
    srand48(time(NULL));
    
    sockdg = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sockdg < 0)
        error("ERROR opening socket");
    
    bzero((char *) &serv_addr, sizeof(serv_addr));
    
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(portno);
    
    /****** Set alarm signal handler ******/
    sa.sa_handler = sig_handler;
    sigfillset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGALRM, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }
    /*********************************/
    char initial_buf[CHUNKSIZE];
    int response_len = 0;
    int recvd_last_packet = 0;
    int expected_seq = 1;
    FILE *file;
    char location[CHUNKSIZE] = ""; //assume small size for filename
    strcat(location,"receiver_files/");
    strcat(location,file_to_rcv);
    strcat(location,"\0"); //location file will be saved on receiver end
    
    printf("requesting: %s.\n", file_to_rcv);
    
    struct Packet requestdg;
    struct Packet datadg;
    struct Packet ackdg;
    
    //printf("Packets request, data, and ack declared\n");
    
    memset(&requestdg, 0, sizeof(requestdg));
   // printf("first memset done\n");
    memset(&datadg, 0, sizeof(datadg));
    memset(&ackdg, 0, sizeof(ackdg));
    
    //printf("memset complete\n");
    
    requestdg.len = htonl(0);
    requestdg.dest_port = htons(portno);
    requestdg.dgtype = htons(REQUEST);
    requestdg.seq = htons(expected_seq);
    
    //Create Request Packet
    
    
    requestdg.len = htonl(strlen(file_to_rcv));
    requestdg.dest_port = htonl(portno);
    requestdg.dgtype = htonl(REQUEST);
    requestdg.seq = htonl(0);
    memcpy(&requestdg.data, &file_to_rcv, CHUNKSIZE);
    //printf("requestdg data: %s.\n", requestdg.data);
    //printf("recvd_last_packet: %d.\n", recvd_last_packet);
    
    {
        //printf("notice: %s\n", "while loop entered");
        if (attempts >= ATTEMPTS)
        {
            error("Request not recognized");
            exit(1);
        }
        
        //Send request
        
        int req_bytes = sendto (sockdg, &requestdg, sizeof(requestdg), 0, (struct sockaddr *) &serv_addr, sizeof(serv_addr));
        
      //  printf("notice: %s\n", "sendto done");
        printf("%d bytes sent\n", req_bytes);
        
        expected_seq=1;
        
        printf("Expecting packet: %d\n", expected_seq);
        
        response_len = recvfrom (sockdg, &datadg, sizeof(datadg), 0,(struct sockaddr *) &serv_addr, &clilen);
        
        
        
        //datadg.dgtype = ntohl(datadg.dgtype);
        datadg.len = ntohl(datadg.len);
        //datadg.seq = ntohl(datadg.seq);
        
        int spare_seq = ntohl(datadg.seq);
        int spare_type = ntohl(datadg.dgtype);
        
        //printf("notice: %s\n", "recvfrom done");
        printf("%d bytes received\n", response_len);
        
        
        printf("packet: %d\n", spare_seq);
        
        if (response_len > 0 /*&& datadg.seq == expected_seq*/ && spare_type == DATA)
        {
            
            attempts = 0;
            
            printf("opening file: %s\n", location);
            
            file = fopen(location,"wb");
            if (!file) {
                error("failed to open file");
                exit(1);
            }
            
            printf("notice: %s\n", "file opened");
            /*
             fwrite(datadg.data,sizeof(datadg.data),1,file);
             printf("notice: %s\n", "sending first ACK");
             ackdg.seq=datadg.seq;
             ackdg.dgtype = htonl(ACK);
             int ack_bytes = sendto(sockdg, &ackdg, sizeof(ackdg), 0, (struct sockaddr *) &serv_addr, clilen);
             printf("%d ACK bytes sent\n", ack_bytes);
             expected_seq++;
             */
            while (1)
            {
                float psu_rand1 = drand48();
                float psu_rand2 = drand48();
                
                int lost_flag = 0;
                int corr_flag = 0;
                
          //      printf("rand1: %f\nrand2: %f\n", psu_rand1, psu_rand2);
                
                if(probLoss > psu_rand1)
                {
                    printf("Packet %d was LOST.\n", ntohl(datadg.seq));
                    lost_flag = 1;
                }
                else if(probCrpt > psu_rand2)
                {
                    
                    printf("Packet %d was corrupt.\n", ntohl(datadg.seq));
                    corr_flag = 1;
                }
                
                if (attempts >= ATTEMPTS)
                {
                    error("Connection was lost\n");
                    fclose(file);
                    exit(1);
                }
                // Check if expected sequence number and add it to file and send ACK
                
                if (lost_flag == 0 && corr_flag == 0) {
                    
                    if (response_len > 0 && ntohl(datadg.seq) == expected_seq && (ntohl(datadg.dgtype) == DATA || ntohl(datadg.dgtype) == LAST))
                    {
                        printf("Received DATA packet: %d\n", expected_seq);
                        attempts=0;
                       // printf("writing: %s\n", datadg.data);
                       printf("****************WRITING TO FILE****************\n");
                        if(ntohl(datadg.dgtype) != LAST)
                            fwrite(datadg.data,sizeof(datadg.data),1,file);
                        else
                            fwrite(datadg.data, ntohl(datadg.len),1,file);

                        printf("Sending ACK: %d\n", expected_seq);
                        ackdg.seq = expected_seq;
                        ackdg.dgtype = htonl(ACK);
                        
                        if(ntohl(datadg.dgtype) != LAST)
                            sendto(sockdg, &ackdg, (sizeof (int) * 5), 0, (struct sockaddr *) &serv_addr, sizeof(serv_addr));
                        else
                        {
                            int spamLastAck = 0;
                            while(spamLastAck < 5)
                            {
                                 sendto(sockdg, &ackdg, (sizeof (int) * 5), 0, (struct sockaddr *) &serv_addr, sizeof(serv_addr));
                                 spamLastAck++;
                            }
                        }
                        expected_seq++;
                        
                    }
                    else
                    {
                        printf("Sending ACK: %d\n", (expected_seq-1));
                        ackdg.seq = (expected_seq-1);
                        ackdg.dgtype = htonl(ACK);
                        sendto(sockdg, &ackdg, (sizeof (int) * 5), 0, (struct sockaddr *) &serv_addr, sizeof(serv_addr));
                    }
                    
                    //If EOF type then exit
                    if (ntohl(datadg.dgtype) == LAST && ntohl(datadg.seq) == expected_seq - 1)
                    {
                        printf("notice: %s\n", "File was completely transfered");
                        fclose(file);
                        exit(1);
                    }
                }
                else
                {
                    lost_flag = 0;
                    corr_flag = 0;
                }
                printf("Expecting packet: %d\n", expected_seq);
                // Receive from sender
                response_len = recvfrom (sockdg, &datadg, sizeof(datadg), 0,(struct sockaddr *) &serv_addr, &clilen);
                //attempts++;
                
            }
        }
        else
        {
            printf("Sending ACK: %d\n", (expected_seq-1));
            ackdg.seq = (expected_seq-1);
            ackdg.dgtype = htonl(ACK);
            sendto(sockdg, &ackdg, (sizeof (int) * 5), 0, (struct
                                                           sockaddr *) &serv_addr, sizeof(serv_addr));
        }
        
        attempts++;
        
    } /* end of block */
    return 0; /* we never get here */
}

/*
 ./sender 1101 1 0 0
 ./receiver 127.0.0.1 1101 text.txt 0 0
 
 
 
 
 cd /Users/Tigerlight/Desktop/118_project2/project2.2
 ./sender 1101 1 0 0
 
 cd /Users/Tigerlight/Desktop/118_project2/project2.2
 ./receiver 127.0.0.1 1101 text.txt 0 0
 
 
 
 */

