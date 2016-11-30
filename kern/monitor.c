// Simple command-line kernel monitor useful for
// controlling the kernel and exploring the system interactively.

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/memlayout.h>
#include <inc/assert.h>
#include <inc/x86.h>

#include <kern/console.h>
#include <kern/monitor.h>
#include <kern/kdebug.h>
#include <kern/trap.h>

#define CMDBUF_SIZE	80	// enough for one VGA text line


struct Command {
	const char *name;
	const char *desc;
	// return -1 to force monitor to exit
	int (*func)(int argc, char** argv, struct Trapframe* tf);
};

static struct Command commands[] = {
	{ "help", "Display this list of commands", mon_help },
	{ "kerninfo", "Display information about the kernel", mon_kerninfo },
	{ "backtrace", "backtrace the kernel", mon_backtrace },
	// yanginz: add commands
	{ "showmappings", "Display all of the physical page mappings", mon_showmappings },
	{ "permission", "Explicitly set, clear, or change the permissions of mappings", mon_permission },
	{ "dumpmem", "Dump the contents of a range of memory", mon_dumpmemory },
	// { "step", "single step from current location", mon_step },
};

/***** Implementations of basic kernel monitor commands *****/

int
mon_help(int argc, char **argv, struct Trapframe *tf)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(commands); i++)
		cprintf("%s - %s\n", commands[i].name, commands[i].desc);
	return 0;
}

int
mon_kerninfo(int argc, char **argv, struct Trapframe *tf)
{
	extern char _start[], entry[], etext[], edata[], end[];

	cprintf("Special kernel symbols:\n");
	cprintf("  _start                  %08x (phys)\n", _start);
	cprintf("  entry  %08x (virt)  %08x (phys)\n", entry, entry - KERNBASE);
	cprintf("  etext  %08x (virt)  %08x (phys)\n", etext, etext - KERNBASE);
	cprintf("  edata  %08x (virt)  %08x (phys)\n", edata, edata - KERNBASE);
	cprintf("  end    %08x (virt)  %08x (phys)\n", end, end - KERNBASE);
	cprintf("Kernel executable memory footprint: %dKB\n",
		ROUNDUP(end - entry, 1024) / 1024);
	return 0;
}

int
mon_backtrace(int argc, char **argv, struct Trapframe *tf)
{
	uint32_t * ebp = (uint32_t *)read_ebp();
	uint32_t eip;
	uint32_t arg0, arg1, arg2, arg3, arg4;
	uintptr_t addr;
	struct Eipdebuginfo info;

	cprintf("Stack backtrace:\n");

	//__asm __volatile("movl 4(%%ebp), %0" : "r" (eip));

	while(ebp != 0){
		eip = *(uint32_t *)(ebp + 1);
		addr = (uintptr_t)*(ebp + 1);
		arg0 = ebp[2];
		arg1 = ebp[3];
		arg2 = ebp[4];
		arg3 = ebp[5];
		arg4 = ebp[6];
		cprintf("ebp %08x eip %08x args %08x %08x %08x %08x %08x\n", ebp, eip, arg0, arg1, arg2, arg3, arg4);
		memset(&info, 0, sizeof(info));
		debuginfo_eip(addr, &info);
		ebp = (uint32_t *)(*(uint32_t *)ebp);
	}
	return 0;
}

//
// yangminz: commands added
//

int showmappings(uint32_t va1, uint32_t va2);
int mon_showmappings(int argc, char **argv, struct Trapframe *tf){
	uint32_t va1, va2;
	uint32_t va = 0;
	bool unexpected = false;

	if(argc != 3)
		unexpected = true;
	if(!(va1 = strtol(argv[1], NULL, 16)))
		unexpected = true;
	if(!(va2 = strtol(argv[2], NULL, 16)))
		unexpected = true;
	if( va1 != ROUNDUP(va1, PGSIZE) ||
		va2 != ROUNDUP(va2, PGSIZE) ||
		va1 > va2)
		unexpected = true;

	if(unexpected){
		cprintf("Not expected format! Usage\n");
		cprintf(" > showmappings 0xva_low 0xva_high\n");
		return 0;
	}

	showmappings(va1, va2);

	return 0;
}

extern pde_t *kern_pgdir;
pte_t * pgdir_walk(pde_t *pgdir, const void *va, int create);
int mon_permission(int argc, char **argv, struct Trapframe *tf){
	uint32_t va=0, perm=0;
	char type, flag;
	pte_t * pte;
	bool unexpected = false;

	if(argc != 4 || !(va = strtol(argv[1], NULL, 16)))
		unexpected = true;

	type = argv[2][0];
	if(va != ROUNDUP(va, PGSIZE) || !(type == 'c' || type == 's'))
		unexpected = true;

	flag = argv[3][0];
	switch(flag){
		case 'P': perm = PTE_P; break;
		case 'W': perm = PTE_W; break;
		case 'U': perm = PTE_U; break;
		default: unexpected = true; break;
	}
	
	if(unexpected)
	{
		cprintf("Not expected format! Usage\n");
		cprintf(" > permission 0xva [c|s :clear or set] [P|W|U]\n");
		return 0;
	}

	pte = pgdir_walk(kern_pgdir, (const void*)va, 1);
	cprintf("origin:  0x%08x\tP: %1d\tW: %1d\tU: %1d\n", va, *pte&PTE_P, *pte&PTE_W, *pte&PTE_U);

	if(type == 'c'){
		cprintf("clearing virtual addr 0x%08x permission\n", va);
		*pte = *pte & ~perm;
	}
	else{
		cprintf("setting virtual addr 0x%08x permission\n", va);
		*pte = *pte | perm;
	}

	cprintf("current: 0x%08x\tP: %1d\tW: %1d\tU: %1d\n", va, *pte&PTE_P, *pte&PTE_W, *pte&PTE_U);
	return 0;
}

int mon_dumpmemory(int argc, char **argv, struct Trapframe *tf){
	bool unexpected = false;
	uint32_t n = -1, i = 0, bias = KERNBASE/4;
	void ** addr = NULL;
	char type;

	type = argv[1][0];
	if(argc != 4 || !(addr = (void **)strtol(argv[2], 0, 16)))
		unexpected = true;
	n = strtol(argv[3], 0, 0);
	if(addr != ROUNDUP(addr, PGSIZE) ||
		!(type == 'p' || type == 'v') ||
		n <= 0)
		unexpected = true;

	if(unexpected){
		cprintf("Not expected format! Usage:\n");
		cprintf(" > dumpmem [p|v addr type] 0xaddr N\n");
	}

	if(type == 'p'){
		cprintf("!\n");
		for(i = bias; i < n + bias; i ++)
		cprintf("physical memory:0x%08x\tvalue:0x%08x\n", addr + i, addr[i]);
	}
	if(type == 'v'){
		for(i = 0; i < n; i ++)
		cprintf("virtual memory:0x%08x\tvalue:0x%08x\n", addr + i, addr[i]);
	}

	return 0;
}

/*
// lab3 single step
extern struct Env * curenv;
extern void env_run(struct Env *e);
int mon_step(int argc, char **argv, struct Trapframe *tf){
	if(argc != 1){
		cprintf("Not expected format! Usage\n");
		cprintf(" > step\n");
		return 0;
	}

	if(tf == NULL){
		cprintf("single step error!\n");
		return 0;
	}
	tf->tf_eflags |= FL_TF;
	cprintf("now eip at\t%08x\n", tf->tf_eip);
	env_run(curenv);

	return 0;
}
*/



/***** Kernel monitor command interpreter *****/

#define WHITESPACE "\t\r\n "
#define MAXARGS 16

static int
runcmd(char *buf, struct Trapframe *tf)
{
	int argc;
	char *argv[MAXARGS];
	int i;

	// Parse the command buffer into whitespace-separated arguments
	argc = 0;
	argv[argc] = 0;
	while (1) {
		// gobble whitespace
		while (*buf && strchr(WHITESPACE, *buf))
			*buf++ = 0;
		if (*buf == 0)
			break;

		// save and scan past next arg
		if (argc == MAXARGS-1) {
			cprintf("Too many arguments (max %d)\n", MAXARGS);
			return 0;
		}
		argv[argc++] = buf;
		while (*buf && !strchr(WHITESPACE, *buf))
			buf++;
	}
	argv[argc] = 0;

	// Lookup and invoke the command
	if (argc == 0)
		return 0;
	for (i = 0; i < ARRAY_SIZE(commands); i++) {
		if (strcmp(argv[0], commands[i].name) == 0)
			return commands[i].func(argc, argv, tf);
	}
	cprintf("Unknown command '%s'\n", argv[0]);
	return 0;
}

void
monitor(struct Trapframe *tf)
{
	char *buf;

	cprintf("Welcome to the JOS kernel monitor!\n");
	cprintf("Type 'help' for a list of commands.\n");
	cprintf("%C%s test!\n",0x0400, "Color");

	if (tf != NULL)
		print_trapframe(tf);

	while (1) {
		buf = readline("K> ");
		if (buf != NULL)
			if (runcmd(buf, tf) < 0)
				break;
	}
}
