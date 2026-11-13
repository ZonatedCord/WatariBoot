/* SPDX-License-Identifier: MIT */
/* Watari EL2 T6g (PLAN S13): the layer's own checkpoint squares, 0-15 (the guest's checks are squares 16 and up,
 * wel2_hvc.h WEL2_CHK_SQUARE_BASE). One definition for the code, the host test and el2/boot/README.md "Checkpoints":
 * test_chk.c compares the table below with the README table row by row, so a renumbering that forgets the
 * documentation (which the day-2 procedure quotes) fails `make -C el2 test`. */
#ifndef WEL2_CHK_H
#define WEL2_CHK_H

enum wel2_layer_chk {
	WEL2_LCHK_RELOCATED   = 0,   /* start.S relocated the image, stage1_build found the framebuffer; MMU still off */
	WEL2_LCHK_STAGE1_ON   = 1,   /* s1_enable done: EL2 MMU and caches on */
	WEL2_LCHK_BANNER      = 2,   /* console known, banner and stage-1 report printed */
	WEL2_LCHK_DISCOVER    = 3,   /* platform discovery: cores known */
	WEL2_LCHK_IRQ_READY   = 4,   /* vGIC model and the SPI policy (AIC / GICD backend) ready, the boot core can take SPIs */
	WEL2_LCHK_CPU_INIT    = 5,   /* platform init done (init_boot, Apple FIQ module / GIC cpu interface, WHOAMI); wel2.step=2 ends here */
	WEL2_LCHK_STAGE2_ON   = 6,   /* guest stage 2 built and enabled */
	WEL2_LCHK_CPUIF       = 7,   /* virtual CPU interface on (trap / trap-notc / hw) */
	WEL2_LCHK_ENTER_GUEST = 8,   /* about to eret to the guest */
	WEL2_LCHK_CPU_ACCEPT  = 9,   /* a CPU_ON was accepted (M1; the core has not arrived yet) */
	WEL2_LCHK_CPU_ARRIVE  = 10,  /* a started core reached the layer and is about to eret to the guest (M1) */
	WEL2_LCHK_HALT        = 11,  /* SYSTEM_OFF, or SYSTEM_RESET without a watchdog node: the halt (M1) */
	/* 12-15 are free */
	WEL2_LCHK_COUNT       = 12
};

struct wel2_chk_name {
	unsigned n;
	const char *name;            /* the identifier shown in the README table (upper case, no spaces) */
};

#define WEL2_LCHK_NAMES { \
	{ WEL2_LCHK_RELOCATED,   "RELOCATED" },   { WEL2_LCHK_STAGE1_ON,   "STAGE1_ON" }, \
	{ WEL2_LCHK_BANNER,      "BANNER" },      { WEL2_LCHK_DISCOVER,    "DISCOVER" }, \
	{ WEL2_LCHK_IRQ_READY,   "IRQ_READY" },   { WEL2_LCHK_CPU_INIT,    "CPU_INIT" }, \
	{ WEL2_LCHK_STAGE2_ON,   "STAGE2_ON" },   { WEL2_LCHK_CPUIF,       "CPUIF" }, \
	{ WEL2_LCHK_ENTER_GUEST, "ENTER_GUEST" }, { WEL2_LCHK_CPU_ACCEPT,  "CPU_ACCEPT" }, \
	{ WEL2_LCHK_CPU_ARRIVE,  "CPU_ARRIVE" },  { WEL2_LCHK_HALT,        "HALT" }, \
}

#endif
