// SPDX-License-Identifier: MIT
// fdt_m1_dump: host tool for rung L0 of the M3a hardware day. Reads a flattened device tree file and prints exactly
// what the Watari EL2 layer would use from it (fdt_m1_parse), and NOTHING else of the blob: no serial number,
// SMBIOS data, MAC address or seed is ever read, so none can appear in the output.
// Build: make -C el2 (build/fdt_m1_dump). Usage: fdt_m1_dump file.dtb
// Exit: 0 parsed, 1 malformed blob, 2 usage or file error.

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include "fdt.h"
#include "fdt_m1.h"

#define X64(v) ((uint64_t)(v))

static void range(const char *tag, unsigned i, const struct m1_range *r)
{
	printf("%s %u base=0x%" PRIx64 " size=0x%" PRIx64 " end=0x%" PRIx64 "\n", tag, i, X64(r->base), X64(r->size),
	       X64(r->base + r->size));
}

int main(int argc, char **argv)
{
	if (argc != 2) {
		fprintf(stderr, "usage: %s file.dtb\n", argv[0]);
		return 2;
	}
	FILE *f = fopen(argv[1], "rb");
	if (!f) {
		perror(argv[1]);
		return 2;
	}
	/* 8-byte aligned buffer, at most 16 MB */
	size_t cap = 16u << 20, n;
	uint64_t *mem = malloc(cap);
	if (!mem) {
		fclose(f);
		return 2;
	}
	n = fread(mem, 1, cap, f);
	fclose(f);
	if (n < 40) {
		fprintf(stderr, "%s: too short\n", argv[1]);
		free(mem);
		return 1;
	}
	/* a blob whose totalsize is larger than the file is truncated: pad with zeros so reads stay in the buffer, and
	 * refuse it */
	if (fdt_totalsize(mem) > n) {
		fprintf(stderr, "%s: file shorter than the header's totalsize (%u > %zu): truncated blob\n", argv[1],
			fdt_totalsize(mem), n);
		free(mem);
		return 1;
	}
	struct m1_platform *p = malloc(sizeof(*p));
	if (!p) {
		free(mem);
		return 2;
	}
	if (fdt_m1_parse(mem, p)) {
		fprintf(stderr, "%s: malformed device tree\n", argv[1]);
		free(p);
		free(mem);
		return 1;
	}
	printf("platform %s\n", p->is_m1 ? "m1 (apple,aic found)" : "qemu (no apple,aic node)");
	for (unsigned i = 0; i < p->ncpu; i++)
		printf("cpu %u reg=0x%" PRIx64 " spin_table=%d release_addr=%s0x%" PRIx64 "\n", i, X64(p->cpu[i].reg),
		       p->cpu[i].spin_table, p->cpu[i].has_release ? "" : "(none) ", X64(p->cpu[i].release_addr));
	if (p->cpus_truncated)
		printf("cpu list truncated at %d\n", M1_MAX_CPUS);
	for (unsigned i = 0; i < p->nmem; i++)
		range("memory", i, &p->mem[i]);
	for (unsigned i = 0; i < p->nresv; i++)
		range("reserved", i, &p->resv[i]);
	for (unsigned i = 0; i < p->nusable; i++)
		range("usable", i, &p->usable[i]);
	printf("memory pairs wholly reserved: %u\n", p->mem_wholly_reserved);
	if (p->lists_truncated)
		printf("WARNING: a list was truncated (listed ranges are safe, not complete)\n");
	if (p->bad_ranges)
		printf("ignored zero-sized or wrapping ranges: %u\n", p->bad_ranges);
	for (unsigned i = 0; i < p->naic; i++)
		range("aic", i, &p->aic[i]);
	if (p->uart.found)
		printf("uart s5l base=0x%" PRIx64 " size=0x%" PRIx64 " chosen_by=%s\n", X64(p->uart.base), X64(p->uart.size),
		       p->uart.from_stdout ? "stdout-path/serial0" : "first-found");
	else
		printf("uart none\n");
	if (p->wdt.found)
		printf("wdt base=0x%" PRIx64 " size=0x%" PRIx64 "\n", X64(p->wdt.base), X64(p->wdt.size));
	else
		printf("wdt none\n");
	if (p->fb.fmt != WEL2_FB_NONE)
		printf("framebuffer base=0x%" PRIx64 " %ux%u stride=%u format=%s\n", X64(p->fb.base), p->fb.width, p->fb.height,
		       p->fb.stride, p->fb.fmt == WEL2_FB_X2R10G10B10 ? "x2r10g10b10" : "x8r8g8b8");
	else
		printf("framebuffer none\n");
	free(p);
	free(mem);
	return 0;
}
