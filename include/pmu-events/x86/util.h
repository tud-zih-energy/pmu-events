// SPDX-License-Identifier: GPL-2.0

// Various cpuid handling code lifted from:
// linux/tools/perf/arch/x86/util/header.c
// and
// linux/tools/perf/arch/x86/util/cpuid.h

#ifndef PMU_EVENTS_X86_UTIL
#define PMU_EVENTS_X86_UTIL

#include <pmu-events/types.h>

#include <errno.h>
#include <regex.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static inline void cpuid(unsigned int op, unsigned int op2, unsigned int* a, unsigned int* b,
                         unsigned int* c, unsigned int* d)
{
    /*
     * Preserve %ebx/%rbx register by either placing it in %rdi or saving it
     * on the stack - x86-64 needs to avoid the stack red zone. In PIC
     * compilations %ebx contains the address of the global offset
     * table. %rbx is occasionally used to address stack variables in
     * presence of dynamic allocas.
     */
    asm(
#if defined(__x86_64__)
        "mov %%rbx, %%rdi\n"
        "cpuid\n"
        "xchg %%rdi, %%rbx\n"
#else
        "pushl %%ebx\n"
        "cpuid\n"
        "movl %%ebx, %%edi\n"
        "popl %%ebx\n"
#endif
        : "=a"(*a), "=D"(*b), "=c"(*c), "=d"(*d)
        : "a"(op), "2"(op2));
}

void get_cpuid_0(char* vendor, unsigned int* lvl)
{
    unsigned int b, c, d;

    cpuid(0, 0, lvl, &b, &c, &d);
    strncpy(&vendor[0], (char*)(&b), 4);
    strncpy(&vendor[4], (char*)(&d), 4);
    strncpy(&vendor[8], (char*)(&c), 4);
    vendor[12] = '\0';
}

int __get_cpuid(char* buffer, size_t sz, const char* fmt)
{
    unsigned int a, b, c, d, lvl;
    int family = -1, model = -1, step = -1;
    int nb;
    char vendor[16];

    get_cpuid_0(vendor, &lvl);

    if (lvl >= 1)
    {
        cpuid(1, 0, &a, &b, &c, &d);

        family = (a >> 8) & 0xf; /* bits 11 - 8 */
        model = (a >> 4) & 0xf;  /* Bits  7 - 4 */
        step = a & 0xf;

        /* extended family */
        if (family == 0xf)
            family += (a >> 20) & 0xff;

        /* extended model */
        if (family >= 0x6)
            model += ((a >> 16) & 0xf) << 4;
    }
    nb = snprintf(buffer, sz, fmt, vendor, family, model, step);

    /* look 4for end marker to ensure the entire data fit */
    if (strchr(buffer, '$'))
    {
        buffer[nb - 1] = '\0';
        return 0;
    }
    return ENOBUFS;
}

char* get_cpuid_str(struct perf_cpu cpu)
{
    char* buf = malloc(128);

    if (buf && __get_cpuid(buf, 128, "%s-%u-%X-%X$") < 0)
    {
        free(buf);
        return NULL;
    }
    return buf;
}

static bool is_full_cpuid(const char* id)
{
    const char* tmp = id;
    int count = 0;

    while ((tmp = strchr(tmp, '-')) != NULL)
    {
        count++;
        tmp++;
    }

    if (count == 3)
        return true;

    return false;
}

int strcmp_cpuid_str(const char* mapcpuid, const char* id)
{
    regex_t re;
    regmatch_t pmatch[1];
    int match;
    bool full_mapcpuid = is_full_cpuid(mapcpuid);
    bool full_cpuid = is_full_cpuid(id);

    /*
     * Full CPUID format is required to identify a platform.
     * Error out if the cpuid string is incomplete.
     */
    if (full_mapcpuid && !full_cpuid)
    {
        return 1;
    }

    if (regcomp(&re, mapcpuid, REG_EXTENDED) != 0)
    {
        return 1;
    }

    match = !regexec(&re, id, 1, pmatch, 0);
    regfree(&re);
    if (match)
    {
        size_t match_len = (pmatch[0].rm_eo - pmatch[0].rm_so);
        size_t cpuid_len;

        /* If the full CPUID format isn't required,
         * ignoring the stepping.
         */
        if (!full_mapcpuid && full_cpuid)
            cpuid_len = strrchr(id, '-') - id;
        else
            cpuid_len = strlen(id);

        /* Verify the entire string matched. */
        if (match_len == cpuid_len)
            return 0;
    }

    return 1;
}
#endif
