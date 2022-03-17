// origin code: KYG-yaya573142/fibdrv/client_statistic.c

#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>


#define FIB_DEV "/dev/fibonacci"
#define SAMPLE_SIZE 50
#define MODE_CNT 2
#define FIB_OFFSET 100

int main()
{
    FILE *fp = fopen("./plot_input_statistic", "w");
    int fd = open(FIB_DEV, O_RDWR);
    if (fd < 0) {
        perror("Failed to open character device");
        exit(1);
    }

    /* for each F(i), measure SAMPLE_SIZE times of data and
     * remove outliers based on the 95% confidence level
     */
    for (int i = 0; i <= FIB_OFFSET; i++) {
        lseek(fd, i, SEEK_SET);
        double kernel_time[MODE_CNT][SAMPLE_SIZE] = {0};
        double mean[MODE_CNT] = {0.0}, sd[MODE_CNT] = {0.0},
               result[MODE_CNT] = {0.0};
        int count[MODE_CNT] = {0};

        for (int n = 0; n < SAMPLE_SIZE; n++) { /* sampling */
            /* get the runtime in kernel space here */
            for (int m = 0; m < MODE_CNT; ++m) {
                kernel_time[m][n] = (double) write(fd, NULL, m);
                mean[m] += kernel_time[m][n]; /* sum */
            }
        }
        for (int m = 0; m < MODE_CNT; ++m) {
            mean[m] /= SAMPLE_SIZE; /* mean */
        }

        for (int n = 0; n < SAMPLE_SIZE; n++) {
            for (int m = 0; m < MODE_CNT; ++m) {
                sd[m] += (kernel_time[m][n] - mean[m]) *
                         (kernel_time[m][n] - mean[m]);
            }
        }
        for (int m = 0; m < MODE_CNT; ++m) {
            sd[m] = sqrt(sd[m] / (SAMPLE_SIZE - 1)); /* standard deviation */
        }

        for (int n = 0; n < SAMPLE_SIZE;
             n++) { /* remove outliers for 95% confidence interval */
            for (int m = 0; m < MODE_CNT; ++m) {
                if (kernel_time[m][n] <= (mean[m] + 2 * sd[m]) &&
                    kernel_time[m][n] >= (mean[m] - 2 * sd[m])) {
                    result[m] += kernel_time[m][n];
                    count[m]++;
                }
            }
        }
        for (int m = 0; m < MODE_CNT; ++m) {
            result[m] /= count[m];
        }

        // column 1
        fprintf(fp, "%d ", i);

        // column 2 ~ // column MODE_CNT
        for (int m = 0; m < MODE_CNT; ++m) {
            fprintf(fp, "%.2lf ", result[m]);
        }
        fprintf(fp, "samples: ");
        for (int m = 0; m < MODE_CNT; ++m) {
            fprintf(fp, "%d ", count[m]);
        }
        fprintf(fp, "\n");
    }
    close(fd);
    fclose(fp);
    return 0;
}