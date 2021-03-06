#include <param.h>
#include <x86.h>
#include <proto.h>
#include <proc.h>

#include <page.h>
#include <vm.h>

#include <conf.h>

/*
 * proc.c - 2010 fleurer
 * this file implies the initilization of proc[0] and the implementation
 * of fork(),
 *
 * */

struct proc proc0;

struct proc *proc[NPROC] = {NULL, };
struct proc *cu = NULL;

struct tss_desc tss;

extern void _hwint_ret();

/* ----------------------------------------------------------------- */

/*
 * find an empty proc slot, return the number as pid
 * return 0 on fail
 * */
int find_pid(){
    int nr;
    for(nr=0; nr<NPROC; nr++){
        if (proc[nr]==NULL){
            return nr;
        }
    }
    return 0;
}

/* Spawn a kernel thread.
 * This is not quite cool, but we have to do some initialization in
 * kernel's address space, the approach in linux0.11 is not quite
 * ease here for the fact that trap occured in the kernel space do
 * not refering the esp in TSS.
 *
 * returns a pointer to the newly borned proc, one page size(with the kernel stack).
 * */
struct proc* kspawn(void (*func)()){
    uint nr;
    int fd, n;
    struct file *fp;
    struct proc *p;

    nr = find_pid();
    if (nr == 0) {
        panic("no free pid");
    }

    p = (struct proc *) kmalloc(PAGE);
    if (p==NULL) {
        panic("no free page");
    }
    proc[nr] = p;
    p->p_stat = SSLEEP; // set SRUN later.
    p->p_pid  = nr;
    p->p_ppid = cu->p_pid;
    p->p_pgrp = cu->p_pgrp;
    p->p_flag = cu->p_flag;
    p->p_cpu  = cu->p_cpu;
    p->p_nice = cu->p_nice;
    p->p_pri  = PUSER;
    //
    p->p_euid = cu->p_euid;
    p->p_egid = cu->p_egid;
    p->p_ruid = cu->p_ruid;
    p->p_rgid = cu->p_rgid;
    // increase the reference count of inodes, and dup files
    if (cu->p_wdir != NULL) {
        p->p_wdir = cu->p_wdir;
        p->p_wdir->i_count++;
        p->p_iroot = cu->p_iroot;
        p->p_iroot->i_count++;
    }
    // dup the files, and fdflag
    for (fd=0; fd<NOFILE; fd++){
        fp = cu->p_ofile[fd];
        if (fp != NULL) {
            fp->f_count++;
            fp->f_ino->i_count++;
        }
        p->p_ofile[fd] = fp;
        p->p_fdflag[fd] = cu->p_fdflag[fd];
    }
    // signals
    p->p_sig = cu->p_sig;
    p->p_sigmask = cu->p_sigmask;
    for (n=0; n<NSIG; n++) {
        p->p_sigact[n] = cu->p_sigact[n];
    }
    // clone kernel's address space.
    vm_clone(&p->p_vm);
    p->p_contxt = cu->p_contxt;
    p->p_contxt.eip = (uint)func;
    p->p_contxt.esp = (uint)p+PAGE;
    p->p_stat = SRUN;
    return p;
}

/*
 * main part of sys_fork().
 * note that the fact that ALL process swtching occurs in kernel
 * space, hence fork() just returns to _hwint_ret(in entry.S.rb),
 * and initialize a kernel stack just as intrrupt occurs here.
 * */
int do_fork(struct trap *tf){
    struct proc *p;
    struct trap *ntf;

    p = kspawn(&_hwint_ret);
    ntf = (struct trap *)((uint)p+PAGE) - 1;
    *ntf = *tf;
    ntf->eax = 0; // this is why fork() returns 0.
    p->p_contxt.esp = (uint)ntf;
    p->p_trap = ntf;
    return p->p_pid;
}

/* ----------------------------------------------------------- */

/*
 * init proc[0]
 * set the LDT and th ONLY TSS into GDT
 * and make current as proc[0]
 */
void proc0_init(){
    int fd;

    struct proc *p = cu = proc[0] = &proc0;
    p->p_pid = 0;
    p->p_ppid = 0;
    p->p_stat = SRUN;
    p->p_flag = SLOAD;
    // on shedule
    p->p_cpu = 0;
    p->p_pri = 0;
    p->p_nice = 20;
    // on user
    p->p_euid = 0;
    p->p_egid = 0;
    // attach the page table
    p->p_vm.vm_pgd = pgd0;
    //
    p->p_wdir = NULL;
    p->p_iroot = NULL;
    // init tss
    tss.ss0  = KERN_DS;
    tss.esp0 = KSTACK0;
    for (fd=0; fd<NOFILE; fd++){
        p->p_ofile[fd] = NULL;
    }
}

/* --------------------------------------------------- */

void dump_proc(struct proc *p){
    printk("%s ", (p==cu)? "-":" " );
    printk("pid:%d pri:%d cpu:%d nice:%d stat:%d esp0:%x eip:%x \n", p->p_pid, p->p_pri, p->p_cpu, p->p_nice, p->p_stat, p->p_contxt.esp, p->p_contxt.eip);
}

void dump_procs(){
    int i;
    struct proc *p;
    for(i=0; i<NPROC; i++){
        p = proc[i];
        if(p){
            dump_proc(p);
        }
    }
}

