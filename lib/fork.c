// implement fork from user space

#include <inc/string.h>
#include <inc/lib.h>

// PTE_COW marks copy-on-write page table entries.
// It is one of the bits explicitly allocated to user processes (PTE_AVAIL).
#define PTE_COW		0x800

//
// Custom page fault handler - if faulting page is copy-on-write,
// map in our own private writable copy.
//
static void
pgfault(struct UTrapframe *utf)
{
	void *addr = (void *) utf->utf_fault_va;
	uint32_t err = utf->utf_err;
	int r;

	// Check that the faulting access was (1) a write, and (2) to a
	// copy-on-write page.  If not, panic.
	// Hint:
	//   Use the read-only page table mappings at uvpt
	//   (see <inc/memlayout.h>).

	// LAB 4: Your code here.    
    if (!((err & FEC_WR) && (uvpd[PDX(addr)] & PTE_P) && (uvpt[PGNUM(addr)] & PTE_P) && (uvpt[PGNUM(addr)] & PTE_COW)))
        panic("not copy-on-write");

    addr = ROUNDDOWN(addr, PGSIZE);
    if (sys_page_alloc(0, PFTEMP, PTE_W|PTE_U|PTE_P) < 0)
        panic("sys_page_alloc");
    memcpy(PFTEMP, addr, PGSIZE);
    if (sys_page_map(0, PFTEMP, 0, addr, PTE_W|PTE_U|PTE_P) < 0)
        panic("sys_page_map");
    if (sys_page_unmap(0, PFTEMP) < 0)
        panic("sys_page_unmap");
    return;
	//panic("pgfault not implemented");
}

//
// Map our virtual page pn (address pn*PGSIZE) into the target envid
// at the same virtual address.  If the page is writable or copy-on-write,
// the new mapping must be created copy-on-write, and then our mapping must be
// marked copy-on-write as well.  (Exercise: Why do we need to mark ours
// copy-on-write again if it was already copy-on-write at the beginning of
// this function?)
//
// Returns: 0 on success, < 0 on error.
// It is also OK to panic on error.
//
static int
duppage(envid_t envid, unsigned pn)
{
	int r;

	// LAB 4: Your code here.
    void *addr = (void*) (pn*PGSIZE);
    if ((uvpt[pn] & PTE_W) || (uvpt[pn] & PTE_COW)) {
        r = sys_page_map(0, addr, envid, addr, PTE_COW|PTE_U|PTE_P);
        if(r < 0)
            return r;
        r = sys_page_map(0, addr, 0, addr, PTE_COW|PTE_U|PTE_P);
        if(r < 0)
            return r;
    }
    else sys_page_map(0, addr, envid, addr, PTE_U|PTE_P);
    return 0;
    panic("duppage not implemented");
}

//
// User-level fork with copy-on-write.
// Set up our page fault handler appropriately.
// Create a child.
// Copy our address space and page fault handler setup to the child.
// Then mark the child as runnable and return.
//
// Returns: child's envid to the parent, 0 to the child, < 0 on error.
// It is also OK to panic on error.
//
// Hint:
//   Use uvpd, uvpt, and duppage.
//   Remember to fix "thisenv" in the child process.
//   Neither user exception stack should ever be marked copy-on-write,
//   so you must allocate a new page for the child's user exception stack.
//
envid_t
fork(void)
{
	// LAB 4: Your code here.
    set_pgfault_handler(pgfault);

    envid_t envid;
    uint32_t addr;
    envid = sys_exofork();
    if (envid == 0) {
        // panic("child");
        thisenv = &envs[ENVX(sys_getenvid())];
        return 0;
    }
    // cprintf("sys_exofork: %x\n", envid);
    if (envid < 0)
        panic("sys_exofork: %e", envid);

    for (addr = 0; addr < USTACKTOP; addr += PGSIZE)
        if ((uvpd[PDX(addr)] & PTE_P) && (uvpt[PGNUM(addr)] & PTE_P)
            && (uvpt[PGNUM(addr)] & PTE_U)) {
            duppage(envid, PGNUM(addr));
        }

    if (sys_page_alloc(envid, (void *)(UXSTACKTOP-PGSIZE), PTE_U|PTE_W|PTE_P) < 0)
        panic("1");
    extern void _pgfault_upcall();
    sys_env_set_pgfault_upcall(envid, _pgfault_upcall);

    if (sys_env_set_status(envid, ENV_RUNNABLE) < 0)
        panic("sys_env_set_status");

    return envid;
    // panic("fork not implemented");
}

// Challenge!
int
sfork(void)
{
	// LAB 4: Your code here.
    extern void _pgfault_upcall(void);
    envid_t myenvid = sys_getenvid();
    envid_t envid;
    uint32_t i, j, pn;
    int perm;

    // set page fault handler
    set_pgfault_handler(pgfault);

    // create a child
    if((envid = sys_exofork()) < 0)
    {
        return -1;
    }

    if(envid == 0)
    {
        thisenv = &envs[ENVX(sys_getenvid())];
        return envid;
    }

    // copy address space to child
    for (i = PDX(UTEXT); i < PDX(UXSTACKTOP); i++)
    {
        if(uvpd[i] & PTE_P)
        {
            for (j = 0; j < NPTENTRIES; j++)
            {
                pn = PGNUM(PGADDR(i, j, 0));
                if(pn == PGNUM(UXSTACKTOP - PGSIZE))
                {
                    break;
                }

                if(pn == PGNUM(USTACKTOP - PGSIZE))
                {
                     duppage(envid, pn); // cow for stack page
                     continue;
                }

                // map same page to child env with same perms
                if (uvpt[pn] & PTE_P)
                {
                    
                    perm = uvpt[pn] & ~(uvpt[pn] & ~(PTE_P |PTE_U | PTE_W | PTE_AVAIL));
                    if (sys_page_map(myenvid, (void *)(PGADDR(i, j, 0)),
                                     envid,   (void *)(PGADDR(i, j, 0)), perm) < 0)
                    {
                        return -1;
                    }

                }
            }
        }
    }

    // allocate new exception stack for child
    if(sys_page_alloc(envid, (void *)(UXSTACKTOP - PGSIZE), PTE_U | PTE_P | PTE_W) < 0)
    {
        return -1;
    }

    // map child uxstack to temp page
    if(sys_page_map(envid, (void *)(UXSTACKTOP - PGSIZE), myenvid, PFTEMP, PTE_U | PTE_P | PTE_W) < 0)
    {
        return -1;
    }

    // copy own uxstack to temp page
    memmove((void *)(UXSTACKTOP - PGSIZE), PFTEMP, PGSIZE);

    if(sys_page_unmap(myenvid, PFTEMP) < 0)
    {
        return -1;
    }

    // set page fault handler in child
    if(sys_env_set_pgfault_upcall(envid, _pgfault_upcall) < 0)
    {
        return -1;
    }

    // mark child env as RUNNABLE
    if(sys_env_set_status(envid, ENV_RUNNABLE) < 0)
    {
        return -1;
    }

    return envid;
}
