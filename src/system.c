#include "system.h"

#if defined(__FreeBSD__) || defined(__DragonFly__)
# include <sys/types.h>
# include <sys/sysctl.h>
#endif

#if defined(__OpenBSD__) || defined(__NetBSD__)
# include <sys/param.h>
# include <sys/sysctl.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int
system_cpu_count(void)
{
	int generic = 2;
	int cores = 0;
	const char *cpu_count = getenv("NUMBER_OF_PROCESSORS");
	if (cpu_count) {
		cores = atoi(cpu_count);
		return (cores);
	}
#if defined(__FreeBSD__) || defined(__DragonFly__) || defined(__OpenBSD__) || defined(__NetBSD__)
	size_t len;
	int mib[2] = { CTL_HW, HW_NCPU };

	len = sizeof(cores);
	if (sysctl(mib, 2, &cores, &len, NULL, 0) != -1) 
		return (cores);
#elif defined(__linux__)
	char buf[4096];
	FILE *f = fopen("/proc/stat", "r");
	if (!f) return (generic);
	int line = 0;
	while (fgets(buf, sizeof(buf), f)) {
		if (line) {
			char *tok = strtok(buf, " ");
			if (!strncmp(tok, "cpu", 3))
				++cores;
			else break;
		}	
		line++;
	}
	fclose(f);

	if (cores)
		return (cores);
#endif  // FALL THROUGH
	return (generic);
}
