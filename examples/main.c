#include <pmu-events/pmu-events.h>

#include <errno.h>
#include <linux/perf_event.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

void print_pmu_event(struct pmu_event *ev)
{
    printf("  name: %s\n", ev->name);
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

    if(ev->deprecated)
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
    struct perf_cpu cpu;
    cpu.cpu = 0;

    const struct pmu_events_map* map = map_for_cpu(cpu);
    int i;
    printf("ARCH: %s\n", map->arch);
    printf("CPUID: %s\n", map->cpuid);
    printf("\n\n");

    for (int i = 0; i < map->event_table.num_pmus; i++)
    {
        struct pmu_table_entry entry = map->event_table.pmus[i];

        printf("==============\n");
        printf("PMU: %s\n", get_pmu_name(entry));
        printf("--------------\n");

        for (int x = 0; x < entry.num_entries; x++)
        {
            struct pmu_event ev;
            decompress_event(entry.entries[x].offset, &ev);

            print_pmu_event(&ev);
            printf("\n\n");
        }
    }
}

bool stop = false;

void sighandler(int signal [[maybe_unused]])
{
    stop = true;
}

/*
 * Looks up the event "ev" on cpu 0
 * and then reads it using perf every second
 */
void read_event(char* ev)
{
    struct perf_cpu cpu;
    cpu.cpu = 0;

    struct pmu_event pmu_ev;
    if (get_event_by_name(map_for_cpu(cpu), ev, &pmu_ev) == -1)
    {
        fprintf(stderr, "No event named: %s\n!", ev);
        return;
    }

    struct perf_event_attr attr;
    attr.size = sizeof(attr);
    memset(&attr, 0, sizeof(attr));

    if (gen_attr_for_event(&pmu_ev, cpu, &attr) == -1)
    {
        fprintf(stderr, "Can not generate perf_event_attr for: %s!\n", ev);
        return;
    }

    int perf_fd = syscall(SYS_perf_event_open, &attr, -1, 0, -1, 0);

    if (perf_fd == -1)
    {
        fprintf(stderr, "Could not open event: %s!\n", strerror(errno));
        return;
    }

    fprintf(stderr, "Reading %s every second until Ctrl+C\n", ev);

    signal(SIGTERM, sighandler);
    while (!stop)
    {
        long long count;
        if (read(perf_fd, &count, sizeof(count)) != sizeof(count))
        {
            fprintf(stderr, "Could not read event %s: %s!\n", ev, strerror(errno));
        }

        printf("%s: %llu\n", ev, count);
        sleep(1);
    }
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
