#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h> 
#include <fcntl.h> 
#include <unistd.h>
#include <sys/socket.h>
#include <stdint.h>
#include <netinet/in.h>
#include <arpa/inet.h>

int main(int argc, char *argv[])
{

    printf("(%d) arguments given.\r\n", argc);
    
    if (argc != 3)
    {
        printf("Three arguments required, %d given.\r\n", argc);
        exit(EXIT_FAILURE);
    }

    char *ip_addr = argv[1];
    uint16_t num_packets = (uint16_t) atoi(argv[2]);

    int sockfd;
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(25344);
    server_addr.sin_addr.s_addr = inet_addr(ip_addr);

    sockfd = socket(AF_INET, SOCK_DGRAM, 0); // IPPROTO_UDP

    if (sockfd < 0)
    {
        printf("socket creation failed");
        exit(EXIT_FAILURE);
    }

    int N = 512;
    uint16_t packet_count = 0;
    uint16_t packet_buff[N + 1];


    for (int16_t i = 0; i < N-1; i = i + 2)
    {
        packet_buff[1 + i] = i;
        packet_buff[2 + i] = (-1)*i;
    }

    for (uint16_t j = 0; j < num_packets; j++)
    {
        packet_buff[0] = j;
        sendto(sockfd, packet_buff, sizeof(uint16_t) * (N + 1), 0,
            (struct sockaddr *) &server_addr, sizeof(server_addr));
    }

    return 0;
}
