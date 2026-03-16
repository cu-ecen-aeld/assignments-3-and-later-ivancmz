# Assignment 7 - Kernel Oops Analysis

## Command that caused the oops

```
echo "hello_world" > /dev/faulty
```

## Kernel Oops Output

```
Unable to handle kernel NULL pointer dereference at virtual address 0000000000000000
Mem abort info:
  ESR = 0x0000000096000045
  EC = 0x25: DABT (current EL), IL = 32 bits
  SET = 0, FnV = 0
  EA = 0, S1PTW = 0
  FSC = 0x05: level 1 translation fault
Data abort info:
  ISV = 0, ISS = 0x00000045
  CM = 0, WnR = 1
user pgtable: 4k pages, 39-bit VAs, pgdp=0000000041bb2000
[0000000000000000] pgd=0000000000000000, p4d=0000000000000000, pud=0000000000000000
Internal error: Oops: 0000000096000045 [#1] SMP
Modules linked in: hello(O) faulty(O) scull(O)
CPU: 0 PID: 154 Comm: sh Tainted: G           O       6.1.44 #1
Hardware name: linux,dummy-virt (DT)
pstate: 80000005 (Nzcv daif -PAN -UAO -TCO -DIT -SSBS BTYPE=--)
pc : faulty_write+0x10/0x20 [faulty]
lr : vfs_write+0xc8/0x390
sp : ffffffc008dfbd20
x29: ffffffc008dfbd80 x28: ffffff8001aac240 x27: 0000000000000000
x26: 0000000000000000 x25: 0000000000000000 x24: 0000000000000000
x23: 000000000000000c x22: 000000000000000c x21: ffffffc008dfbdc0
x20: 00000055932e0a20 x19: ffffff8001b59100 x18: 0000000000000000
x17: 0000000000000000 x16: 0000000000000000 x15: 0000000000000000
x14: 0000000000000000 x13: 0000000000000000 x12: 0000000000000000
x11: 0000000000000000 x10: 0000000000000000 x9 : 0000000000000000
x8 : 0000000000000000 x7 : 0000000000000000 x6 : 0000000000000000
x5 : 0000000000000001 x4 : ffffffc000787000 x3 : ffffffc008dfbdc0
x2 : 000000000000000c x1 : 0000000000000000 x0 : 0000000000000000
Call trace:
 faulty_write+0x10/0x20 [faulty]
 ksys_write+0x74/0x110
 __arm64_sys_write+0x1c/0x30
 invoke_syscall+0x54/0x130
 el0_svc_common.constprop.0+0x44/0xf0
 do_el0_svc+0x2c/0xc0
 el0_svc+0x2c/0x90
 el0t_64_sync_handler+0xf4/0x120
 el0t_64_sync+0x18c/0x190
Code: d2800001 d2800000 d503233f d50323bf (b900003f) 
---[ end trace 0000000000000000 ]-
```

## Analysis

### Error type

The kernel reports a **NULL pointer dereference** at virtual address `0x0000000000000000`. This means a kernel module attempted to write to a NULL pointer, which is an invalid memory access that triggers a page fault the kernel cannot resolve.

### Identifying the faulty function

The most important line in the oops is:

```
pc : faulty_write+0x10/0x20 [faulty]
```

This tells us:

- **`pc`**: the program counter at the time of the crash.
- **`faulty_write`**: the function where the crash occurred.
- **`+0x10`**: the offset (in bytes) from the start of the function where the faulting instruction is located.
- **`/0x20`**: the total size of the function (32 bytes).
- **`[faulty]`**: the kernel module containing this function (`faulty.ko`).

### Call trace

The call trace shows how execution reached the faulting instruction:

1. **`el0t_64_sync`** — userspace triggers a synchronous exception (syscall) from EL0.
2. **`el0_svc`** → **`do_el0_svc`** → **`invoke_syscall`** — the kernel's syscall dispatch path.
3. **`__arm64_sys_write`** → **`ksys_write`** — the kernel handles the `write()` system call invoked by `echo`.
4. **`vfs_write`** — the VFS layer routes the write to the appropriate file operations.
5. **`faulty_write+0x10/0x20`** — the driver's `.write` handler is called, and it crashes here.

### Register and instruction analysis

- Register **x1 = 0x0000000000000000**: the destination address for the store is NULL.
- The faulting instruction encoded as `b900003f` disassembles to `str wzr, [x1]`, which attempts to store the value zero at the address held in x1 (NULL).

### Root cause

Looking at the source code in `misc-modules/faulty.c`, the `faulty_write` function intentionally dereferences a NULL pointer:

```c
ssize_t faulty_write(struct file *filp, const char __user *buf, size_t count, loff_t *pos)
{
    *(int *)0 = 0;
    return 0;
}
```

The expression `*(int *)0 = 0` writes to address zero, which is an unmapped address in the kernel's virtual address space, causing the NULL pointer dereference oops.

### How to use this information to locate bugs

When encountering a kernel oops in a real driver:

1. Look at the **`pc` line** to identify the function and module responsible.
2. Use the **offset** (`+0x10`) with `objdump -d faulty.ko` or `addr2line` to pinpoint the exact source line.
3. Follow the **call trace** to understand the execution context that led to the fault.
4. Examine **register values** to understand what data was being operated on at the time of the crash.
