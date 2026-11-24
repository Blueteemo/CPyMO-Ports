#include <stdint.h>

/* The Linux ARM EABI and the 32-bit Actions ION interface. */
enum {
    SYS_EXIT = 1,
    SYS_WRITE = 4,
    SYS_OPEN = 5,
    SYS_CLOSE = 6,
    SYS_IOCTL = 54,
    SYS_MUNMAP = 91,
    SYS_MMAP2 = 192,
    BUFFER_SIZE = 1280 * 720 * 3 / 2,
    DMA_SIZE = (BUFFER_SIZE + 4095) & ~4095,
};

struct allocation {
    uint32_t length, alignment, heap_mask, flags;
    int32_t handle;
};

struct buffer_fd {
    int32_t handle, fd;
};

struct physical_range {
    int32_t handle;
    uint32_t address, size;
};

struct custom_ioctl {
    uint32_t command;
    uint32_t argument;
};

#define ION_IOWR(nr, type) (0xc0004900u | (sizeof(type) << 16) | (nr))

_Static_assert(sizeof(void *) == 4, "32-bit ARM ABI required");
_Static_assert(sizeof(struct allocation) == 20, "ION allocation ABI");
_Static_assert(sizeof(struct buffer_fd) == 8, "ION mapping ABI");
_Static_assert(sizeof(struct physical_range) == 12, "Actions ION address ABI");

static long call6(long number, long a, long b, long c, long d, long e, long f)
{
    register long r0 __asm__("r0") = a;
    register long r1 __asm__("r1") = b;
    register long r2 __asm__("r2") = c;
    register long r3 __asm__("r3") = d;
    register long r4 __asm__("r4") = e;
    register long r5 __asm__("r5") = f;
    register long r7 __asm__("r7") = number;
    __asm__ volatile ("svc #0" : "+r" (r0) :
        "r" (r1), "r" (r2), "r" (r3), "r" (r4), "r" (r5), "r" (r7) : "cc", "memory");
    return r0;
}

static long call3(long number, long a, long b, long c)
{
    return call6(number, a, b, c, 0, 0, 0);
}

static void write_text(const char *message)
{
    unsigned length = 0;
    while (message[length])
        ++length;
    call3(SYS_WRITE, 1, (long)message, length);
}

static void report(const char *step, uint32_t value)
{
    static const char digits[] = "0123456789abcdef";
    char hexadecimal[] = "0x00000000\n";
    for (int i = 9; i >= 2; --i) {
        hexadecimal[i] = digits[value & 15];
        value >>= 4;
    }
    write_text(step);
    write_text(hexadecimal);
}

static int probe_ion(void)
{
    struct allocation allocation = { DMA_SIZE, 0, 1, 0, -1 };
    struct buffer_fd mapped = { -1, -1 };
    struct physical_range physical = { -1, 0, 0 };
    struct custom_ioctl query = { 0, (uint32_t)&physical };
    int status = 1;
    long ion_fd = call3(SYS_OPEN, (long)"/dev/ion", 2, 0);
    report("ion_open=", ion_fd);
    if (ion_fd < 0)
        return status;

    long result = call3(SYS_IOCTL, ion_fd, ION_IOWR(0, struct allocation), (long)&allocation);
    report("ion_alloc=", result);
    if (result < 0)
        goto close_ion;

    physical.handle = allocation.handle;
    result = call3(SYS_IOCTL, ion_fd, ION_IOWR(6, struct custom_ioctl), (long)&query);
    report("ion_phys=", result);
    if (result < 0)
        goto free_buffer;
    report("ion_dma_address=", physical.address);
    report("ion_dma_size=", physical.size);
    if (!physical.address || (physical.address & 4095) || physical.size < DMA_SIZE)
        goto free_buffer;

    mapped.handle = allocation.handle;
    result = call3(SYS_IOCTL, ion_fd, ION_IOWR(2, struct buffer_fd), (long)&mapped);
    report("ion_map_fd=", result < 0 ? result : mapped.fd);
    if (result < 0 || mapped.fd < 0)
        goto free_buffer;

    long address = call6(SYS_MMAP2, 0, DMA_SIZE, 3, 1, mapped.fd, 0);
    report("ion_mmap=", address);
    if ((uint32_t)address >= 0xfffff001u)
        goto close_mapping;

    volatile uint8_t *buffer = (volatile uint8_t *)address;
    buffer[0] = 0xa5;
    buffer[BUFFER_SIZE - 1] = 0x5a;
    if (buffer[0] == 0xa5 && buffer[BUFFER_SIZE - 1] == 0x5a) {
        write_text("ion_readback=pass\n");
        status = 0;
    } else {
        write_text("ion_readback=fail\n");
    }
    status |= call3(SYS_MUNMAP, address, DMA_SIZE, 0) < 0;

close_mapping:
    status |= call3(SYS_CLOSE, mapped.fd, 0, 0) < 0;
free_buffer:
    result = call3(SYS_IOCTL, ion_fd, ION_IOWR(1, int32_t), (long)&allocation.handle);
    report("ion_free=", result);
    status |= result < 0;
close_ion:
    status |= call3(SYS_CLOSE, ion_fd, 0, 0) < 0;
    return status;
}

/* This binary has no runtime loader and uses only the system calls above. */
__attribute__((noreturn)) void _start(void)
{
    write_text("rg35xx_ion_probe\n");
    int status = probe_ion();
    write_text(status ? "result=fail\n" : "result=pass\n");
    call3(SYS_EXIT, status, 0, 0);
    __builtin_unreachable();
}
