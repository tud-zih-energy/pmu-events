/* SPDX-License-Identifier: GPL-2.0 */

/* Originally from linux/tools/perf/pmu-events/pmu-events.h
 * commit: cec1e6e5d1ab33403b809f79cd20d6aff124ccfe
 */
#ifndef PMU_EVENTS_H
#define PMU_EVENTS_H

#include <pmu-events/types.h>

#include <complex.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <linux/perf_event.h>

/*
 * Get the structure of all PMUs
 */
int get_pmus(struct pmus* pmus);
void free_pmus(struct pmus* pmus);

/*
 * Resolve the event name "ev" in the pmu_instance "pmu_instance", putting
 * the result into "pmu_ev"
 *
 * Return 0 on success, -1 on failure
 */
int get_event_by_name(const struct pmu_instance* pmu_instance, const char* ev,
                      struct pmu_event* pmu_ev);

/*
 * For the given "pmu_instance, and "ev", set the perf_event_attr to be able to then open
 * the event.
 *
 *
 *
 * Returns 0 on success, -1 on failure
 */
int gen_attr_for_event(const struct pmu_instance* pmu_instance, const struct pmu_event* ev,
                       struct perf_event_attr* attr);

#endif
