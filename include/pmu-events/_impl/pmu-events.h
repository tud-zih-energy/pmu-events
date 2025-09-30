#pragma once

#include <pmu-events/pmu-events.h>

#include <stddef.h>
#include <stdint.h>

#include <linux/perf_event.h>

/*
 * Stores assignments of the form key=value
 */
struct assignment
{
    char* key;
    uint64_t value;
};

/*
 * Stores a list of assignments, e.g.:
 * len=3
 * key1=value1,key2=value2,key3=value3
 */
struct assignment_list
{
    size_t len;
    struct assignment* assignments;
};

/*
 * One of the three struct perf_event_attr
 * members that can be set by event config
 */
enum ATTR_VAR
{
    CONFIG,
    CONFIG1,
    CONFIG2
};

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
 * A combination of a perf_event_attr member with the
 * range list it applies to.
 */
struct config_def
{
    enum ATTR_VAR var;
    struct range_list range;
};

int parse_range(const char* term, struct range* range);

int parse_range_list(const char* term, struct range_list* list);
void free_range_list(struct range_list* list);

int parse_config_def(const char* term, struct config_def* list);
void free_config_def(struct config_def* list);

int parse_assignment(char* term, struct assignment* assignment);
void free_assignment(struct assignment* asn);

int parse_assignment_list(const char* str, struct assignment_list* list);
void free_assignment_list(struct assignment_list* list);

int apply_range_list_to_val(unsigned long long* config, uint64_t to_apply, struct range_list* list);
int apply_config_def_to_attr(struct perf_event_attr* attr, uint64_t val, struct config_def* def);

char* get_format_file_content(char* fmt_file, struct perf_cpu cpu);
int read_perf_type(struct perf_cpu cpu);
