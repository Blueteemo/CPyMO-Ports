#ifndef GARLIC_AUDIO_SYNC_H
#define GARLIC_AUDIO_SYNC_H
/* Linux futex mutex: uncontended audio calls stay in userspace. Blocking
   waiters sleep rather than spin, without an additional runtime dependency. */
#include <unistd.h>
#include <sys/syscall.h>
#include <linux/futex.h>
/* syscall is hidden when an embedding test defines strict POSIX feature macros. */
extern long syscall(long number, ...);
static int garlic_audio_mutex;
static void garlic_audio_lock(void)
{
    int expected = 0;
    if (__atomic_compare_exchange_n(&garlic_audio_mutex, &expected, 1, 0,
            __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) return;
    while (__atomic_exchange_n(&garlic_audio_mutex, 2, __ATOMIC_ACQUIRE) != 0)
        syscall(SYS_futex, &garlic_audio_mutex, FUTEX_WAIT_PRIVATE, 2, NULL, NULL, 0);
}
static void garlic_audio_unlock(void)
{
    if (__atomic_exchange_n(&garlic_audio_mutex, 0, __ATOMIC_RELEASE) == 2)
        syscall(SYS_futex, &garlic_audio_mutex, FUTEX_WAKE_PRIVATE, 1, NULL, NULL, 0);
}
#endif
