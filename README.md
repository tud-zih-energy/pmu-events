# pmu-events

## Stand-alone library version of the pmu-events perf event naming code

The Linux tool `perf` contains information about a wealth of
CPU PMU events.

The code handling these event definitions lives in the `tools/perf/pmu-events` folder of 
the Linux source code.

This library takes the code for working with those event definitions
and makes it available as a stand-alone library for external tools to reach 1:1
compatability with perf event names.

## Dependencies

- A recent C compiler.
- Python 3 to generate the pmu-events.c from the JSON event definitions.
- Either an x86_64 or an ARM64 architecture.
## Example

For a detailed example, see `examples/main.c`.

A short example that tries to find and open the "l3_misses" event on
cpu 0 is given below:

```c
struct perf_cpu cpu;
cpu.cpu = 0;

struct pmu_event pmu_ev;
if (get_event_by_name(map_for_cpu(cpu), "l3_misses", &pmu_ev) == -1)
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
```

## License

This project, like the original Linux kernel code is licensed under the terms
of the GPL 2.0. For more information, see LICENSE.

## Original pmu-events README

The contents of this directory allow users to specify PMU events in their
CPUs by their symbolic names rather than raw event codes (see example below).

The main program in this directory, is the 'jevents', which is built and
executed _BEFORE_ the perf binary itself is built.

The 'jevents' program tries to locate and process JSON files in the directory
tree tools/perf/pmu-events/arch/foo.

	- Regular files with '.json' extension in the name are assumed to be
	  JSON files, each of which describes a set of PMU events.

	- The CSV file that maps a specific CPU to its set of PMU events is to
	  be named 'mapfile.csv' (see below for mapfile format).

	- Directories are traversed, but all other files are ignored.

	- To reduce JSON event duplication per architecture, platform JSONs may
	  use "ArchStdEvent" keyword to dereference an "Architecture standard
	  events", defined in architecture standard JSONs.
	  Architecture standard JSONs must be located in the architecture root
	  folder. Matching is based on the "EventName" field.

The PMU events supported by a CPU model are expected to grouped into topics
such as Pipelining, Cache, Memory, Floating-point etc. All events for a topic
should be placed in a separate JSON file - where the file name identifies
the topic. Eg: "Floating-point.json".

All the topic JSON files for a CPU model/family should be in a separate
sub directory. Thus for the Silvermont X86 CPU:

	$ ls tools/perf/pmu-events/arch/x86/silvermont
	cache.json     memory.json    virtual-memory.json
	frontend.json  pipeline.json

The JSONs folder for a CPU model/family may be placed in the root arch
folder, or may be placed in a vendor sub-folder under the arch folder
for instances where the arch and vendor are not the same.

Using the JSON files and the mapfile, 'jevents' generates the C source file,
'pmu-events.c', which encodes the two sets of tables:

	- Set of 'PMU events tables' for all known CPUs in the architecture,
	  (one table like the following, per JSON file; table name 'pme_power8'
	  is derived from JSON file name, 'power8.json').

		struct pmu_event pme_power8[] = {

			...

			{
				.name = "pm_1plus_ppc_cmpl",
				.event = "event=0x100f2",
				.desc = "1 or more ppc insts finished,",
			},

			...
		}

	- A 'mapping table' that maps each CPU of the architecture, to its
	  'PMU events table'

		struct pmu_events_map pmu_events_map[] = {
		{
			.cpuid = "004b0000",
			.version = "1",
			.type = "core",
			.table = pme_power8
		},
			...

		};

After the 'pmu-events.c' is generated, it is compiled and the resulting
'pmu-events.o' is added to 'libperf.a' which is then used to build perf.

NOTES:
	1. Several CPUs can support same set of events and hence use a common
	   JSON file. Hence several entries in the pmu_events_map[] could map
	   to a single 'PMU events table'.

	2. The 'pmu-events.h' has an extern declaration for the mapping table
	   and the generated 'pmu-events.c' defines this table.

	3. _All_ known CPU tables for architecture are included in the perf
	   binary.

At run time, perf determines the actual CPU it is running on, finds the
matching events table and builds aliases for those events. This allows
users to specify events by their name:

	$ perf stat -e pm_1plus_ppc_cmpl sleep 1

where 'pm_1plus_ppc_cmpl' is a Power8 PMU event.

However some errors in processing may cause the alias build to fail.

Mapfile format
===============

The mapfile enables multiple CPU models to share a single set of PMU events.
It is required even if such mapping is 1:1.

The mapfile.csv format is expected to be:

	Header line
	CPUID,Version,Dir/path/name,Type

where:

	Comma:
		is the required field delimiter (i.e other fields cannot
		have commas within them).

	Comments:
		Lines in which the first character is either '\n' or '#'
		are ignored.

	Header line
		The header line is the first line in the file, which is
		always _IGNORED_. It can be empty.

	CPUID:
		CPUID is an arch-specific char string, that can be used
		to identify CPU (and associate it with a set of PMU events
		it supports). Multiple CPUIDS can point to the same
		File/path/name.json.

		Example:
			CPUID == 'GenuineIntel-6-2E' (on x86).
			CPUID == '004b0100' (PVR value in Powerpc)
	Version:
		is the Version of the mapfile.

	Dir/path/name:
		is the pathname to the directory containing the CPU's JSON
		files, relative to the directory containing the mapfile.csv

	Type:
		indicates whether the events are "core" or "uncore" events.


	Eg:

	$ grep silvermont tools/perf/pmu-events/arch/x86/mapfile.csv
	GenuineIntel-6-37,v13,silvermont,core
	GenuineIntel-6-4D,v13,silvermont,core
	GenuineIntel-6-4C,v13,silvermont,core

	i.e the three CPU models use the JSON files (i.e PMU events) listed
	in the directory 'tools/perf/pmu-events/arch/x86/silvermont'.
