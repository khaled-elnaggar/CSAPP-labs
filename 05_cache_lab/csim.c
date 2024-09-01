#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "cachelab.h"

int verbose = 0;
unsigned int set_index_bits = 0, lines_per_set = 0, block_offset_bits = 0;
unsigned long sets_count = 0;
int hits = 0, misses = 0, evictions = 0, time = 0;
FILE *trace_file;

typedef struct CacheLine
{
    long tags;
    long last_used;
    char valid;
} CacheLine;

typedef struct CacheSet
{
    CacheLine *lines;
} CacheSet;

CacheSet *cache_sets;

void parse_cache_specs(int argc, char **argv)
{

    if (argc < 9)
    {
        printf("To use run as \n./csim [-v] -s <s> -E <E> -b <b> -t <tracefile>\n");
        exit(EXIT_FAILURE);
    }

    int i = 1;
    while (i < argc)
    {
        if (strcmp("-v", argv[i]) == 0)
        {
            verbose = 1;
            i++;
            continue;
        }

        if (strcmp("-s", argv[i]) == 0)
        {
            set_index_bits = atoi(argv[i + 1]);
            sets_count = 1 << set_index_bits;
            i += 2;
            continue;
        }

        if (strcmp("-E", argv[i]) == 0)
        {
            lines_per_set = atoi(argv[i + 1]);
            i += 2;
            continue;
        }

        if (strcmp("-b", argv[i]) == 0)
        {
            block_offset_bits = atoi(argv[i + 1]);
            i += 2;
            continue;
        }

        if (strcmp("-t", argv[i]) == 0)
        {
            trace_file = fopen(argv[i + 1], "r");
            i += 2;
            continue;
        }

        printf("Unknown tag %s\n", argv[i]);
        exit(EXIT_FAILURE);
    }
}

void initialize_cache_set()
{
    cache_sets = malloc(sizeof(CacheSet) * sets_count);
    for (int i = 0; i < sets_count; i++)
    {
        cache_sets[i].lines = malloc(sizeof(CacheLine) * lines_per_set);
        for (int j = 0; j < lines_per_set; j++)
        {
            cache_sets[i].lines[j].valid = 0;
            cache_sets[i].lines[j].tags = 0;
        }
    }
}

void parse_trace_line(char *buffer, unsigned char *operation, unsigned long *memory_address)
{
    unsigned char size;
    sscanf(buffer, " %c %lx,%c\n", operation, memory_address, &size);
    // printf("op %c memory_address %lx\n", *operation, *memory_address);
}

void extract_cache_targets(unsigned long memory_address, unsigned long *target_set, unsigned long *target_tag)
{
    *target_set = (memory_address >> block_offset_bits) & ((1 << set_index_bits) - 1);
    *target_tag = (memory_address >> (block_offset_bits + set_index_bits));
}

void print_verbose_logs(int is_hit, char *buffer, int found_empty, int operation)
{
    if (verbose)
    {
        if (is_hit)
        {
            printf("%s hit", buffer);
        }
        else if (found_empty)
        {
            printf("%s miss", buffer);
        }
        else
        {
            printf("%s miss eviction", buffer);
        }

        if (operation == 'M')
        {
            printf(" hit\n");
        }
        else
        {
            printf("\n");
        }
    }
}

int checkForHit(CacheSet set, int target_set, int target_tag)
{
    for (int i = 0; i < lines_per_set; i++)
    {
        // if hit
        if (set.lines[i].valid && set.lines[i].tags == target_tag)
        {
            // then update time used
            set.lines[i].last_used = time;
            hits++;
            return 1;
        }
    }
    return 0;
}

int handle_miss(CacheSet set, int target_tag)
{
    int found_empty = 0;
    misses++;
    // check for a replacement
    int least_recently_used = 0;
    for (int i = 0; i < lines_per_set; i++)
    {
        if (set.lines[i].last_used < set.lines[least_recently_used].last_used)
        {
            least_recently_used = i;
        }

        // check for an empty line
        if (set.lines[i].valid == 0)
        {
            set.lines[i].valid = 1;
            set.lines[i].tags = target_tag;
            set.lines[i].last_used = time;
            found_empty = 1;
            break;
        }
    }

    // otherwise evict the least_recently_used line
    if (!found_empty)
    {
        evictions++;
        set.lines[least_recently_used].valid = 1;
        set.lines[least_recently_used].tags = target_tag;
        set.lines[least_recently_used].last_used = time;
    }
    return found_empty;
}

int main(int argc, char **argv)
{

    parse_cache_specs(argc, argv);
    initialize_cache_set();
    // printf("s=%d S=%ld, b=%d, E=%d\n", set_index_bits, sets_count, block_offset_bits, lines_per_set);

    char buffer[51];
    while (fgets(buffer, 50, trace_file))
    {
        if (buffer[0] == 'I' || buffer[0] == '\n')
        {
            continue;
        }
        size_t ln = strlen(buffer) - 1;
        if (buffer[ln] == '\n')
        {
            buffer[ln] = '\0';
        }

        unsigned char operation;
        unsigned long memory_address;
        parse_trace_line(buffer, &operation, &memory_address);

        // from address extract ithSet and ithTag using left and right shifts
        unsigned long target_set, target_tag;
        extract_cache_targets(memory_address, &target_set, &target_tag);

        CacheSet set = cache_sets[target_set];
        // check set for a hit:
        int is_hit = checkForHit(set, target_set, target_tag);
        // otherwise it is a miss
        int found_empty = 0;
        if (!is_hit)
        {
            found_empty = handle_miss(set, target_tag);
        }

        if (operation == 'M')
        {
            hits++;
        }
        print_verbose_logs(is_hit, buffer, found_empty, operation);

        time++;
    }

    printSummary(hits, misses, evictions);
    return 0;
}
