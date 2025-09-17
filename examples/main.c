#include <pmu-events/pmu-events.h>
#include <stdio.h>

void print_pmu_event(struct pmu_event *ev)
{
    printf("name: %s\n", ev->name);
    printf("compat: %s\n", ev->compat);
    printf("event: %s\n", ev->event);
    printf("desc: %s\n", ev->desc);
    printf("topic: %s\n", ev->topic);
    printf("long_desc: %s\n", ev->long_desc);
    //printf("pmu: %s\n", ev->pmu);
    printf("unit: %s\n", ev->unit);
    printf("retirement_latency_mean: %s\n", ev->retirement_latency_mean);
    printf("retirement_latency_min: %s\n", ev->retirement_latency_min);
    printf("retirement_latency_max: %s\n", ev->retirement_latency_max);
        
    if(ev->perpkg)
    {
        printf("Is perpkg\n");
    }

    if(ev->deprecated)
    {
        printf("Is deprecated\n");
    }

}
int main(void)
{
    struct perf_cpu cpu;
    cpu.cpu = 0;

    const struct pmu_events_map *map = map_for_cpu(cpu);
    
    printf("ARCH: %s\n", map->arch);
    printf("CPUID: %s\n", map->cpuid);
    printf("\n\n");

    for(int i = 0;i < map->event_table.num_pmus; i++)
    {
        struct pmu_table_entry entry = map->event_table.pmus[i];
        struct pmu_event pmu;
        decompress_event(entry.pmu_name.offset, &pmu);
    
        print_pmu_event(&pmu);
        printf("\n\n");

        for(int x = 0; x < entry.num_entries; x++){
            struct pmu_event ev;
            decompress_event(entry.entries[x].offset, &ev);
            
            print_pmu_event(&ev);
            printf("\n\n");
        }
    }
}
