#include <minix/mthread.h>
#include "global.h"
#include "proto.h"

#define MAIN_CTX	&(mainthread.m_context)
#define OLD_CTX		&(threads[old_thread].m_context);
#define CURRENT_CTX	&(threads[current_thread].m_context)
#define CURRENT_STATE	threads[current_thread].m_state
PRIVATE int yield_all;

/*===========================================================================*
 *				mthread_getcontext			     *
 *===========================================================================*/
PUBLIC int mthread_getcontext(ctx)
ucontext_t *ctx;
{
/* Retrieve this process' current state.*/

  /* We're not interested in FPU state nor signals, so ignore them. 
   * Coincidentally, this significantly speeds up performance.
   */
  ctx->uc_flags |= (UCF_IGNFPU | UCF_IGNSIGM);
  return getcontext(ctx);
}


/*===========================================================================*
 *				mthread_schedule			     *
 *===========================================================================*/
PUBLIC void mthread_schedule(void)
{
/* Pick a new thread to run and run it. In practice, this involves taking the 
 * first thread off the (FIFO) run queue and resuming that thread. 
 */

  int old_thread;
  ucontext_t *new_ctx, *old_ctx;

  mthread_init();	/* Make sure mthreads is initialized */

  old_thread = current_thread;

  if (mthread_queue_isempty(&run_queue)) {
	/* No runnable threads. Let main thread run. */

	/* We keep track whether we're running the program's 'main' thread or
	 * a spawned thread. In case we're already running the main thread and
	 * there are no runnable threads, we can't jump back to its context. 
	 * Instead, we simply return.
	 */
	if (running_main_thread) return;

	/* We're running the last runnable spawned thread. Return to main
	 * thread as there is no work left.
	 */
	running_main_thread = 1;
	current_thread = NO_THREAD;
  } else {
	current_thread = mthread_queue_remove(&run_queue);
	running_main_thread = 0;	/* Running thread after swap */
  }

  if (current_thread == NO_THREAD) 
  	new_ctx = MAIN_CTX;
  else
  	new_ctx = CURRENT_CTX;

  if (old_thread == NO_THREAD) 
	old_ctx = MAIN_CTX;
  else
	old_ctx = OLD_CTX;

  if (swapcontext(old_ctx, new_ctx) == -1) 
  	mthread_panic("Could not swap context");
}


/*===========================================================================*
 *				mthread_init_scheduler			     *
 *===========================================================================*/
PUBLIC void mthread_init_scheduler(void)
{
/* Initialize the scheduler */
  mthread_queue_init(&run_queue);
  yield_all = 0;

}


/*===========================================================================*
 *				mthread_suspend				     *
 *===========================================================================*/
PUBLIC void mthread_suspend(state)
mthread_state_t state;
{
/* Stop the current thread from running. There can be multiple reasons for
 * this; the process tries to lock a locked mutex (i.e., has to wait for it to
 * become unlocked), the process has to wait for a condition, the thread
 * volunteered to let another thread to run (i.e., it called yield and remains
 * runnable itself), or the thread is dead.
 */

  int continue_thread = 0;

  if (state == DEAD) mthread_panic("Shouldn't suspend with DEAD state");

  threads[current_thread].m_state = state;
  
  /* Save current thread's context */
  if (mthread_getcontext(CURRENT_CTX) != 0)
	mthread_panic("Couldn't save current thread's context");

  /* We return execution here with setcontext/swapcontext, but also when we
   * simply return from the getcontext call. If continue_thread is non-zero, we
   * are continuing the execution of this thread after a call from setcontext 
   * or swapcontext.
   */

  if(!continue_thread) {
  	continue_thread = 1;
	mthread_schedule(); /* Let other thread run. */
  }
}


/*===========================================================================*
 *				mthread_unsuspend			     *
 *===========================================================================*/
PUBLIC void mthread_unsuspend(thread)
mthread_thread_t thread; /* Thread to make runnable */
{
/* Mark the state of a thread runnable and add it to the run queue */

  if (!isokthreadid(thread)) mthread_panic("Invalid thread id\n");
  threads[thread].m_state = RUNNABLE;
  mthread_queue_add(&run_queue, thread);
}


/*===========================================================================*
 *				mthread_yield				     *
 *===========================================================================*/
PUBLIC int mthread_yield(void)
{
/* Defer further execution of the current thread and let another thread run. */

  mthread_init();	/* Make sure mthreads is initialized */

  if (mthread_queue_isempty(&run_queue)) {	/* No point in yielding. */
  	return(-1);
  } else if (current_thread == NO_THREAD) {
  	/* Can't yield this thread, but still give other threads a chance to
  	 * run.
  	 */
  	mthread_schedule();
  	return(-1);
  }

  mthread_queue_add(&run_queue, current_thread);
  mthread_suspend(RUNNABLE); /* We're still runnable, but we're just kind
			      *	enough to let someone else run.
			      */
  return(0);
}


/*===========================================================================*
 *				mthread_yield_all			     *
 *===========================================================================*/
PUBLIC void mthread_yield_all(void)
{
/* Yield until there are no more runnable threads left. Two threads calling
 * this function will lead to a deadlock.
 */

  mthread_init();	/* Make sure mthreads is initialized */

  if (yield_all) mthread_panic("Deadlock: two threads trying to yield_all");
  yield_all = 1;

  /* This works as follows. Thread A is running and threads B, C, and D are
   * runnable. As A is running, it is NOT on the run_queue (see
   * mthread_schedule). It calls mthread_yield and will be added to the run
   * queue, allowing B to run. B runs and suspends eventually, possibly still
   * in a runnable state. Then C and D run. Eventually A will run again (and is
   * thus not on the list). If B, C, and D are dead, waiting for a condition,
   * or waiting for a lock, they are not on the run queue either. At that
   * point A is the only runnable thread left.
   */
  while (!mthread_queue_isempty(&run_queue)) {
	(void) mthread_yield();
  }

  /* Done yielding all threads. */
  yield_all = 0;
}
