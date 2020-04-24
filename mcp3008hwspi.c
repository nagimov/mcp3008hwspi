/*
mcp3008hwspi: fast MCP3008 reader for Raspberry Pi
License: https://github.com/nagimov/mcp3008hwspi/blob/master/LICENSE
Readme: https://github.com/nagimov/mcp3008hwspi/blob/master/README.md
*/

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <linux/spi/spidev.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define MAX_ADC_CH 8
int selectedChannels[MAX_ADC_CH];
int channels[MAX_ADC_CH];
char spidev_path[] = "/dev/spidev0.0";
const char codeVersion[5] = "0.0.1";
const int blocksDefault = 1;
const int blocksMax = 511;
const int channelDefault = 0;
const int samplesDefault = 1000;
const int freqDefault = 0;
const int clockRateDefault = 3600000;
const int clockRateMin = 1000000;
const int clockRateMax = 3600000;
const int coldSamples = 10000;


void printUsage() {
    printf("mcp3008hwspi (version %s) \n"
           "Reads data from MCP3008 ADC through hardware SPI interface on Raspberry Pi.\n"
           "Online help, docs & bug reports: <https://github.com/nagimov/mcp3008hwspi>\n"
           "\n"
           "Usage: mcp3008hwspi [OPTION]... \n"
           "Mandatory arguments to long options are mandatory for short options too.\n"
           "  -b, --block B        read B blocks per every scan of all specified channels,\n"
           "                       1 <= BPR <= %i (default: %i) [integer];\n"
           "                       multiple channels are always read as a single block;\n"
           "  -r, --clockrate CR   SPI clock rate, Hz, %i <= CR <= %i\n"
           "                       (default: %i) [integer];\n"
           "                       MCP3008 must be powered from 5V for 3.6MHz clock rate;\n"
           "  -c, --channels CH    read specified channels CH, 0 <= CH <= 7 (default: %i);\n"
           "                       multiple channels can be specified, e.g. -c 0123;\n"
           "                       all channels are read as a single block, e.g. if ran as\n"
           "                       <mcp3008hwspi -c 0123 -b 2>\n"
           "                       8 blocks are transmitted per SPI read (4 channels x 2);\n"
           "  -s, --save FILE      save data to specified FILE (if not specified, data is\n"
           "                       printed to stdout);\n"
           "  -n, --samples N      set the number of samples per channel to be read to N\n"
           "                       (default: %i samples) [integer];\n"
           "  -f, --freq FREQ      set the sampling rate to FREQ, samples per second\n"
           "                       (default: %i Hz) [integer];\n"
           "                       if set to 0, ADC is sampled at maximum achievable rate,\n"
           "                       if set to > 0, --block is reset to 1;\n"
           "\n"
           "Data is streamed in comma separated format, e. g.:\n"
           "  sample ch0,  value ch0,  sample ch1,  value ch1\n"
           "           0,       1023,           1,        512\n"
           "           2,       1022,           3,        513\n"
           "         ...,        ...,         ...,        ...\n"
           "  samples are (hopefully) equally spaced in time;\n"
           "  channels are read sequentially with equal time delays between samples;\n"
           "  value chX shows raw 10-bit integer readback from channel X;\n"
           "  average sampling rate is written to both stdout and output file header.\n"
           "\n"
           "Exit status:\n"
           "  0  if OK\n"
           "  1  if error occurred while reading or wrong cmdline arguments.\n"
           "\n"
           "Example:\n"
           "  mcp3008hwspi  -r 3600000  -c 0123  -s out.csv  -f 0  -n 1000  -b 25\n"
           "                      ^         ^         ^        ^      ^        ^\n"
           "                      |         |         |        |      |        |\n"
           "  3.6 MHz SPI clock --+         |         |        |      |        |\n"
           "  read channels 0, 1, 2 and 3 --+         |        |      |        |\n"
           "  save data to output file 'out.csv' -----+        |      |        |\n"
           "  set sampling frequency to max achievable rate ---+      |        |\n"
           "  read 1000 samples per channel (1000 x 4 = 4000 total) --+        |\n"
           "  read channels in blocks of 25 (25 x 4 = 100 blocks per SPI read)-+\n"
           "",
           codeVersion, blocksMax, blocksDefault, clockRateMin, clockRateMax, clockRateDefault,
           channelDefault, samplesDefault, freqDefault);
}


int main(int argc, char *argv[]) {
    int i, j;
    int ch_len = 0;
    int bSave = 0;
    char vSave[256] = "";
    int vSamples = samplesDefault;
    double vFreq = freqDefault;
    int vBlocks = blocksDefault;
    int vClockRate = clockRateDefault;
    int vChannel;
    for (i = 1; i < argc; i++) {
        if ((strcmp(argv[i], "-b") == 0) || (strcmp(argv[i], "--block") == 0)) {
            if (i + 1 <= argc - 1) {
                i++;
                vBlocks = atoi(argv[i]);
                if ((vBlocks < 1) || (vBlocks > blocksMax)) {
                    printf("Wrong blocks per read value specified!\n\n");
                    printUsage();
                    return 1;
                }
            } else {
                printUsage();
                return 1;
            }
        } else if ((strcmp(argv[i], "-r") == 0) || (strcmp(argv[i], "--clockrate") == 0)) {
            if (i + 1 <= argc - 1) {
                i++;
                vClockRate = atoi(argv[i]);
                if ((vClockRate < clockRateMin) || (vClockRate > clockRateMax)) {
                    printf("Wrong clock rate value specified!\n\n");
                    printUsage();
                    return 1;
                }
            } else {
                printUsage();
                return 1;
            }
        } else if ((strcmp(argv[i], "-c") == 0) || (strcmp(argv[i], "--channels") == 0)) {
            if (i + 1 <= argc - 1) {
                i++;
                ch_len = strlen(argv[i]);
                memset(selectedChannels, 0, sizeof(selectedChannels));
                for (j = 0; j < ch_len; j++) {
                    vChannel = argv[i][j] - '0';
                    if ((vChannel < 0) || (vChannel > 7)) {
                        printf("Wrong channel %d specified!\n\n", vChannel);
                        printUsage();
                        return 1;
                    }
                    if (selectedChannels[vChannel]) {
                        printf("Channel %d listed more then once!\n", vChannel);
                        printUsage();
                    }
                    selectedChannels[vChannel] = 1;
                    channels[j] = vChannel;
                }
            } else {
                printUsage();
                return 1;
            }
        } else if ((strcmp(argv[i], "-s") == 0) || (strcmp(argv[i], "--save") == 0)) {
            bSave = 1;
            if (i + 1 <= argc - 1) {
                i++;
                strcpy(vSave, argv[i]);
            } else {
                printUsage();
                return 1;
            }
        } else if ((strcmp(argv[i], "-n") == 0) || (strcmp(argv[i], "--samples") == 0)) {
            if (i + 1 <= argc - 1) {
                i++;
                vSamples = atoi(argv[i]);
            } else {
                printUsage();
                return 1;
            }
        } else if ((strcmp(argv[i], "-f") == 0) || (strcmp(argv[i], "--freq") == 0)) {
            if (i + 1 <= argc - 1) {
                i++;
                vFreq = atoi(argv[i]);
                vBlocks = 1;
                if (vFreq < 0) {
                    printf("Wrong sampling rate specified!\n\n");
                    printUsage();
                    return 1;
                }
            } else {
                printUsage();
                return 1;
            }
        } else {
            printUsage();
            return 1;
        }
    }
    if (ch_len == 0) {
        ch_len = 1;
        channels[0] = channelDefault;
    }
    int microDelay = 0;
    if (vFreq != 0) {
        microDelay = 1000000 / vFreq;
    }
    int count = 0;
    int fd = 0;
    int val;
    struct timeval start, end;
    double diff;
    double rate;
    int *data;
    data = malloc(ch_len * vSamples * sizeof(int));
    struct spi_ioc_transfer *tr = 0;
    unsigned char *tx = 0;
    unsigned char *rx = 0;
    tr = (struct spi_ioc_transfer *)malloc(ch_len * vBlocks * sizeof(struct spi_ioc_transfer));
    if (!tr) {
        perror("malloc");
        goto loop_done;
    }
    tx = (unsigned char *)malloc(ch_len * vBlocks * 4);
    if (!tx) {
        perror("malloc");
        goto loop_done;
    }
    rx = (unsigned char *)malloc(ch_len * vBlocks * 4);
    if (!rx) {
        perror("malloc");
        goto loop_done;
    }
    memset(tr, 0, ch_len * vBlocks * sizeof(struct spi_ioc_transfer));
    memset(tx, 0, ch_len * vBlocks);
    memset(rx, 0, ch_len * vBlocks);
    for (i = 0; i < vBlocks; i++) {
        for (j = 0; j < ch_len; j++) {
            tx[(i * ch_len + j) * 4] = 0x60 | (channels[j] << 2);
            tr[i * ch_len + j].tx_buf = (unsigned long)&tx[(i * ch_len + j) * 4];
            tr[i * ch_len + j].rx_buf = (unsigned long)&rx[(i * ch_len + j) * 4];
            tr[i * ch_len + j].len = 3;
            tr[i * ch_len + j].speed_hz = vClockRate;
            tr[i * ch_len + j].cs_change = 1;
        }
    }
    tr[ch_len * vBlocks - 1].cs_change = 0;
    fd = open(spidev_path, O_RDWR);
    if (fd < 0) {
        perror("open()");
        printf("%s\n", spidev_path);
        goto loop_done;
    }
    while (count < coldSamples) {
        if (ioctl(fd, SPI_IOC_MESSAGE(ch_len * vBlocks), tr) < 0) {
            perror("ioctl");
            goto loop_done;
        }
        count += ch_len * vBlocks;
    }
    count = 0;
    if (gettimeofday(&start, NULL) < 0) {
        perror("gettimeofday: start");
        return 1;
    }
    while (count < ch_len * vSamples) {
        if (ioctl(fd, SPI_IOC_MESSAGE(ch_len * vBlocks), tr) < 0) {
            perror("ioctl");
            goto loop_done;
        }
        for (i = 0, j = 0; i < ch_len * vBlocks; i++, j += 4) {
            val = (rx[j + 1] << 2) + (rx[j + 2] >> 6);
            data[count + i] = val;
        }
        count += ch_len * vBlocks;
        if (microDelay > 0) {
            usleep(microDelay);
        }
    }
    if (count > 0) {
        if (gettimeofday(&end, NULL) < 0) {
            perror("gettimeofday: end");
        } else {
            if (end.tv_usec > start.tv_usec) {
                diff = (double)(end.tv_usec - start.tv_usec);
            } else {
                diff = (double)((1000000 + end.tv_usec) - start.tv_usec);
                end.tv_sec--;
            }
            diff /= 1000000.0;
            diff += (double)(end.tv_sec - start.tv_sec);
            if (diff > 0.0)
                rate = count / diff;
            else
                rate = 0.0;
        }
    }
    printf("%0.2lf seconds, %d samples, %0.2lf Hz total sample rate, %0.2lf Hz per-channel sample "
           "rate\n"
           "",
           diff, count, rate, rate / ch_len);
    if (bSave == 1) {
        printf("Writing to the output file...\n");
        FILE *f;
        f = fopen(vSave, "w");
        fprintf(f,
                "# %0.2lf seconds, %d samples, %0.2lf Hz total sample rate, %0.2lf Hz per-channel "
                "sample rate\n"
                "",
                diff, count, rate, rate / ch_len);
        fprintf(f, "sample ch%d, value ch%d", channels[0], channels[0]);
        if (ch_len > 1) {
            for (i = 1; i < ch_len; i++) {
                fprintf(f, ", sample ch%d, value ch%d", channels[i], channels[i]);
            }
        }
        fprintf(f, "\n");
        for (i = 0; i < vSamples; i++) {
            fprintf(f, "%d, %d", i * ch_len, data[i * ch_len]);
            if (ch_len > 1) {
                for (j = 1; j < ch_len; j++) {
                    fprintf(f, ", %d, %d", i * ch_len + j, data[i * ch_len + j]);
                }
            }
            fprintf(f, "\n");
        }
        fclose(f);
    } else {
        printf("sample ch%d, value ch%d", channels[0], channels[0]);
        if (ch_len > 1) {
            for (i = 1; i < ch_len; i++) {
                printf(", sample ch%d, value ch%d", channels[i], channels[i]);
            }
        }
        printf("\n");
        for (i = 0; i < vSamples; i++) {
            printf("%d, %d", i * ch_len, data[i * ch_len]);
            if (ch_len > 1) {
                for (j = 1; j < ch_len; j++) {
                    printf(", %d, %d", i * ch_len + j, data[i * ch_len + j]);
                }
            }
            printf("\n");
        }
    }
loop_done:
    if (fd)
        close(fd);
    if (rx)
        free(rx);
    if (tx)
        free(tx);
    if (tr)
        free(tr);
    return 0;
}
