#include <stdio.h>
#include <stdbool.h>
#include <sys/mman.h> 
#include <fcntl.h> 
#include <unistd.h>
#include <stdint.h>
#define _BSD_SOURCE


#define RADIO_TUNER_FAKE_ADC_PINC_OFFSET 0
#define RADIO_TUNER_TUNER_PINC_OFFSET 1
#define RADIO_TUNER_CONTROL_REG_OFFSET 2
#define RADIO_TUNER_TIMER_REG_OFFSET 3
#define RADIO_PERIPH_ADDRESS 0x43c00000
#define FIFO_PERIPH_ADDRESS 0x43c10000
#define FIFO_COUNT_OFFSET 0
#define FIFO_DATA_OFFSET 1

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

int main()
{

    printf("\r\n\r\n\r\nFinal Project Milestone: Daniel Connolly\r\n");

    // first, get a pointer to the peripheral base address using /dev/mem and the function mmap
    volatile unsigned int *full_radio = get_a_pointer(RADIO_PERIPH_ADDRESS);
    volatile unsigned int *sample_fifo = get_a_pointer(FIFO_PERIPH_ADDRESS);
    
    *(full_radio+RADIO_TUNER_CONTROL_REG_OFFSET) = 0; // make sure radio isn't in reset

    tune_radio(full_radio,30e6);
    set_adc_freq(full_radio,30000800);

    int samples_per_packet = 256;
    int num_samples_read = 0;

    printf("Reading 10 seconds of data...\r\n");
    while (1)
    {
        int fifo_count = (int) get_fifo_count(sample_fifo);
        for (int i = 0; i < fifo_count; i++)
        {
            uint32_t fifo_data = get_fifo_sample(sample_fifo);
            num_samples_read++;
            if (num_samples_read > 480e3)
            {
                printf("Finished!\r\n");
                return 0;
            }
        }
    }

    return 0;
}
