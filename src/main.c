#include <time.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <getopt.h>
#include <stdint.h>
#include "utility.h"
#include "star.h"
#include "float.h"
#include <pthread.h>

#define NUM_STARS 30000
#define MAX_LINE 1024
#define DELIMITER " \t\n"

struct Star star_array[NUM_STARS];
uint8_t (*distance_calculated)[NUM_STARS];

double distance = 0;

//Initialized mutexes for distance, min , and max
pthread_mutex_t distance_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t min_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t max_mutex = PTHREAD_MUTEX_INITIALIZER;

double min = FLT_MAX;
double max = FLT_MIN;

// Struct that holds positive values for start and end index
// of a thread
typedef struct ThreadData{
    uint32_t start_idx;
    uint32_t end_idx;
} ThreadData;

void showHelp() 
{
    printf("Use: findAngular [options]\n");
    printf("Where options are:\n");
    printf("-t          Number of threads to use\n");
    printf("-h          Show this help\n");
}

//Merged the determineAverageAngularDistance function 
//within the void * func
void *func(void *arg) 
{
    //Casts the thread ID into a struct
    ThreadData *thread_data = (ThreadData *)arg;
    
    //Initialized local variables for min, max and sum
    double temp_min = FLT_MAX;
    double temp_max = FLT_MIN;
    double temp_sum = 0;

    uint64_t count = 0;

    for (uint32_t i = thread_data->start_idx; i < thread_data->end_idx; i++) 
    {
        for (uint32_t j = i + 1; j < NUM_STARS; j++) 
        {
            double temp_distance = calculateAngularDistance(
                star_array[i].RightAscension, star_array[i].Declination,
                star_array[j].RightAscension, star_array[j].Declination);

            if (temp_min > temp_distance) {
                temp_min = temp_distance;
            }

            if (temp_max < temp_distance) {
                temp_max = temp_distance;
            }

            temp_sum += temp_distance;
            count++;
        }
    }

    pthread_mutex_lock(&distance_mutex);
    distance += temp_sum;
    pthread_mutex_unlock(&distance_mutex);

    pthread_mutex_lock(&min_mutex);
    if (min > temp_min) 
    {
        min = temp_min;
    }
    pthread_mutex_unlock(&min_mutex);

    pthread_mutex_lock(&max_mutex);
    if (max < temp_max) 
    {
        max = temp_max;
    }
    pthread_mutex_unlock(&max_mutex);

    pthread_exit(NULL);
}

int main(int argc, char **argv)
{
    FILE *fp;

    uint32_t star_count = 0;
    uint32_t n;

    int num_threads = 0;

    clock_t start_t, end_t;
    double total_t;

    distance_calculated = malloc(sizeof(uint8_t[NUM_STARS][NUM_STARS]));

    if( distance_calculated == NULL )
    {
        uint64_t num_stars = NUM_STARS;
        uint64_t size = num_stars * num_stars * sizeof(uint8_t);
        printf("Could not allocate %ld bytes\n", size);
        exit( EXIT_FAILURE );
    }

    int i, j;
    
    for (i = 0; i < NUM_STARS; i++)
    {
        for (j = 0; j < NUM_STARS; j++)
        {
            distance_calculated[i][j] = 0;
        }
    }

    for( n = 1; n < argc; n++ )          
    {
        if( strcmp(argv[n], "-help" ) == 0 )
        {
        showHelp();
        exit(0);
        }
    }

    fp = fopen( "data/tycho-trimmed.csv", "r" );

    if( fp == NULL )
    {
        printf("ERROR: Unable to open the file data/tycho-trimmed.csv\n");
        exit(1);
    }

    char line[MAX_LINE];
    while (fgets(line, 1024, fp))
    {
        uint32_t column = 0;

        char* tok;
        for (tok = strtok(line, " ");
                tok && *tok;
                tok = strtok(NULL, " "))
        {
        switch( column )
        {
            case 0:
                star_array[star_count].ID = atoi(tok);
                break;
        
            case 1:
                star_array[star_count].RightAscension = atof(tok);
                break;
        
            case 2:
                star_array[star_count].Declination = atof(tok);
                break;

            default: 
                printf("ERROR: line %d had more than 3 columns\n", star_count );
                exit(1);
                break;
        }
        column++;
        }
        star_count++;
    }
    printf("%d records read\n", star_count );

    //Checks the commandline for a -t parameter
    //and grabs the number of specified threads
    for (int i = 1; i < argc; i++) 
    {
        if (strcmp(argv[i], "-t") == 0) 
        {
            num_threads = atoi(argv[i + 1]);
        }
    }

    //If number of threads isnt specified, 1 is the default
    if (!num_threads)
    {
        num_threads = 1;
    }

    //Declared an array of threads with size of the number of threads 
    pthread_t threads[num_threads];

    //Declared an array of Structs with size of the number of threads 
    ThreadData thread_data[num_threads];

    //Calculates the the range of stars that each thread should process 
    uint32_t thread_range = NUM_STARS / num_threads;

    //Calculates the remainder of stars that should be divided evenly among the threads
    uint32_t thread_remainder = NUM_STARS % num_threads;

    //Starts the clock
    start_t = clock();

    //Sets the start and end index in the struct and creates threads 
    for (int i = 0; i < num_threads; i++) 
    {
        thread_data[i].start_idx = i * thread_range;
        thread_data[i].end_idx = (i + 1) * thread_range;

        if (i == num_threads - 1) 
        {
            thread_data[i].end_idx += thread_remainder;
        }

        int result = pthread_create(&threads[i], NULL, func, (void *)&thread_data[i]);

        if (result) 
        {
            printf("Error creating thread %d.\n", i);

            exit(1);
        }
    }

    //Waits for individual threads to finish
    for (int i = 0; i < num_threads; i++) 
    {
        pthread_join(threads[i], NULL);
    }

    //Stops the clock
    end_t = clock();

    //Calculates total time in seconds 
    total_t = (double)(end_t - start_t) / CLOCKS_PER_SEC;

    //Calculates the mean for distance 
    double mean = distance / ((NUM_STARS * (NUM_STARS - 1)) / 2);

    printf("Average distance found is %lf\n", mean);
    printf("Minimum distance found is %lf\n", min);
    printf("Maximum distance found is %lf\n", max);
    printf("Total time elapsed: %f\n", total_t);

    return 0;
}