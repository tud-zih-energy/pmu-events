#include <assert.h>
#include <complex.h>
#include <pmu-events/pmu-events.h>

#include <pmu-events/_impl/pmu-events.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * Base path for all PMU devices in sysfs[:we
 */
static const char* pmu_devices_base = "/sys/bus/event_source/devices";

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
 * For a pmu_table_entry, get the name of the pmu
 */
const char* get_pmu_name(struct pmu_table_entry entry);
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
 * Get a range list representing all CPUs in the system.
 */
static struct range_list all_cpus()
{
    struct range_list res;

    res.ranges = malloc(sizeof(struct range));
    res.len = 1;
    res.ranges[0].start = 0;
    res.ranges[0].end = sysconf(_SC_NPROCESSORS_ONLN) - 1;
    return res;
}

/*
 * Get [pmu_path]/cpus as a range_list
 *
 * Returns 0 on success, -1 on failure.
 *
 * If get_cpus_for() succeeds, the caller is responsible for free-ing range_list with
 * free_range_list()
 */
static int get_cpus_for(const char* pmu_path, struct range_list* range_list)
{
    char* cpus_path = concat_path(pmu_path, "cpus");
    if (access(cpus_path, F_OK) != F_OK)
    {
        free(cpus_path);
        return -1;
    }

    char* content = get_file_content(cpus_path);
    free(cpus_path);
    if (content == NULL)
    {
        return -1;
    }

    if (parse_range_list(content, range_list) == -1)
    {
        free(content);
        return -1;
    }
    free(content);
    return 0;
}

/*
 * Get [pmu_path]/cpumask as a range_list
 *
 * Returns 0 on success, -1 on failure.
 *
 * If get_cpumask_for() succeeds, the caller is responsible for free-ing range_list with
 * free_range_list()
 */
static int get_cpumask_for(const char* pmu_path, struct range_list* range_list)
{
    char* cpus_path = concat_path(pmu_path, "cpumask");
    if (access(cpus_path, F_OK) != F_OK)
    {
        free(cpus_path);
        return -1;
    }

    char* content = get_file_content(cpus_path);
    free(cpus_path);
    if (content == NULL)
    {
        return -1;
    }

    if (parse_range_list(content, range_list) == -1)
    {
        free(content);
        return -1;
    }
    free(content);
    return 0;
}

/*
 * Returns the content of  [path-to-pmu_instance]/format/[fmt_file]
 * This is usually a config def string, parseable
 * by parse_config_def() (e.g. "config1:0,23-42")
 *
 * Returns NULL on error.
 *
 * The string returned by get_format_file_content needs to be freed by the caller.
 */
char* get_format_file_content(char* fmt_file, const struct pmu_instance* pmu_instance)
{
    char* full_path = concat_path(pmu_devices_base, pmu_instance->name);
    if (full_path == NULL)
    {
        return NULL;
    }
    char* format_path = concat_path(full_path, "format/");
    if (format_path == NULL)
    {
        free(full_path);
        return NULL;
    }
    free(full_path);

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
 * Reads the perf_event_attr.type from [path to pmu_instance]/type
 *
 * Returns the perf_event_attr.type or -1 on failure.
 */
int read_perf_type(const struct pmu_instance* pmu_instance)
{
    char* full_path = concat_path(pmu_devices_base, pmu_instance->name);
    if (full_path == NULL)
    {
        return -1;
    }
    char* type_path = concat_path(full_path, "type");
    if (type_path == NULL)
    {
        free(full_path);
        return -1;
    }
    free(full_path);

    int fd = open(type_path, O_RDONLY);
    if (fd == -1)
    {
        free(type_path);
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

int gen_attr_for_event(const struct pmu_instance* pmu_instance, const struct pmu_event* ev,
                       struct perf_event_attr* attr)
{
    if ((attr->type = read_perf_type(pmu_instance)) == -1)
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

        if (strcmp(asn.key, "period") == 0)
        {
            continue;
        }

        char* config_def_str = get_format_file_content(asn.key, pmu_instance);
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

        free(config_def_str);

        if (apply_config_def_to_attr(attr, asn.value, &conf_def) == -1)
        {
            free_config_def(&conf_def);
            free_assignment_list(&asn_list);
            free(config_def_str);
            return -1;
        }
        free_config_def(&conf_def);
    }
    free_assignment_list(&asn_list);
    return 0;
}

/*
 * Searches for the perf event "ev" in the pmu_instance "pmu_instance",
 * returning the result in "pmu_ev".
 *
 * On success, 0 is returned and the event is put into "pmu_ev"
 * On failure, -1 is returned.
 */
int get_event_by_name(const struct pmu_instance* pmu_instance, const char* ev,
                      struct pmu_event* pmu_ev)
{
    for (size_t x = 0; x < pmu_instance->num_entries; x++)
    {
        decompress_event(pmu_instance->entries[x].offset, pmu_ev);

        if (strcmp(pmu_ev->name, ev) == 0)
        {
            return 0;
        }
    }

    return -1;
}

/*
 * Return a list of all instances for the given pmu_class class.
 *
 * The PMUs given by the underlying perf/pmu-events mechanism are
 * PMU _classes_, meaning that for every class there can be one to many instances.
 *
 * Every PMU instance in a given Linux installation has a folder in
 *      "/sys/bus/event_source/devices/"
 *
 * This function returns for a given struct pmu_class, the PMU instances found
 * in the system.
 *
 * There are two mechanisms for matching the pmu_class to PMU instances:
 *
 * - The PMU class name is "default_core": This PMU class contains the PMU events tied
 *   to the cores of the processor.
 *   - One most Intel and AMD x86 systems, there is a single "cpu" PMU instance.
 *     That PMU instance is responsible for all the events in the "default_core"
 *     PMU class on all cores.
 *   - If there is no "cpu" PMU instance folder, then the PMU instance folders that
 *     contain a "cpus" file belong to the "default_core" PMU class.
 *     This "cpus" file also tells you which cores this PMU instance is responsible for.
 *
 * - The PMU class is something other, like "uncore_arb".
 *   - If thre is only one instance of the PMU class,  the sysfs PMU instance
       folder is called just "[PMU CLASS]"
 *   - If they are n different instances of a PMU, then the instances are called
 *     "[PMU CLASS]_0" to "[PMU_CLASS]_(n-1)"
 *
 *
 * If class->num_instances is 0, then no PMU instances belonging to that PMU class could be
 * found. Sometimes, a kernel module might need to be loaded to make a PMU instance available.
 *
 * If class->num_instances is != 0, then the caller is responsible for free-ing the pmu_class
 * with free_pmu_class();
 */
static int get_all_pmu_instances_for(struct pmu_class* class)
{
    DIR* dfd;
    struct dirent* dp;
    bool is_cpu = false;

    class->instances = NULL;
    class->num_instances = 0;

    if ((dfd = opendir(pmu_devices_base)) == NULL)
    {
        return -1;
    }

    if (strcmp(class->name, "default_core") == 0)
    {
        is_cpu = true;
    }

    /*
     * Iterate over "/sys/bus/event_source/devices/"
     */
    while ((dp = readdir(dfd)) != NULL)
    {
        /*
         * Skip . and ..
         */
        if (strcmp(".", dp->d_name) == 0 || strcmp("..", dp->d_name) == 0)
        {
            continue;
        }

        if (is_cpu)
        {
            if (strcmp(dp->d_name, "cpu") == 0)
            {
                class->num_instances++;
                class->instances =
                    realloc(class->instances, sizeof(struct pmu_instance) * class->num_instances);

                class->instances[class->num_instances - 1].name = strdup("cpu");
                class->instances[class->num_instances - 1].cpus = all_cpus();

                return 0;
            }

            char* full_path = concat_path(pmu_devices_base, dp->d_name);

            struct range_list cpus;
            if (get_cpus_for(full_path, &cpus) == -1)
            {
                free(full_path);
                continue;
            }
            free(full_path);

            class->num_instances++;

            class->instances =
                realloc(class->instances, sizeof(struct pmu_instance) * class->num_instances);
            class->instances[class->num_instances - 1].name = strdup(dp->d_name);
            class->instances[class->num_instances - 1].cpus = cpus;
        }
        else
        {
            /*
             * There can be multiple instances of some PMUs per system. e.g.
             * one memory channel interface PMU instance per memory channel of the processor.
             *
             * These folders have a name of the form PMU_NAME(_[0-9]+)?
             *
             * To check if the current folder is an instance of the given PMU class,
             * We are dealing with uncore pmus here.
             * first check if class->name is a true prefix of dp->d_name
             *
             */
            if (strncmp(dp->d_name, class->name, strlen(class->name)) != 0)
            {
                continue;
            }

            /*
             * Check for exact matches (e.g. dp->d_name == class->name)
             *
             * For example, on Intel Alderlake there is a cpu_atom PMU class and
             * exactly one cpu_atom PMU instance.
             *
             */
            if (strlen(dp->d_name) != strlen(class->name))
            {
                /*
                 * Ok, class->name is shorter than dp->d_name.
                 *
                 * Check then, if we have a PMU instance or an unrelated PMU class
                 * that class->name is a prefix of.
                 *
                 * e.g., for a PMU class "foo" we are interested in "foo_0", "foo_42", but not
                 * "foobar_0" because that is from another "foobar" PMU class.
                 */
                if (strlen(dp->d_name) + 2 < strlen(class->name))
                {
                    continue;
                }

                // check if the PMU class name is followed by a underscore...
                if (*(dp->d_name + strlen(class->name)) != '_')
                {
                    continue;
                }

                // ...and then a number.
                char* endptr;
                errno = 0;
                strtoul(dp->d_name + strlen(class->name) + 1, &endptr, 10);
                if (errno != 0)
                {
                    continue;
                }
                if (*endptr != '\0')
                {
                    continue;
                }
            }

            class->num_instances++;

            class->instances =
                realloc(class->instances, sizeof(struct pmu_instance) * class->num_instances);
            class->instances[class->num_instances - 1].name = strdup(dp->d_name);

            struct range cpus;

            char* full_path = concat_path(pmu_devices_base, dp->d_name);
            struct range_list range_list;

            /*
             * If either [pmu-instance-path]/cpus or [pmu-instance-path]/cpumask exists
             * then it contains the list of CPUs for which this event can be perf_event_open'ed.
             *
             * Otherwise, the event is openable on all cores of the cpu.
             */
            if (get_cpus_for(full_path, &range_list) == -1)
            {
                if (get_cpumask_for(full_path, &range_list) == -1)
                {
                    range_list = all_cpus();
                }
            }
            free(full_path);
            class->instances[class->num_instances - 1].cpus = range_list;
        }
    }

    closedir(dfd);

    return 0;
}

/*
 * Gets the tree of all pmus in the system.
 *
 * This data structure takes the following form
 *
 * struct pmus
 *  |--> classes:
 *         struct pmu_class
 *          |--> name
 *          \--> instances:
 *                 struct pmu_instance
 *                  |--> struct range_list cpus
 *                  |--> name
 *                  \--> entries:
 *                        struct compact_pmu_event
 *
 * On success, returns 0, otherwise -1.
 * On success, the caller is responsible for free-ing the struct pmus using
 * free_pmus()
 */
int get_pmus(struct pmus* pmus)
{
    pmus->num_classes = 0;
    pmus->classes = NULL;

    struct perf_cpu cpu;
    // TODO: even on heterogeneous systems (i.e. Intel Alderlake with P/E Cores)
    // every CPU returns the same list of events. So assume (for now) that is the
    // same for every CPU, regardless of architecture
    //
    // TODO: What about heterogeneous multi-cpu systems, are they even allowed?
    cpu.cpu = 0;
    const struct pmu_events_map* map = map_for_cpu(cpu);

    for (int cur_pmu = 0; cur_pmu < map->event_table.num_pmus; cur_pmu++)
    {
        const char* pmu_name = get_pmu_name(map->event_table.pmus[cur_pmu]);

        struct pmu_class class;
        class.name = pmu_name;
        if (get_all_pmu_instances_for(&class) == -1)
        {
            continue;
        }

        if (class.num_instances == 0)
        {
            continue;
        }

        pmus->num_classes++;
        pmus->classes = realloc(pmus->classes, sizeof(struct pmu_class) * pmus->num_classes);
        pmus->classes[pmus->num_classes - 1] = class;

        for (size_t cur_instance = 0;
             cur_instance < pmus->classes[pmus->num_classes - 1].num_instances; cur_instance++)
        {
            pmus->classes[pmus->num_classes - 1].instances[cur_instance].entries =
                map->event_table.pmus[cur_pmu].entries;
            pmus->classes[pmus->num_classes - 1].instances[cur_instance].num_entries =
                map->event_table.pmus[cur_pmu].num_entries;
        }
    }

    if (pmus->num_classes == 0)
    {
        return -1;
    }

    return 0;
}

void free_pmu_instance(struct pmu_instance* instance)
{
    free_range_list(&instance->cpus);
    free(instance->name);
}

void free_pmu_class(struct pmu_class* class)
{
    for (size_t cur_instance = 0; cur_instance < class->num_instances; cur_instance++)
    {
        free_pmu_instance(&class->instances[cur_instance]);
    }
    free(class->instances);
}

void free_pmus(struct pmus* pmus)
{
    for (int cur_pmu = 0; cur_pmu < pmus->num_classes; cur_pmu++)
    {
        free_pmu_class(&pmus->classes[cur_pmu]);
    }
    free(pmus->classes);
}
