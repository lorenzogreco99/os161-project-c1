/*
 * AUthor: G.Cabodi
 * Very simple implementation of sys__exit.
 * It just avoids crash/panic. Full process exit still TODO
 * Address space is released
 */

#include <types.h>
#include <kern/unistd.h>
#include <clock.h>
#include <copyinout.h>
#include <syscall.h>
#include <lib.h>
#include <proc.h>
#include <thread.h>
#include <addrspace.h>

/*
 * simple proc management system calls
 */
void
sys__exit(int status)
{
  /* get address space of current process and destroy */
  struct addrspace *as = proc_getas();	//gives back the address space of the current process
  as_destroy(as);	//frees all the structures associated to the address space
  /* thread exits. proc data structure will be lost */
  thread_exit();	//does not return, does context switch to another thread

  panic("thread_exit returned (should not happen)\n");
  (void) status; // TODO: status handling
}