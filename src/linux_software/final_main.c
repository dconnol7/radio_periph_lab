/*
* final_main.c: Final Project Application
*   Build with `gcc -pthread final_main.c -o final_main`
*/

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/mman.h> 
#include <fcntl.h> 
#include <unistd.h>
#include <stdint.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <string.h>
// #define _BSD_SOURCE

#define RADIO_TUNER_FAKE_ADC_PINC_OFFSET 0
#define RADIO_TUNER_TUNER_PINC_OFFSET 1
#define RADIO_TUNER_CONTROL_REG_OFFSET 2
#define RADIO_TUNER_TIMER_REG_OFFSET 3
#define RADIO_PERIPH_ADDRESS 0x43c00000
#define FIFO_PERIPH_ADDRESS 0x43c10000
#define FIFO_COUNT_OFFSET 0
#define FIFO_DATA_OFFSET 1

pthread_mutex_t stream_mutex;
bool exit_thread_flag = false;

struct thread_args
{
    int sockfd;
    char *ip_addr;
    volatile unsigned int *sample_fifo;
};

// 2^27 = 134217728
static float phase_increment_factor = (134217728.0 / 125000000);

// the below code uses a device called /dev/mem to get a pointer to a physical
// address.  We will use this pointer to read/write the custom peripheral
volatile unsigned int * get_a_pointer(unsigned int phys_addr)
{

	int mem_fd = open("/dev/mem", O_RDWR | O_SYNC); 
	void *map_base = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, mem_fd, phys_addr); 
	volatile unsigned int *radio_base = (volatile unsigned int *)map_base; 
	return (radio_base);
}

uint32_t get_fifo_count(volatile unsigned int *fifo_ptr)
{
    uint32_t fifo_count = *(fifo_ptr + FIFO_COUNT_OFFSET);
    return fifo_count;
}

uint32_t get_fifo_sample(volatile unsigned int *fifo_ptr)
{
    uint32_t sample = *(fifo_ptr + FIFO_DATA_OFFSET);
    return sample;
}

void tune_radio(volatile unsigned int *radio_ptr, float frequency_hz)
{
    // phase_increment = -1 * f_output * (2^N / Fs)
    //  N = 27
    //  Fs = 125e6
    //  F_output = frequency_hz
	int32_t pinc = ((int32_t) (((-1)*frequency_hz) * phase_increment_factor));
	*(radio_ptr + RADIO_TUNER_TUNER_PINC_OFFSET) = pinc;
}

void set_adc_freq(volatile unsigned int* radio_ptr, float frequency_hz)
{
    // phase_increment = f_output * (2^N / Fs)
    //  N = 27
    //  Fs = 125e6
    //  F_output = frequency_hz
	int32_t pinc = ((int32_t) (frequency_hz * phase_increment_factor));
	*(radio_ptr + RADIO_TUNER_FAKE_ADC_PINC_OFFSET) = pinc;
}

void* stream_packets(void *args)
{
    struct thread_args *socket_args = args;

    volatile unsigned int *sample_fifo = socket_args->sample_fifo;

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(25344);
    server_addr.sin_addr.s_addr = inet_addr(socket_args->ip_addr);

    uint16_t packet_count = 0;
    int samples_per_packet = 256;
    bool thread_should_exit = 0;

    int N = samples_per_packet * 2;
    while (1)
    {
        int fifo_count = (int) get_fifo_count(sample_fifo);
        if (fifo_count > samples_per_packet)
        {
            uint16_t packet_buff[N + 1];
            packet_buff[0] = packet_count;
            for (int i = 0; i < samples_per_packet; i++)
            {
                uint32_t fifo_data = get_fifo_sample(sample_fifo);
                // Q is upper 16 bits, I is lower 16 bits
                packet_buff[1 + i*2] = (uint16_t) (fifo_data & 0x0000FFFF);
                packet_buff[2 + i*2] = (uint16_t) ((fifo_data & 0xFFFF0000) >> 16);
            }
            sendto(socket_args->sockfd, packet_buff, sizeof(uint16_t) * (N + 1), 0,
                (struct sockaddr *) &server_addr, sizeof(server_addr));
            packet_count++;
        }
        pthread_mutex_lock(&stream_mutex);
        thread_should_exit = exit_thread_flag;
        pthread_mutex_unlock(&stream_mutex);
        if (thread_should_exit)
        {
            break;
        }
    }
    return NULL;
}

void show_menu()
{
    printf("Welcome to the SDR Final Project Menu\r\n");
    printf("Press f to tune the ADC to a new frequency [default: 0 Hz]\r\n");
    printf("Press t to tune the tuner to a new frequency [default: 0 Hz]\r\n");
    printf("Press e to toggle enable/disable of streaming radio output to ethernet [default: disabled]\r\n");
    printf("Press i to update the destination IP address of UDP packets (port is always 25344) [default: 127.0.0.1]\r\n");
    printf("Press s to show the current settings\r\n");
    printf("Press x to exit the program\r\n");
    printf("Press r to show this menu again\r\n");
    printf("*NOTE*: You must hit [ENTER] to submit your input\r\n");
}

int main(int argc)
{

    printf("\r\nFinal Project: Daniel Connolly\r\n");

    // first, get a pointer to the peripheral base address using /dev/mem and the function mmap
    volatile unsigned int *full_radio = get_a_pointer(RADIO_PERIPH_ADDRESS);
    volatile unsigned int *sample_fifo = get_a_pointer(FIFO_PERIPH_ADDRESS);
    
    *(full_radio+RADIO_TUNER_CONTROL_REG_OFFSET) = 0; // make sure radio isn't in reset

    float tune_freq = 0;
    float adc_freq = 0;
    char ip_addr[15] = "127.0.0.1";
    tune_radio(full_radio,tune_freq);
    set_adc_freq(full_radio,adc_freq);

    int sockfd = socket(AF_INET, SOCK_DGRAM, 0); // IPPROTO_UDP

    if (sockfd < 0)
    {
        printf("socket creation failed");
        exit(EXIT_FAILURE);
    }
    
    show_menu();

    char user_input[15];
    bool streaming_enabled = false;

    struct thread_args socket_settings;
    socket_settings.sockfd = sockfd;
    socket_settings.ip_addr = ip_addr;
    socket_settings.sample_fifo = sample_fifo;
    pthread_t stream_thread_id;

    while (1)
    {
        scanf("%s", user_input);
        if (strcmp(user_input, "f") == 0)
        {
            printf("Enter the desired ADC frequency in Hz: ");
            scanf("%f", &adc_freq);
            set_adc_freq(full_radio, adc_freq);
            printf("ADC frequency set to %f\r\n", adc_freq);
        }
        else if (strcmp(user_input, "t") == 0)
        {
            printf("Enter the desired Tuner frequency in Hz: ");
            scanf("%f", &tune_freq);
            tune_radio(full_radio, tune_freq);
            printf("Tune frequency set to %f\r\n", tune_freq);
        }
        else if (strcmp(user_input, "e") == 0)
        {
            if (!streaming_enabled)
            {
                pthread_mutex_lock(&stream_mutex);
                exit_thread_flag = false;
                pthread_mutex_unlock(&stream_mutex);

                pthread_create(&stream_thread_id, NULL, stream_packets, (void *)&socket_settings);
                printf("Streaming enabled to IP: %s\r\n", ip_addr);
                streaming_enabled = true;
            }
            else
            {
                pthread_mutex_lock(&stream_mutex);
                exit_thread_flag = true;
                pthread_mutex_unlock(&stream_mutex);
                pthread_join(stream_thread_id, NULL);
                printf("Streaming disabled\r\n");
                streaming_enabled = false;
            }
        }
        else if (strcmp(user_input, "i") == 0)
        {
            printf("Enter the destination IP: ");
            scanf("%s", ip_addr);
            socket_settings.ip_addr = ip_addr;

            if (streaming_enabled)
            {
                printf("Restarting streaming to new IP\r\n");
                // Stop streaming
                pthread_mutex_lock(&stream_mutex);
                exit_thread_flag = true;
                pthread_mutex_unlock(&stream_mutex);
                pthread_join(stream_thread_id, NULL);

                // Start streaming
                pthread_mutex_lock(&stream_mutex);
                exit_thread_flag = false;
                pthread_mutex_unlock(&stream_mutex);
                pthread_create(&stream_thread_id, NULL, stream_packets, (void *)&socket_settings);
            }
            printf("IP address set to %s\r\n", ip_addr);
        }
        else if (strcmp(user_input, "s") == 0)
        {
            printf("\r\nSettings: \r\n");
            printf("  Tuner frequency : %f\r\n", tune_freq);
            printf("  ADC frequency   : %f\r\n", adc_freq);
            printf("  Streaming IP    : %s\r\n", ip_addr);
            printf("  Stream Enable   : %d\r\n", streaming_enabled);
        }
        else if (strcmp(user_input, "x") == 0)
        {
            printf("Exiting program.\r\n");
            break;
        }
        else if (strcmp(user_input, "r") == 0)
        {
            show_menu();
        }
        else
        {
            printf("Unknown key pressed\r\n");
            show_menu();
        }
    }

    if (streaming_enabled)
    {
        pthread_mutex_lock(&stream_mutex);
        exit_thread_flag = true;
        pthread_mutex_unlock(&stream_mutex);
        pthread_join(stream_thread_id, NULL);
        streaming_enabled = false;
    }

    printf("Setting tune/adc frequencies to 0 and putting radio in reset\r\n");
    *(full_radio+RADIO_TUNER_CONTROL_REG_OFFSET) = 1; // Put radio in reset
    tune_radio(full_radio, 0);
    set_adc_freq(full_radio, 0);

    printf("Done!\r\n");
    return 0;
}
