#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct perf_cpu
{
    int16_t cpu;
};

enum aggr_mode_class
{
    PerChip = 1,
    PerCore
};

/**
 * enum metric_event_groups - How events within a pmu_metric should be grouped.
 */
enum metric_event_groups
{
    /**
     * @MetricGroupEvents: Default, group events within the metric.
     */
    MetricGroupEvents = 0,
    /**
     * @MetricNoGroupEvents: Don't group events for the metric.
     */
    MetricNoGroupEvents = 1,
    /**
     * @MetricNoGroupEventsNmi:
     * Don't group events for the metric if the NMI watchdog is enabled.
     */
    MetricNoGroupEventsNmi = 2,
    /**
     * @MetricNoGroupEventsSmt:
     * Don't group events for the metric if SMT is enabled.
     */
    MetricNoGroupEventsSmt = 3,
    /**
     * @MetricNoGroupEventsThresholdAndNmi:
     * Don't group events for the metric thresholds and if the NMI watchdog
     * is enabled.
     */
    MetricNoGroupEventsThresholdAndNmi = 4,
};

/*
 * Describe each PMU event. Each CPU has a table of PMU events.
 */
struct pmu_event
{
    const char* name;
    const char* compat;
    const char* event;
    const char* desc;
    const char* topic;
    const char* long_desc;
    const char* pmu;
    const char* unit;
    const char* retirement_latency_mean;
    const char* retirement_latency_min;
    const char* retirement_latency_max;
    bool perpkg;
    bool deprecated;
};

struct pmu_metric
{
    const char* pmu;
    const char* metric_name;
    const char* metric_group;
    const char* metric_expr;
    const char* metric_threshold;
    const char* unit;
    const char* compat;
    const char* desc;
    const char* long_desc;
    const char* metricgroup_no_group;
    const char* default_metricgroup_name;
    enum aggr_mode_class aggr_mode;
    enum metric_event_groups event_grouping;
};

struct compact_pmu_event
{
    int offset;
};

struct pmu_table_entry
{
    const struct compact_pmu_event* entries;
    uint32_t num_entries;
    struct compact_pmu_event pmu_name;
};

/* Struct used to make the PMU event table implementation opaque to callers. */
struct pmu_events_table
{
    const struct pmu_table_entry* pmus;
    uint32_t num_pmus;
};

/* Struct used to make the PMU metric table implementation opaque to callers. */
struct pmu_metrics_table
{
    const struct pmu_table_entry* pmus;
    uint32_t num_pmus;
};

/*
 * Map a CPU to its table of PMU events. The CPU is identified by the
 * cpuid field, which is an arch-specific identifier for the CPU.
 * The identifier specified in tools/perf/pmu-events/arch/xxx/mapfile
 * must match the get_cpuid_str() in tools/perf/arch/xxx/util/header.c)
 *
 * The  cpuid can contain any character other than the comma.
 */
struct pmu_events_map
{
    const char* arch;
    const char* cpuid;
    struct pmu_events_table event_table;
    struct pmu_metrics_table metric_table;
};

/*
 * Returns the list of all events for the given cpu, or NULL on
 * failure
 */
const struct pmu_events_map* map_for_cpu(struct perf_cpu cpu);

/*
 * pmu_table_entries store the pmu events compressed.
 *
 * This function decompresses the event using the offset as its address
 */
void decompress_event(int offset, struct pmu_event* pe);

/*
 * Non-Linux functions
 */

/*
 * A range, e.g. "bit 5", "bit 4-6",...
 */
struct range
{
    uint64_t start;
    uint64_t end;
};

/*
 * A list of ranges, e.g.:
 * len = 2
 * bit 4, bit 7-14
 */
struct range_list
{
    size_t len;
    struct range* ranges;
};

/*
 * An instance of a pmu class, such as uncore_cbox_0
 */
struct pmu_instance
{
    struct range_list cpus;
    char* name;
    const struct compact_pmu_event* entries;
    uint32_t num_entries;
};

/*
 * A class of pmu devices, such as uncore_cbox
 */
struct pmu_class
{
    const char* name;
    struct pmu_instance* instances;
    int num_instances;
};

// The list of all PMUs
struct pmus
{
    size_t num_classes;
    struct pmu_class* classes;
};
