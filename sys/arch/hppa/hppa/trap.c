/*	$OpenBSD: trap.c,v 1.4 1999/05/03 16:33:10 mickey Exp $	*/

/*
 * Copyright (c) 1998 Michael Shalayeff
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Michael Shalayeff.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#define INTRDEBUG

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/syscall.h>
#include <sys/ktrace.h>
#include <sys/proc.h>
#include <sys/user.h>
#include <sys/acct.h>
#include <sys/signal.h>

#include <vm/vm.h>
#include <uvm/uvm.h>

#include <machine/iomod.h>
#include <machine/cpufunc.h>
#include <machine/reg.h>
#include <machine/db_machdep.h>
#include <machine/autoconf.h>

#define	FAULT_TYPE(op)	(VM_PROT_READ|(inst_load(op) ? 0 : VM_PROT_WRITE))

const char *trap_type[] = {
	"invalid interrupt vector",
	"high priority machine check",
	"power failure",
	"recovery counter trap",
	"external interrupt",
	"low-priority machine check",
	"instruction TLB miss fault",
	"instruction protection trap",
	"Illegal instruction trap",
	"break instruction trap",
	"privileged operation trap",
	"privileged register trap",
	"overflow trap",
	"conditional trap",
	"assist exception trap",
	"data TLB miss fault",
	"ITLB non-access miss fault",
	"DTLB non-access miss fault",
	"data protection trap/unalligned data reference trap",
	"data break trap",
	"TLB dirty bit trap",
	"page reference trap",
	"assist emulation trap",
	"higher-privelege transfer trap",
	"lower-privilege transfer trap",
	"taken branch trap",
	"data access rights trap",
	"data protection ID trap",
	"unaligned data ref trap",
	"reserved",
	"reserved 2"
};
int trap_types = sizeof(trap_type)/sizeof(trap_type[0]);

u_int32_t sir;
int want_resched;

void pmap_hptdump __P((void));
void cpu_intr __P((u_int32_t t, struct trapframe *frame));
void syscall __P((struct trapframe *frame, int *args));

static __inline void
userret (struct proc *p, register_t pc, u_quad_t oticks)
{
	int sig;
	/* take pending signals */
	while ((sig = CURSIG(p)) != 0)
		postsig(sig);

	p->p_priority = p->p_usrpri;
	if (want_resched) {
		register int s;
		/*
		 * Since we are curproc, a clock interrupt could
		 * change our priority without changing run queues
		 * (the running process is not kept on a run queue).
		 * If this happened after we setrunqueue ourselves but
		 * before we switch()'ed, we might not be on the queue
		 * indicated by our priority.
		 */
		s = splstatclock();
		setrunqueue(p);
		p->p_stats->p_ru.ru_nivcsw++;
		mi_switch();
		splx(s);
		while ((sig = CURSIG(p)) != 0)
			postsig(sig);
	}

	/*
	 * If profiling, charge recent system time to the trapped pc.
	 */
	if (p->p_flag & P_PROFIL) {
		extern int psratio;

		addupc_task(p, pc, (int)(p->p_sticks - oticks) * psratio);
	}

	curpriority = p->p_priority;
}

void
trap(type, frame)
	int type;
	struct trapframe *frame;
{
	struct proc *p = curproc;
	register vm_offset_t va;
	register vm_map_t map;
	register pa_space_t space;
	u_int opcode, t;
	int ret;

	va = frame->tf_ior;
	space = (pa_space_t) frame->tf_isr;

	if (USERMODE(frame->tf_iioq_head)) {
		type |= T_USER;
		p->p_md.md_regs = frame;
	}
#ifdef DEBUG
	if ((type & ~T_USER) != T_INTERRUPT)
		printf("trap: %d, %s for %x:%x at %x:%x\n",
		       type, trap_type[type & ~T_USER], space, va,
		       frame->tf_iisq_head, frame->tf_iioq_head);
#endif
	switch (type) {
	case T_NONEXIST:
	case T_NONEXIST|T_USER:
		/* we are screwd up by the central scrutinizer */
		panic ("trap: zombie on the bridge!!!");
		break;

	case T_RECOVERY:
	case T_RECOVERY|T_USER:
		printf ("trap: handicapped");
		break;

	case T_INTERRUPT:
	case T_INTERRUPT|T_USER:
		mfctl(CR_EIRR, t);
		t &= frame->tf_eiem;
		/* ACK it now */
		/* hardcode intvl timer intr, to save for proc switching */
		if (t & INT_ITMR) {
			mtctl(INT_ITMR, CR_EIRR);
			/* we've got an interval timer interrupt */
			cpu_initclocks();
			hardclock(frame);
			t ^= INT_ITMR;
		}
		if (t)
			cpu_intr(t, frame);
		return;

#ifdef DIAGNOSTIC
	case T_IBREAK:
	case T_HPMC:
	case T_TLB_DIRTY:
		panic ("trap: impossible \'%s\' (%d)",
			trap_type[type & ~T_USER], type);
		break;
#endif

	case T_POWERFAIL:
		break;

	case T_LPMC:
		break;

	case T_PAGEREF:
		break;

	case T_DBREAK:
		if (kdb_trap (type, 0, frame))
			return;
		break;

	case T_EXCEPTION:	/* co-proc assist trap */
		break;

	case T_OVERFLOW:
	case T_CONDITION:
		break;

	case T_ILLEGAL:
	case T_PRIV_OP:
	case T_PRIV_REG:
	case T_HIGHERPL:
	case T_LOWERPL:
	case T_TAKENBR:
		break;

	case T_IPROT:	case T_IPROT | T_USER:
	case T_DPROT:	case T_DPROT | T_USER:
	case T_ITLBMISS:
	case T_ITLBMISS | T_USER:
	case T_ITLBMISSNA:
	case T_ITLBMISSNA | T_USER:
	case T_DTLBMISS:
	case T_DTLBMISS | T_USER:
	case T_DTLBMISSNA:
	case T_DTLBMISSNA | T_USER:
		va = trunc_page(va);
		opcode = frame->tf_iir;
		map = &p->p_vmspace->vm_map;

		ret = uvm_fault(map, va, FAULT_TYPE(opcode), FALSE);
		if (ret != KERN_SUCCESS)
			panic ("trap: uvm_fault(%p, %x, %d, %d): %d",
			       map, va, FAULT_TYPE(opcode), 0, ret);
		break;

#ifdef HP7100_CPU
	case T_DATACC:   case T_DATACC   | T_USER:
	case T_DATAPID:  case T_DATAPID  | T_USER:
	case T_DATALIGN: case T_DATALIGN | T_USER:
		if (0 /* T-chip */) {
			break;
		}
		/* FALLTHROUGH to unimplemented */
#endif
	case T_EMULATION:
	case T_EMULATION | T_USER:
	default:
		panic ("trap: unimplemented \'%s\' (%d)",
			trap_type[type & ~T_USER], type);
	}
}

void
child_return(p)
	struct proc *p;
{
	userret(p, p->p_md.md_regs->tf_iioq_head, 0);
#ifdef KTRACE
	if (KTRPOINT(p, KTR_SYSRET))
		ktrsysret(p->p_tracep, SYS_fork, 0, 0);
#endif
}

/*
 * call actual syscall routine
 * from the low-level syscall handler:
 * - all HPPA_FRAME_NARGS syscall's arguments supposed to be copied onto
 *   our stack, this wins compared to copyin just needed amount anyway
 * - register args are copied onto stack too
 */
void
syscall(frame, args)
	struct trapframe *frame;
	int *args;
{
	register struct proc *p;
	register const struct sysent *callp;
	int nsys, code, argsize, error;
	int rval[2];

	uvmexp.syscalls++;

	if (!USERMODE(frame->tf_iioq_head))
		panic("syscall");

	p = curproc;
	nsys = p->p_emul->e_nsysent;
	callp = p->p_emul->e_sysent;
	code = frame->tf_arg0;
	switch (code) {
	case SYS_syscall:
		code = frame->tf_arg1;
		args += 1;
		break;
	case SYS___syscall:
		if (callp != sysent)
			break;
		code = frame->tf_arg1; /* XXX or arg2? */
		args += 2;
	}

	if (code < 0 || code >= nsys)
		callp += p->p_emul->e_nosys;	/* bad syscall # */
	else
		callp += code;
	argsize = callp->sy_argsize;

#ifdef SYSCALL_DEBUG
	scdebug_call(p, code, args);
#endif
#ifdef KTRACE
	if (KTRPOINT(p, KTR_SYSCALL))
		ktrsyscall(p->p_tracep, code, argsize, args);
#endif

	rval[0] = 0;
	rval[1] = 0;
	switch (error = (*callp->sy_call)(p, args, rval)) {
	case 0:
		/* curproc may change iside the fork() */
		p = curproc;
		frame->tf_ret0 = rval[0];
		frame->tf_ret1 = rval[1];
		break;
	case ERESTART:
		frame->tf_iioq_head -= 4; /* right? XXX */
		break;
	case EJUSTRETURN:
		break;
	default:
		if (p->p_emul->e_errno)
			error = p->p_emul->e_errno[error];
		frame->tf_ret0 = error;
		break;
	}

#ifdef SYSCALL_DEBUG
	scdebug_ret(p, code, error, rval);
#endif
	userret(p, p->p_md.md_regs->tf_rp, 0);
#ifdef KTRACE
	if (KTRPOINT(p, KTR_SYSRET))
		ktrsysret(p->p_tracep, code, error, rval[0]);
#endif
}

/* all the interrupts, minus cpu clock, which is the last */
struct cpu_intr_vector {
	const char *name;
	int pri;
	int (*handler) __P((void *));
	void *arg;
} cpu_intr_vectors[CPU_NINTS - 1];

void *
cpu_intr_establish(pri, irq, handler, arg, name)
	int pri, irq;
	int (*handler) __P((void *));
	void *arg;
	const char *name;
{
	register struct cpu_intr_vector *p;

	/* don't allow to override any established vectors,
	   AND interval timer hard-bound one */
	if (irq >= (CPU_NINTS - 1) || cpu_intr_vectors[irq].handler)
		return NULL;

	p = &cpu_intr_vectors[irq];
	p->name = name;
	p->pri = pri;
	p->handler = handler;
	p->arg = arg;

	return p;
}

void
cpu_intr(t, frame)
	u_int32_t t;
	struct trapframe *frame;
{
	u_int32_t eirr;
	register struct cpu_intr_vector *p;
	register int bit;

	do {
		mfctl(CR_EIRR, eirr);
		eirr = (t | eirr) & frame->tf_eiem;
		bit = ffs(eirr) - 1;
		if (bit >= 0) {
			mtctl(1 << bit, CR_EIRR);
			eirr &= ~(1 << bit);
			/* ((struct iomod *)cpu_gethpa(0))->io_eir = 0; */
#ifdef INTRDEBUG
			printf ("cpu_intr: 0x%08x\n", (1 << bit));
#endif
			p = &cpu_intr_vectors[bit];
			if (p->handler) {
				register int s = splx(p->pri);
				if (!(p->handler)(p->arg))
#ifdef INTRDEBUG1
					panic ("%s: can't handle interrupt",
					       p->name);
#else
					printf ("%s: can't handle interrupt\n",
						p->name);
#endif
				splx(s);
			} else {
#ifdef INTRDEBUG
				panic  ("cpu_intr: stray interrupt %d", bit);
#else
				printf ("cpu_intr: stray interrupt %d\n", bit);
#endif
			}
		}
	} while (eirr);
}

