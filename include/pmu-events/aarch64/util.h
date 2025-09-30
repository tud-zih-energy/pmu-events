// SPDX-License-Identifier: GPL-2.0
#ifndef _AARCH64_UTIL_H_
#define _AARCH64_UTIL_H_

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/limits.h>
#include <pmu-events/pmu-events.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * linux/tool/include/uapi/linux/const.h
 */

#define __AC(X, Y) (X##Y)
#define _AC(X, Y) __AC(X, Y)

#define _UL(x) (_AC(x, UL))

#define GENMASK(h, l) (((~_UL(0)) << (l)) & (~_UL(0) >> (__BITS_PER_LONG - 1 - (h))))

/*
 * linux/tools/include/linux/bitfield.h
 */

#define __bf_shf(x) (__builtin_ffsll(x) - 1)
#define FIELD_GET(_mask, _reg) ({ (typeof(_mask))(((_reg) & (_mask)) >> __bf_shf(_mask)); })

/*
 * linux/tools/lib/api/io.h
 */

struct io
{
    /* File descriptor being read/ */
    int fd;
    /* Size of the read buffer. */
    unsigned int buf_len;
    /* Pointer to storage for buffering read. */
    char* buf;
    /* End of the storage. */
    char* end;
    /* Currently accessed data pointer. */
    char* data;
    /* Read timeout, 0 implies no timeout. */
    int timeout_ms;
    /* Set true on when the end of file on read error. */
    bool eof;
};

static inline void io__init(struct io* io, int fd, char* buf, unsigned int buf_len)
{
    io->fd = fd;
    io->buf_len = buf_len;
    io->buf = buf;
    io->end = buf;
    io->data = buf;
    io->timeout_ms = 0;
    io->eof = false;
}

static inline int io__fill_buffer(struct io* io)
{
    ssize_t n;

    if (io->eof)
        return -1;

    if (io->timeout_ms != 0)
    {
        struct pollfd pfds[] = {
            {
                .fd = io->fd,
                .events = POLLIN,
            },
        };

        n = poll(pfds, 1, io->timeout_ms);
        if (n == 0)
            errno = ETIMEDOUT;
        if (n > 0 && !(pfds[0].revents & POLLIN))
        {
            errno = EIO;
            n = -1;
        }
        if (n <= 0)
        {
            io->eof = true;
            return -1;
        }
    }
    n = read(io->fd, io->buf, io->buf_len);

    if (n <= 0)
    {
        io->eof = true;
        return -1;
    }
    io->data = &io->buf[0];
    io->end = &io->buf[n];
    return 0;
}

/* Reads one character from the "io" file with similar semantics to fgetc. */
static inline int io__get_char(struct io* io)
{
    if (io->data == io->end)
    {
        int ret = io__fill_buffer(io);

        if (ret)
            return ret;
    }
    return *io->data++;
}

/* Read up to and including the first delim. */
static inline ssize_t io__getdelim(struct io* io, char** line_out, size_t* line_len_out, int delim)
{
    char buf[128];
    int buf_pos = 0;
    char *line = NULL, *temp;
    size_t line_len = 0;
    int ch = 0;

    /* TODO: reuse previously allocated memory. */
    free(*line_out);
    while (ch != delim)
    {
        ch = io__get_char(io);

        if (ch < 0)
            break;

        if (buf_pos == sizeof(buf))
        {
            temp = realloc(line, line_len + sizeof(buf));
            if (!temp)
                goto err_out;
            line = temp;
            memcpy(&line[line_len], buf, sizeof(buf));
            line_len += sizeof(buf);
            buf_pos = 0;
        }
        buf[buf_pos++] = (char)ch;
    }
    temp = realloc(line, line_len + buf_pos + 1);
    if (!temp)
        goto err_out;
    line = temp;
    memcpy(&line[line_len], buf, buf_pos);
    line[line_len + buf_pos] = '\0';
    line_len += buf_pos;
    *line_out = line;
    *line_len_out = line_len;
    return line_len;
err_out:
    free(line);
    *line_out = NULL;
    *line_len_out = 0;
    return -ENOMEM;
}

/*
 * linux/tools/lib/api/fs/fs.c
 */

int filename__read_str(const char* filename, char** buf, size_t* sizep)
{
    struct io io;
    char bf[128];
    int err;

    io.fd = open(filename, O_RDONLY);
    if (io.fd < 0)
        return -errno;
    io__init(&io, io.fd, bf, sizeof(bf));
    *buf = NULL;
    err = io__getdelim(&io, buf, sizep, /*delim=*/-1);
    if (err < 0)
    {
        free(*buf);
        *buf = NULL;
    }
    else
        err = 0;
    close(io.fd);
    return err;
}

static int sysfs__read_str(const char* entry, char** buf, size_t* sizep)
{
    char path[PATH_MAX];

    snprintf(path, sizeof(path), "/sys/%s", entry);

    return filename__read_str(path, buf, sizep);
}

/*
 * linux/tools/lib/perf/cpumap.c
 */

struct perf_cpu_map
{
    /** Length of the map array. */
    int nr;
    /** The CPU values. */
    struct perf_cpu map[];
};

struct perf_cpu perf_cpu_map__cpu(const struct perf_cpu_map* cpus, int idx)
{
    struct perf_cpu result = { .cpu = -1 };

    if (cpus && idx < cpus->nr)
        return cpus->map[idx];

    return result;
}

#define perf_cpu_map__for_each_cpu(cpu, idx, cpus)                                                 \
    for ((idx) = 0, (cpu) = perf_cpu_map__cpu(cpus, idx); (idx) < cpus->nr;                        \
         (idx)++, (cpu) = perf_cpu_map__cpu(cpus, idx))

struct perf_cpu_map* perf_cpu_map__new_online_cpus(void);
static struct perf_cpu_map* cpu_map__trim_new(int nr_cpus, const struct perf_cpu* tmp_cpus);
struct perf_cpu_map* perf_cpu_map__alloc(int nr_cpus);

struct perf_cpu_map* perf_cpu_map__new_any_cpu(void)
{
    struct perf_cpu_map* cpus = perf_cpu_map__alloc(1);

    if (cpus)
        cpus->map[0].cpu = -1;

    return cpus;
}

struct perf_cpu_map* perf_cpu_map__new(const char* cpu_list)
{
    struct perf_cpu_map* cpus = NULL;
    unsigned long start_cpu, end_cpu = 0;
    char* p = NULL;
    int i, nr_cpus = 0;
    struct perf_cpu *tmp_cpus = NULL, *tmp;
    int max_entries = 0;

    if (!cpu_list)
        return perf_cpu_map__new_online_cpus();

    /*
     * must handle the case of empty cpumap to cover
     * TOPOLOGY header for NUMA nodes with no CPU
     * ( e.g., because of CPU hotplug)
     */
    if (!isdigit(*cpu_list) && *cpu_list != '\0')
        goto out;

    while (isdigit(*cpu_list))
    {
        p = NULL;
        start_cpu = strtoul(cpu_list, &p, 0);
        if (start_cpu >= INT16_MAX || (*p != '\0' && *p != ',' && *p != '-' && *p != '\n'))
            goto invalid;

        if (*p == '-')
        {
            cpu_list = ++p;
            p = NULL;
            end_cpu = strtoul(cpu_list, &p, 0);

            if (end_cpu >= INT16_MAX || (*p != '\0' && *p != ',' && *p != '\n'))
                goto invalid;

            if (end_cpu < start_cpu)
                goto invalid;
        }
        else
        {
            end_cpu = start_cpu;
        }

        for (; start_cpu <= end_cpu; start_cpu++)
        {
            /* check for duplicates */
            for (i = 0; i < nr_cpus; i++)
                if (tmp_cpus[i].cpu == (int16_t)start_cpu)
                    goto invalid;

            if (nr_cpus == max_entries)
            {
                if (end_cpu - start_cpu + 1 > 16UL)
                {
                    max_entries += end_cpu - start_cpu + 1;
                }
                else
                {
                    max_entries += 16UL;
                }
                tmp = realloc(tmp_cpus, max_entries * sizeof(struct perf_cpu));
                if (tmp == NULL)
                    goto invalid;
                tmp_cpus = tmp;
            }
            tmp_cpus[nr_cpus++].cpu = (int16_t)start_cpu;
        }
        if (*p)
            ++p;

        cpu_list = p;
    }

    if (nr_cpus > 0)
    {
        cpus = cpu_map__trim_new(nr_cpus, tmp_cpus);
    }
    else if (*cpu_list != '\0')
    {
        cpus = perf_cpu_map__new_online_cpus();
    }
    else
    {
        cpus = perf_cpu_map__new_any_cpu();
    }
invalid:
    free(tmp_cpus);
out:
    return cpus;
}

static int cmp_cpu(const void* a, const void* b)
{
    const struct perf_cpu *cpu_a = a, *cpu_b = b;

    return cpu_a->cpu - cpu_b->cpu;
}

static struct perf_cpu_map* cpu_map__trim_new(int nr_cpus, const struct perf_cpu* tmp_cpus)
{
    size_t payload_size = nr_cpus * sizeof(struct perf_cpu);
    struct perf_cpu_map* cpus = perf_cpu_map__alloc(nr_cpus);
    int i, j;

    if (cpus != NULL)
    {
        memcpy(cpus->map, tmp_cpus, payload_size);
        qsort(cpus->map, nr_cpus, sizeof(struct perf_cpu), cmp_cpu);
        /* Remove dups */
        j = 0;
        for (i = 0; i < nr_cpus; i++)
        {
            if (i == 0 || cpus->map[i].cpu != cpus->map[i - 1].cpu)
            {
                cpus->map[j++].cpu = cpus->map[i].cpu;
            }
        }
        cpus->nr = j;
        assert(j <= nr_cpus);
    }
    return cpus;
}

static struct perf_cpu_map* cpu_map__new_sysfs_online(void)
{
    struct perf_cpu_map* cpus = NULL;
    char* buf = NULL;
    size_t buf_len;

    if (sysfs__read_str("devices/system/cpu/online", &buf, &buf_len) >= 0)
    {
        cpus = perf_cpu_map__new(buf);
        free(buf);
    }
    return cpus;
}

struct perf_cpu_map* perf_cpu_map__alloc(int nr_cpus)
{
    struct perf_cpu_map* cpus;

    if (nr_cpus == 0)
        return NULL;

    cpus = malloc(sizeof(*cpus) + sizeof(struct perf_cpu) * nr_cpus);
    return cpus;
}

static struct perf_cpu_map* cpu_map__new_sysconf(void)
{
    struct perf_cpu_map* cpus;
    int nr_cpus, nr_cpus_conf;

    nr_cpus = sysconf(_SC_NPROCESSORS_ONLN);
    if (nr_cpus < 0)
        return NULL;

    nr_cpus_conf = sysconf(_SC_NPROCESSORS_CONF);

    cpus = perf_cpu_map__alloc(nr_cpus);
    if (cpus != NULL)
    {
        int i;

        for (i = 0; i < nr_cpus; ++i)
            cpus->map[i].cpu = i;
    }

    return cpus;
}

struct perf_cpu_map* perf_cpu_map__new_online_cpus(void)
{
    struct perf_cpu_map* cpus = cpu_map__new_sysfs_online();

    if (cpus)
        return cpus;

    return cpu_map__new_sysconf();
}

/*
 * linux/tools/perf/arch/arm64/util/header.c
 */

#define MIDR_SIZE 19
#define MIDR "/regs/identification/midr_el1"

#define MIDR_REVISION_MASK GENMASK(3, 0)
#define MIDR_VARIANT_MASK GENMASK(23, 20)

static int _get_cpuid(char* buf, size_t sz, struct perf_cpu cpu)
{
    char path[PATH_MAX];
    FILE* file;

    assert(cpu.cpu != -1);

    snprintf(path, PATH_MAX, "/sys/devices/system/cpu/cpu%d" MIDR, cpu.cpu);

    file = fopen(path, "r");
    if (!file)
    {
        return EINVAL;
    }

    if (!fgets(buf, MIDR_SIZE, file))
    {
        fclose(file);
        return EINVAL;
    }
    fclose(file);
    return 0;
}

int get_cpuid(char* buf, size_t sz, struct perf_cpu cpu)
{
    struct perf_cpu_map* cpus;
    int idx;

    if (cpu.cpu != -1)
        return _get_cpuid(buf, sz, cpu);

    cpus = perf_cpu_map__new_online_cpus();
    if (!cpus)
        return EINVAL;

    perf_cpu_map__for_each_cpu(cpu, idx, cpus)
    {
        int ret = _get_cpuid(buf, sz, cpu);

        if (ret == 0)
            return 0;
    }
    return EINVAL;
}

char* get_cpuid_str(struct perf_cpu cpu)
{
    char* buf = malloc(MIDR_SIZE);
    int res;

    if (!buf)
        return NULL;

    /* read midr from list of cpus mapped to this pmu */
    res = get_cpuid(buf, MIDR_SIZE, cpu);
    if (res)
    {
        buf = NULL;
    }

    return buf;
}

/*
 * Return 0 if idstr is a higher or equal to version of the same part as
 * mapcpuid. Therefore, if mapcpuid has 0 for revision and variant then any
 * version of idstr will match as long as it's the same CPU type.
 *
 * Return 1 if the CPU type is different or the version of idstr is lower.
 */
int strcmp_cpuid_str(const char* mapcpuid, const char* idstr)
{
    uint64_t map_id = strtoull(mapcpuid, NULL, 16);
    char map_id_variant = FIELD_GET(MIDR_VARIANT_MASK, map_id);
    char map_id_revision = FIELD_GET(MIDR_REVISION_MASK, map_id);
    uint64_t id = strtoull(idstr, NULL, 16);
    char id_variant = FIELD_GET(MIDR_VARIANT_MASK, id);
    char id_revision = FIELD_GET(MIDR_REVISION_MASK, id);
    uint64_t id_fields = ~(MIDR_VARIANT_MASK | MIDR_REVISION_MASK);

    /* Compare without version first */
    if ((map_id & id_fields) != (id & id_fields))
        return 1;

    /*
     * ID matches, now compare version.
     *
     * Arm revisions (like r0p0) are compared here like two digit semver
     * values eg. 1.3 < 2.0 < 2.1 < 2.2.
     *
     *  r = high value = 'Variant' field in MIDR
     *  p = low value  = 'Revision' field in MIDR
     *
     */
    if (id_variant > map_id_variant)
        return 0;

    if (id_variant == map_id_variant && id_revision >= map_id_revision)
        return 0;

    /*
     * variant is less than mapfile variant or variants are the same but
     * the revision doesn't match. Return no match.
     */
    return 1;
}
#endif
