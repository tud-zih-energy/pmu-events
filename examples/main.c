#include <assert.h>
#include <pmu-events/pmu-events.h>

#include <errno.h>
#include <linux/perf_event.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

void print_pmu_event(struct pmu_event* ev)
{
    printf("  NAME: %s\n", ev->name);
    printf("    compat: %s\n", ev->compat);
    printf("    event: %s\n", ev->event);
    printf("    desc: %s\n", ev->desc);
    printf("    topic: %s\n", ev->topic);
    printf("    long_desc: %s\n", ev->long_desc);
    printf("    unit: %s\n", ev->unit);
    printf("    retirement_latency_mean: %s\n", ev->retirement_latency_mean);
    printf("    retirement_latency_min: %s\n", ev->retirement_latency_min);
    printf("    retirement_latency_max: %s\n", ev->retirement_latency_max);

    if (ev->perpkg)
    {
        printf("    Is perpkg\n");
    }

    if (ev->deprecated)
    {
        printf("    Is deprecated\n");
    }
}

void print_help()
{
    fprintf(stderr, "./pmu-events-example COMMAND [ARGS]\n");
    fprintf(stderr, "./pmu-events-example list\n");
    fprintf(stderr, "./pmu-events-example read EVENT\n");
}

/*
 * Lists all available events for CPU 0
 */
void list_events()
{

    struct pmus pmus;
    get_pmus(&pmus);

    for (int cur_pmu_class_id = 0; cur_pmu_class_id < pmus.num_classes; cur_pmu_class_id++)
    {
        const struct pmu_class* pmu_class = &pmus.classes[cur_pmu_class_id];
        printf("CLASS: %s\n", pmu_class->name);

        printf("INSTANCES:\n");
        for (int cur_pmu_instance_id = 0; cur_pmu_instance_id < pmu_class->num_instances;
             cur_pmu_instance_id++)

        {
            const struct pmu_instance* pmu_instance = &pmu_class->instances[cur_pmu_instance_id];
            printf("\tINSTANCE: \"%s\" ", pmu_instance->name);

            printf("CPUS: ");
            for (int cur_range_id = 0; cur_range_id < pmu_instance->cpus.len; cur_range_id++)
            {
                struct range* cur_range = &pmu_instance->cpus.ranges[cur_range_id];
                if (cur_range->start == cur_range->end)
                {
                    printf("%lu", cur_range->start);
                }
                else
                {

                    printf("%lu-%lu", cur_range->start, cur_range->end);
                }

                if (cur_range_id + 1 != pmu_instance->cpus.len)
                {
                    printf(", ");
                }
            }
            printf("\n");
        }
        printf("EVENTS:\n");

        for (int x = 0; x < pmu_class->instances[0].num_entries; x++)
        {
            struct pmu_event ev;
            decompress_event(pmu_class->instances[0].entries[x].offset, &ev);

            print_pmu_event(&ev);
            printf("\n\n");
        }
    }

    free_pmus(&pmus);
}

bool stop = false;

void signal_handler(int signal)
{
    stop = true;
}

struct whole_ev
{
    struct pmu_instance* instance;
    struct pmu_event ev;
    struct perf_event_attr attr;
    int fd;
};

/*
 * Looks up the event "ev" on cpu 0
 * and then reads it using perf every second
 */
void read_event(char* ev)
{
    struct pmus pmus;

    get_pmus(&pmus);

    struct whole_ev* evs = NULL;
    int num_evs = 0;

    for (size_t cur_class_id = 0; cur_class_id < pmus.num_classes; cur_class_id++)
    {
        struct pmu_class* pmu_class = &pmus.classes[cur_class_id];
        for (size_t cur_instance_id = 0; cur_instance_id < pmu_class->num_instances;
             cur_instance_id++)
        {
            struct pmu_instance* cur_instance = &pmu_class->instances[cur_instance_id];

            struct pmu_event pmu_ev;
            if (get_event_by_name(cur_instance, ev, &pmu_ev) == 0)
            {
                num_evs++;
                evs = realloc(evs, sizeof(struct whole_ev) * num_evs);
                evs[num_evs - 1].ev = pmu_ev;
                evs[num_evs - 1].instance = cur_instance;
            }
        }
    }

    if (num_evs == 0)
    {
        free_pmus(&pmus);
        fprintf(stderr, "No event matches: %s\n!", ev);
        return;
    }

    for (int ev_id = 0; ev_id < num_evs; ev_id++)
    {
        evs[ev_id].attr.size = sizeof(struct perf_event_attr);
        memset(&evs[ev_id].attr.size, 0, sizeof(struct perf_event_attr));

        if (gen_attr_for_event(evs[ev_id].instance, &evs[ev_id].ev, &evs[ev_id].attr) == -1)
        {
            free_pmus(&pmus);
            fprintf(stderr, "Can not generate perf_event_attr for: %s!\n", evs[ev_id].ev.name);
            free(evs);
            return;
        }
    }

    for (int ev_id = 0; ev_id < num_evs; ev_id++)
    {
        evs[ev_id].fd = syscall(SYS_perf_event_open, &evs[ev_id].attr, -1,
                                evs[ev_id].instance->cpus.ranges[0].start, -1, 0);

        if (evs[ev_id].fd == -1)
        {
            fprintf(stderr, "Could not open event: %s!\n", strerror(errno));
            return;
        }
    }

    fprintf(stderr, "Reading: \n");

    for (int ev_id = 0; ev_id < num_evs; ev_id++)
    {
        fprintf(stderr, "\t%s::%s (CPU: %lu)\n", evs[ev_id].instance->name, evs[ev_id].ev.name,
                evs[ev_id].instance->cpus.ranges[0].start);
    }
    fprintf(stderr, "Every second until Ctrl+C\n");

    signal(SIGTERM, signal_handler);
    while (!stop)
    {
        long long count;
        for (int ev_id = 0; ev_id < num_evs; ev_id++)
        {
            if (read(evs[ev_id].fd, &count, sizeof(count)) != sizeof(count))
            {
                fprintf(stderr, "Could not read event %s: %s!\n", ev, strerror(errno));
            }

            printf("%s::%s: %llu\n", evs[ev_id].instance->name, evs[ev_id].ev.name, count);
        }
        sleep(1);
    }

    free_pmus(&pmus);
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "%s needs arguments: \n", argv[0]);
        print_help();
        return -1;
    }

    if (strcmp(argv[1], "list") == 0)
    {
        list_events();
        return 0;
    }
    else if (strcmp(argv[1], "read") == 0)
    {
        if (argc != 3)
        {
            fprintf(stderr, "\"read\" command needs exactly two arguments!\n");
            return -1;
        }
        read_event(argv[2]);
        return 0;
    }

    fprintf(stderr, "Unknown command: %s\n", argv[1]);
    return -1;
}
