#include <pmu-events/pmu-events.h>

#include <pmu-events/_impl/pmu-events.h>

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * performs: result = base + "/" + filename
 *
 * On success, returns the concatenated path, on failure returns NULL
 * The caller is responsible for free()-ing the string
 */
static char* concat_path(const char* base, const char* filename)
{
    size_t path_len = strlen(base) + strlen(filename) + 1;
    char* path = malloc(path_len + 1);
    if (snprintf(path, path_len + 1, "%s/%s", base, filename) != path_len)
    {
        free(path);
        return NULL;
    }
    return path;
}

/*
 * Checks if "num" is in any of the ranges in range_list
 */
static bool in_range_list(uint64_t num, struct range_list* range_list)
{
    int i = 0;
    for (; i < range_list->len; i++)
    {
        if (num >= range_list->ranges[i].start && num <= range_list->ranges[i].end)
        {
            return true;
        }
    }
    return false;
}

/*
 * Returns the content of the file with the name "path"
 *
 * Returns the content on success, NULL otherwise
 *
 * The caller is responsible for free()-ing the result.
 */
static char* get_file_content(const char* path)
{
    int fd = open(path, O_RDONLY);
    if (fd == -1)
    {
        return NULL;
    }

    off_t end = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);
    if (end == -1)
    {
        close(fd);
        return NULL;
    }

    char* content = malloc(end + 1);
    if (read(fd, content, end) == -1)
    {
        free(content);
        close(fd);
        return NULL;
    }

    close(fd);
    content[end] = '\0';

    char* newline = strchr(content, '\n');
    if (newline != NULL)
    {
        *newline = '\0';
    }
    return content;
}

/*
 * Parses a range term of the form "5" (5 exactly)
 * or "4-7" (4 to 7, inclusively)
 *
 * Args:
 *  - term: the term to parse
 *  - range, the range struct in which to store the result
 * Returns:
 *  - 0 on success, -1 on error
 */
int parse_range(const char* term, struct range* range)
{
    /* No empty strings please */
    if (*term == '\0')
    {
        return -1;
    }

    char* minus_sign = strchr(term, '-');

    /*single bit, i.e. 42*/
    if (minus_sign == NULL)
    {
        char* endptr;
        uint64_t val = strtoull(term, &endptr, 10);
        if (*endptr != '\0')
        {
            return -1;
        }

        range->start = val;
        range->end = val;
    }
    else
    {
        char* endptr;
        uint64_t start = strtoull(term, &endptr, 10);
        if (*endptr != '-')
        {
            return -1;
        }

        if (*(minus_sign + 1) == '\0')
        {
            return -1;
        }

        uint64_t end = strtoull((minus_sign + 1), &endptr, 10);
        if (*endptr != '\0')
        {
            return -1;
        }

        range->start = start;
        range->end = end;
    }
    return 0;
}

/*
 * Parses a perf_event_attr config member definition.
 *
 * A config member definition starts with the perf_event_attr member and
 * then a comma separated list of ranges, i.e.
 *      "config1:1,45-62"
 * Args:
 *  - term: range list term to parse
 *  - list: struct config_def into which the result is stored
 *
 * Returns:
 *  - 0 on success, -1 on error, possible error cases are:
 *      - the start is neither "config:", "config1:" or "config2"
 *      - there are stray commas at the end of input.
 *      - the parts between the commas are not parseable by parse_range
 *
 *  - The user is responsible for freeing the resulting config_def
 */

int parse_config_def(const char* term, struct config_def* def)
{
    const char* start_term;
    if (strncmp(term, "config:", strlen("config:")) == 0)
    {
        def->var = CONFIG;
        start_term = term + strlen("config:");
    }
    else if (strncmp(term, "config1:", strlen("config1:")) == 0)
    {
        def->var = CONFIG1;
        start_term = term + strlen("config1:");
    }
    else if (strncmp(term, "config2:", strlen("config2:")) == 0)
    {
        def->var = CONFIG2;
        start_term = term + strlen("config2:");
    }
    else
    {
        return -1;
    }

    if (parse_range_list(start_term, &def->range) == -1)
    {
        return -1;
    }

    return 0;
}

/*
 * Parses a range list, a comma separated list of ranges (as defined by parse_range)
 * e.g.: "4,15-43,12"
 *
 * Returns 0 on success, -1 on error.
 *
 * The caller is responsible for freeing list with free_range_list()
 */
int parse_range_list(const char* term, struct range_list* list)
{
    char* new_term = strdup(term);
    char* next_comma = new_term;
    char* cur_range = new_term;

    list->len = 0;
    list->ranges = NULL;
    do
    {
        list->len++;
        list->ranges = realloc(list->ranges, sizeof(struct range) * list->len);
        if (list->ranges == NULL)
        {
            free(new_term);
            return -1;
        }

        next_comma = strchr(cur_range, ',');
        if (next_comma != NULL)
        {
            *next_comma = '\0';
        }

        if (parse_range(cur_range, &list->ranges[list->len - 1]) == -1)
        {
            free(list->ranges);
            free(new_term);
            return -1;
        }

        cur_range = next_comma + 1;
    } while (next_comma != NULL);

    free(new_term);
    return 0;
}

/*
 * Frees range_list structs returned by succesful calls to parse_range_list
 */
void free_range_list(struct range_list* list)
{
    free(list->ranges);
}

/*
 * Frees config_def structs returned by succesful calls to parse_config_def
 */
void free_config_def(struct config_def* def)
{
    free_range_list(&def->range);
}

/*
 * Parse assignments of the form "foo=42"
 *
 * Args:
 *  - term: the term to parse
 *  - assignment: on succesful calls to parse_assignment, will contain the parse assignment
 * Returns:
 *  - 0 on success, -1 on error. Errors if "term" does not contain a term of the
 *    form "[key]=[value]"
 *  - The user of parse_assignment is responsible for freeing the resulting "assignment"
 *    on succesful calls to parse_assignment with free_assignment
 */
int parse_assignment(char* term, struct assignment* assignment)
{
    const char* equal_sign = strchr(term, '=');

    if (equal_sign == NULL)
    {
        return -1;
    }

    if (equal_sign <= term)
    {
        return -1;
    }

    if (*(equal_sign + 1) == '\0')
    {
        return -1;
    }

    uint64_t value = 0;

    /* Some of the assignments we have encountered can look like:
     * foo=None
     *
     * Interpret them as zero
     */
    if (strncmp(equal_sign + 1, "None", sizeof("None")) != 0)
    {
        char* endptr;
        value = strtoull(equal_sign + 1, &endptr, 16);
        if (*endptr != '\0')
        {
            return -1;
        }
    }
    assignment->value = value;
    assignment->key = malloc(equal_sign - term + 1);
    assignment->key[equal_sign - term] = '\0';
    strncpy(assignment->key, term, equal_sign - term);

    return 0;
}

/*
 * Frees the result of parse_assignment()
 */
void free_assignment(struct assignment* asn)
{
    free(asn->key);
}

/*
 * Parses a list of comma separated assignments i.e.:
 *  foo=42,bar=13
 *
 * Args:
 *  - str: The string containing the assignment list
 *  - list: The assignment_list into which the result is pmu_table_entry
 * Returns:
 *  - 0 on success, -1 on failure. Fails if str does not contain a comma-separated
 *    list of assignments
 *  - On succesful return, the user is required to free the assignment_list with
 *    free_assignment_list()
 */
int parse_assignment_list(const char* str, struct assignment_list* list)
{
    char* new_str = strdup(str);
    char* cur_range = new_str;
    char* next_comma = new_str;

    list->len = 0;
    list->assignments = NULL;

    while (next_comma != NULL)
    {
        next_comma = strchr(cur_range, ',');
        if (next_comma != NULL)
        {
            *next_comma = '\0';
        }

        list->len++;
        list->assignments = realloc(list->assignments, list->len * sizeof(struct assignment));

        if (parse_assignment(cur_range, &list->assignments[list->len - 1]) == -1)
        {
            list->len = 0;
            free(list->assignments);
            free(new_str);
            return -1;
        }

        cur_range = next_comma + 1;
    }

    free(new_str);
    return 0;
}

/*
 * Frees the assignment list returned by parse_assignment_list on succesful calls
 */
void free_assignment_list(struct assignment_list* list)
{
    size_t i = 0;
    for (; i < list->len; i++)
    {
        free_assignment(&list->assignments[i]);
    }
    free(list->assignments);
}

/*
 * Applies "to_apply" to the value stored in "config" using the range_list "list".
 *
 * This is done by copying the bits of "to_apply" into the positions given by the
 * range_list "list", starting with the least significant bit.
 *
 * If the range list is "config1:0-7,32-39", then:
 *
 * to_apply[bit0-7] is moved to config[bit0-7]
 * to_apply[bit8-15] is moved to config[bit32-39]
 */
int apply_range_list_to_val(unsigned long long* config, uint64_t to_apply, struct range_list* list)
{
    int range_nr = 0;
    for (; range_nr < list->len; range_nr++)
    {
        struct range range = list->ranges[range_nr];
        size_t range_len = range.end - range.start + 1;

        uint64_t mask = (UINT64_MAX >> (64 - range_len)) << range.start;

        uint64_t cur_apply = to_apply << range.start;
        cur_apply = cur_apply & mask;

        *config = *config & (~mask);
        *config = *config | cur_apply;
        to_apply = to_apply >> range_len;
    }

    return 0;
}

/*
 * Applies the value "val" to the correct member of perf_event_attr by using
 * the config_def->range range_list and apply_range_list_to_val()
 */
int apply_config_def_to_attr(struct perf_event_attr* attr, uint64_t val, struct config_def* def)
{
    switch (def->var)
    {
    case CONFIG:
        apply_range_list_to_val(&attr->config, val, &def->range);
        return 0;
    case CONFIG1:
        apply_range_list_to_val(&attr->config1, val, &def->range);
        return 0;
    case CONFIG2:
        apply_range_list_to_val(&attr->config2, val, &def->range);
        return 0;
    }
}

/*
 * Returns the syfs PMU path for the specific cpu
 *
 * Returns NULL on error
 */
char* get_pmu_path_for_cpu(struct perf_cpu cpu)
{
    /*
     * First case (mostly x86 CPUs): there is a "cpu" PMU.
     *
     * If that PMU exists, then it is automatically responsible for all
     * cpu cores.
     */
    if (access("/sys/bus/event_source/devices/cpu/", F_OK) == F_OK)
    {
        return strdup("/sys/bus/event_source/devices/cpu/");
    }

    /*
     * Second case (mostly observed on ARM and on Intel's P/E-Core systems):
     *
     * On architectures without a "cpu" PMU, the PMUs that are responsible
     * for the CPU cores contain a "cpus" file.
     *
     * This "cpus" file contains a range list of the CPUs it is responsible for.
     *
     * For example (ARM Neoverse N1 architecture):
     * There is one PMU folder in /sys/bus/event_source/devices/ with a "cpus" file.
     *
     *      /sys/bus/event_source/devices/armv8_pmuv3_0/cpus
     *
     * That contains the string "0-79". That means it is responsible for the cores
     * 0 through 79 (which in fact are all the cores on that system.
     *
     * We simply iterate here through all PMU folders in /sys/bus/event_source/devices,
     * checking if they contain a "cpus" file and then checking if the
     * given cpu is in the range of the PMU.
     */
    const char* pmu_dir = "/sys/bus/event_source/devices";
    DIR* pmu_devices = opendir(pmu_dir);
    if (pmu_devices == NULL)
    {
        return NULL;
    }
    struct dirent* ent;
    while ((ent = readdir(pmu_devices)) != NULL)
    {
        if ((strcmp(ent->d_name, ".") == 0) || (strcmp(ent->d_name, "..") == 0))
        {
            continue;
        }
        char* pmu_path = concat_path(pmu_dir, ent->d_name);
        if (pmu_path == NULL)
        {
            closedir(pmu_devices);
            return NULL;
        }
        char* cpus_path = concat_path(pmu_path, "cpus");
        if (cpus_path == NULL)
        {
            closedir(pmu_devices);
            free(pmu_path);
            return NULL;
        }

        char* content = get_file_content(cpus_path);
        free(cpus_path);
        if (content == NULL)
        {
            free(pmu_path);
            continue;
        }

        struct range_list range_list;
        if (parse_range_list(content, &range_list) == -1)
        {
            free(content);
            closedir(pmu_devices);
            free(pmu_path);
            return NULL;
        }
        free(content);

        if (in_range_list(cpu.cpu, &range_list))
        {
            closedir(pmu_devices);
            free_range_list(&range_list);
            return pmu_path;
        }
    }
    closedir(pmu_devices);
    return NULL;
}

/*
 * Returns the content of the [pmu for cpu]/format format file
 * "fmt_file". This is usually a range list string, parseable
 * by parse_range_list() (e.g. "config1:0,23-42")
 *
 * Returns NULL on error.
 */
char* get_format_file_content(char* fmt_file, struct perf_cpu cpu)
{
    char* pmu_path = get_pmu_path_for_cpu(cpu);
    if (pmu_path == NULL)
    {
        return NULL;
    }

    char* format_path = concat_path(pmu_path, "format/");
    if (format_path == NULL)
    {
        free(pmu_path);
        return NULL;
    }

    free(pmu_path);
    char* path = concat_path(format_path, fmt_file);
    free(format_path);
    if (path == NULL)
    {
        return NULL;
    }

    char* content = get_file_content(path);

    free(path);
    return content;
}

/*
 * Reads the perf_event_attr.type from [pmu path for cpu]/type
 *
 * Returns the perf_event_attr.type or -1 on failure.
 */
int read_perf_type(struct perf_cpu cpu)
{
    char* pmu_path = get_pmu_path_for_cpu(cpu);

    if (pmu_path == NULL)
    {
        return -1;
    }

    char* type_path = concat_path(pmu_path, "type");
    free(pmu_path);
    if (type_path == NULL)
    {
        free(pmu_path);
        return -1;
    }
    int fd = open(type_path, O_RDONLY);
    if (fd == -1)
    {
        return -1;
    }

    free(type_path);
    off_t end = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);
    if (end == -1)
    {
        close(fd);
        return -1;
    }

    char* content = malloc(end + 1);
    if (read(fd, content, end) == -1)
    {
        free(content);
        close(fd);
        return -1;
    }
    content[end] = '\0';

    char* newline = strchr(content, '\n');
    if (newline != NULL)
    {
        *newline = '\0';
    }

    char* endptr;
    uint64_t res = strtoull(content, &endptr, 10);

    if (*endptr != '\0')
    {
        return -1;
    }
    free(content);
    close(fd);
    return res;
}

/*
 * For the event assignment string "event" of the form "event=0x40,umask=1",
 * set the type, config, config1 and config2 correctly in perf_event_attr
 * for the given cpu.
 *
 * For every assignment in the "event" string, the key specifies a file
 * in [pmu path for cpu]/format that describes how the value
 * of the assignment is put into the bits of a perf_event_attr member.
 *
 * On a recent AMD cpu, for example, /sys/bus/event_source/devices/cpu/format/event
 * contains: "config:0-7,32-35"
 *
 * This means, that the lowest 8 bits of "event=[value]" are put into
 * attr->config[bits0-7], with the next 4 bits being put into attr->config[bits32-35]
 */
int gen_attr_for_event(const struct pmu_event* ev, struct perf_cpu cpu,
                       struct perf_event_attr* attr)
{
    if ((attr->type = read_perf_type(cpu)) == -1)
    {
        return -1;
    }
    struct assignment_list asn_list;
    if (parse_assignment_list(ev->event, &asn_list) == -1)
    {
        return -1;
    }

    int asn_nr = 0;
    for (; asn_nr < asn_list.len; asn_nr++)
    {
        struct assignment asn = asn_list.assignments[asn_nr];

        char* config_def_str = get_format_file_content(asn.key, cpu);
        if (config_def_str == NULL)
        {
            free_assignment_list(&asn_list);
            return -1;
        }

        struct config_def conf_def;
        if (parse_config_def(config_def_str, &conf_def) == -1)
        {
            free_assignment_list(&asn_list);
            free(config_def_str);
            return -1;
        }

        if (apply_config_def_to_attr(attr, asn.value, &conf_def) == -1)
        {
            free_config_def(&conf_def);
            free_assignment_list(&asn_list);
            free(config_def_str);
            return -1;
        }
    }
    return 0;
}

/*
 * Searches for the perf event "ev" in the pmu_events_map "map", returning the result
 * in "pmu_ev".
 *
 * On success, 0 is returned and the event is put into "pmu_ev"
 * On failure, -1 is returned.
 */
int get_event_by_name(const struct pmu_events_map* map, const char* ev, struct pmu_event* pmu_ev)
{
    for (int i = 0; i < map->event_table.num_pmus; i++)
    {
        struct pmu_table_entry entry = map->event_table.pmus[i];
        for (int x = 0; x < entry.num_entries; x++)
        {
            decompress_event(entry.entries[x].offset, pmu_ev);

            if (strcmp(pmu_ev->name, ev) == 0)
            {
                return 0;
            }
        }
    }
    return -1;
}
