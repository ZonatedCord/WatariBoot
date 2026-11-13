/* SPDX-License-Identifier: MIT */
/* Host test (T6g, PLAN S13): the checkpoint numbers of the code (include/wel2_chk.h, include/wel2_hvc.h) match the table in
 * el2/boot/README.md "Checkpoints" row by row, because the day-2 procedure quotes that table. Run from el2/ (make test). */
#include <string.h>
#include "test.h"
#include "wel2_chk.h"
#include "wel2_hvc.h"

static char g_readme[200000];
static size_t g_len;

/* Does the README contain a table row "| <n> | `<name>` |" ? */
static bool row(unsigned n, const char *name)
{
	char key[96];

	snprintf(key, sizeof(key), "| %u | `%s` |", n, name);
	return strstr(g_readme, key) != NULL;
}

static void t_readme_layer_rows(void)
{
	static const struct wel2_chk_name names[] = WEL2_LCHK_NAMES;

	CHECK(g_len > 0);
	CHECK_EQ(sizeof(names) / sizeof(names[0]), WEL2_LCHK_COUNT);
	for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
		CHECK_EQ(names[i].n, i);                      /* the table is in number order, no gap */
		if (!row(names[i].n, names[i].name))
			printf("    README row missing: | %u | `%s` |\n", names[i].n, names[i].name);
		CHECK(row(names[i].n, names[i].name));
	}
	CHECK(WEL2_LCHK_COUNT <= WEL2_CHK_SQUARE_BASE);         /* the layer's squares never reach the guest's */
}

static void t_readme_guest_rows(void)
{
	static const struct { unsigned n; const char *name; } g[] = {
		{ WEL2_CHK_G1, "G1" }, { WEL2_CHK_G2, "G2" }, { WEL2_CHK_G3, "G3" }, { WEL2_CHK_G4, "G4" },
		{ WEL2_CHK_G5A, "G5a" }, { WEL2_CHK_G5B, "G5b" }, { WEL2_CHK_G6, "G6" }, { WEL2_CHK_G7, "G7" },
		{ WEL2_CHK_G8, "G8" }, { WEL2_CHK_G9, "G9" }, { WEL2_CHK_G10, "G10" }, { WEL2_CHK_G11, "G11" }, { WEL2_CHK_G12, "G12" },
	};

	for (unsigned i = 0; i < sizeof(g) / sizeof(g[0]); i++) {
		if (!row(WEL2_CHK_SQUARE_BASE + g[i].n, g[i].name))
			printf("    README row missing: | %u | `%s` |\n", WEL2_CHK_SQUARE_BASE + g[i].n, g[i].name);
		CHECK(row(WEL2_CHK_SQUARE_BASE + g[i].n, g[i].name));
	}
	CHECK_EQ(WEL2_CHK_LAST, 13);
}

int main(void)
{
	FILE *f = fopen("boot/README.md", "r");

	if (f) {
		g_len = fread(g_readme, 1, sizeof(g_readme) - 1, f);
		fclose(f);
	}
	g_readme[g_len] = 0;
	RUN(t_readme_layer_rows);
	RUN(t_readme_guest_rows);
	TESTS_DONE();
}
