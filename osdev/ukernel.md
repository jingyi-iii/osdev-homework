# 微内核（Microkernel）架构改造路线图

> 将当前宏内核（Monolithic Kernel）逐步改造为微内核架构的详细步骤指南。

---

## 目录

1. [总览](#总览)
2. [第 0 步：修 Bug + 打地基](#第-0-步修-bug--打地基)
3. [第 1 步：能力系统（Capability）](#第-1-步能力系统capability)
4. [第 2 步：IPC 增强](#第-2-步ipc-增强)
5. [第 3 步：内存映射原语](#第-3-步内存映射原语)
6. [第 4 步：IRQ 转发到用户态](#第-4-步irq-转发到用户态)
7. [第 5 步：搬移驱动到用户态](#第-5-步搬移驱动到用户态)
8. [第 6 步：Demo 程序独立运行](#第-6-步demo-程序独立运行)
9. [工作量估算](#工作量估算)
10. [重要提醒](#重要提醒)

---

## 总览

### 架构对比

```
当前（宏内核）                              目标（微内核）
┌──────────────────────────┐         ┌─────────────────┐
│  Kernel Space            │         │  µKernel Space   │
│  ┌────────────────────┐  │         │  ┌─────────────┐ │
│  │ process / thread   │  │         │  │ scheduler   │ │
│  │ VMM / PMM / heap   │  │         │  │ IPC (mailbox)│ │
│  │ bus / driver model │  │         │  │ VMM / PMM    │ │
│  │ IRQ / syscall      │  │         │  │ IRQ dispatch │ │
│  │ mailbox (IPC)      │  │         │  │ capability   │ │
│  │ demo games         │  │         │  └─────────────┘ │
│  └────────────────────┘  │         └─────────────────┘
└──────────────────────────┘         ┌─────────────────┐
                                     │  User Servers    │
                                     │  ┌─────────────┐ │
                                     │  │ kb_server   │ │
                                     │  │ vga_server  │ │
                                     │  │ rtc_server  │ │
                                     │  │ pci_server  │ │
                                     │  └─────────────┘ │
                                     │  ┌─────────────┐ │
                                     │  │ airplane    │ │
                                     │  │ snake       │ │
                                     │  └─────────────┘ │
                                     └─────────────────┘
```

### 核心原则

**只有需要 CPU 特权级的代码留在内核，其他全部搬到用户态。**

留在内核的组件：

| 组件 | 留在内核的理由 |
|------|---------------|
| 线程调度器 | 需要操作 TSS、LDT、CR3 等特权寄存器 |
| 物理内存管理（PMM） | 需要直接操作页表 |
| 虚拟内存管理（VMM） | 需要操作 CR3、页目录/页表 |
| IRQ 顶层分发 | 需要操作 IDT、PIC/APIC |
| IPC 核心（mailbox） | 需要跨地址空间传输数据 |
| 能力系统（capability） | 访问控制的信任根必须在内核 |

搬出内核的组件：

| 组件 | 新位置 | 通信方式 |
|------|--------|---------|
| 键盘驱动 | `servers/kb_server` | IRQ → IPC 转发 |
| 终端/VGA 驱动 | `servers/vga_server` | 共享 framebuffer + IPC 命令 |
| RTC 驱动 | `servers/rtc_server` | IPC |
| 平台总线/设备枚举 | `servers/pci_server` | IPC |
| Demo 游戏 | `demo/`（独立进程） | 共享 framebuffer + IPC 输入 |

---

## 第 0 步：修 Bug + 打地基

> **在做架构改动前，先把现有问题修好，否则 bug 会随着复杂度增加而放大。**

### 0.1 修复 `split_4mb_pde` 的静态全局变量 bug

**位置**：`arch/i386/paging.c`

**问题**：`split_4mb_pde` 函数操作的是静态全局 `pdes` 数组，而不是传入的 `cr3` 参数。当在用户进程的私有页目录中拆分 4MB 大页时，会错误地修改内核的主页目录。

**修复方向**：将 `split_4mb_pde` 改为接受 `uint32_t *pde_base` 参数，使用传入的页目录基址而非静态全局变量。

```c
// 修复前（有问题）
static void split_4mb_pde(uint32_t vaddr, uint32_t cr3) {
    uint32_t *pde = &pdes[PDE_INDEX(vaddr)];  // BUG: 用的是静态全局 pdes
    // ...
}

// 修复后
static void split_4mb_pde(uint32_t vaddr, uint32_t *pde_base) {
    uint32_t *pde = &pde_base[PDE_INDEX(vaddr)];  // 正确：用传入的页目录
    // ...
}
```

### 0.2 修复 unsafe list 遍历删除

**位置**：`kernel/irq.c`（`irqline_release` 函数）

**问题**：在 `list_for_each` 遍历中执行 `list_del`，这是经典的链表损坏模式。

**修复方向**：在 `include/lib/list.h` 中添加 `list_for_each_safe` 宏，然后修正 `irqline_release`。

```c
// include/lib/list.h —— 新增宏
#define list_for_each_safe(pos, n, head) \
    for (pos = (head)->next, n = pos->next; pos != (head); \
         pos = n, n = pos->next)
```

### 0.3 改善异常处理器

**位置**：`arch/i386/irq.S`

**问题**：`DECLARE_EXCEPTION` 宏生成的异常处理桩全部执行 `hlt`，没有寄存器 dump 或栈回溯信息，调试体验极差。

**修复方向**：至少为常见异常（#PF, #GP, #UD 等）添加以下功能：

- 保存并打印全部通用寄存器值
- 打印错误码（error code）
- 打印 `CR2`（页错误地址，针对 #PF）
- 打印调用栈回溯（stack trace）
- 打印当前进程/线程信息

### 0.4 现有代码中值得清理的小问题

| 问题 | 位置 | 优先级 |
|------|------|--------|
| `init.h` 包含了不必要的重型头文件 | `include/kernel/init.h` | 低 |
| `arch_map_4kb_range` 日志信息写成了 `arch_map_4mb_range` | `arch/i386/paging.c` | 低 |
| `mailhander` 拼写错误（应为 `mailhandler`） | `include/kernel/mailbox.h` | 低 |
| `kmalloc` 无对齐保证 | `kernel/mm/heap.c` | 低 |

---

## 第 1 步：能力系统（Capability）

> **这是最关键的基础设施。如果你先把驱动搬出内核但没有任何访问控制，任何用户进程都能直接操作硬件寄存器——比宏内核还不安全。**

### 1.1 设计理念

能力（Capability）是一个不可伪造的令牌，持有它意味着拥有执行某项特权操作的权限。微内核在进程创建时授予最小权限，进程只能做被明确允许的事情。

### 1.2 定义能力类型

创建新文件 `include/kernel/capability.h`：

```c
#ifndef _KERNEL_CAPABILITY_H_
#define _KERNEL_CAPABILITY_H_

#include <stdint.h>
#include "lib/list.h"

typedef enum {
    CAP_IRQ_OWN,         // 拥有某个 IRQ 线
    CAP_MEM_MAP,         // 映射某段物理内存
    CAP_IO_PORT,         // 访问某个 I/O 端口范围
    CAP_IPC_SEND,        // 向某个 mailbox 发送消息
    CAP_IPC_RECV,        // 从某个 mailbox 接收消息
    CAP_PROC_CREATE,     // 创建新进程
    CAP_THREAD_CREATE,   // 创建新线程
} cap_type_t;

typedef struct capability {
    list_node_t  node;          // 链入进程的能力列表
    cap_type_t   type;
    union {
        // CAP_IRQ_OWN
        uint8_t   irq;
        // CAP_MEM_MAP
        struct {
            uint32_t phys_base;
            uint32_t size;
            uint32_t flags;     // MAP_READ | MAP_WRITE | MAP_IO
        } mem;
        // CAP_IO_PORT
        struct {
            uint16_t port_base;
            uint16_t port_count;
        } io;
        // CAP_IPC_SEND / CAP_IPC_RECV
        uint64_t  mailbox_id;
    };
} capability_t;

// 能力检查函数
int  cap_check(struct pcb *proc, cap_type_t type, const void *arg);
int  cap_grant(struct pcb *proc, cap_type_t type, const void *arg);
void cap_revoke(struct pcb *proc, cap_type_t type, const void *arg);
void cap_revoke_all(struct pcb *proc);

#endif
```

### 1.3 给进程附加能力列表

修改 `include/kernel/process.h` 中的 PCB 结构：

```c
typedef struct pcb {
    // ... 现有字段 ...
    list_node_t   capabilities;   // 该进程拥有的所有 capability 链表
    spinlock_t   *cap_lock;       // 保护 capability 列表的锁
} pcb;
```

### 1.4 能力检查层

所有涉及特权操作的 syscall 都需要加入能力检查：

```c
// kernel/process.c —— 示例：IRQ 注册的 syscall handler
int sys_irq_register(uint8_t irq, irq_handler_t handler) {
    struct pcb *proc = current_thread()->parent;

    // 检查当前进程是否有 CAP_IRQ_OWN 能力
    if (!cap_check(proc, CAP_IRQ_OWN, &irq))
        return -EPERM;

    return irqline_register(irq, handler);
}

// 示例：内存映射的权限检查
void* sys_mem_map(uint32_t phys_addr, size_t size, uint32_t flags) {
    struct pcb *proc = current_thread()->parent;

    // 检查进程是否有对应物理内存范围的 CAP_MEM_MAP 能力
    struct { uint32_t base; uint32_t size; uint32_t flags; } arg = {
        .base = phys_addr, .size = size, .flags = flags
    };
    if (!cap_check(proc, CAP_MEM_MAP, &arg))
        return (void*)-EPERM;

    return vmm_map_physical(proc, phys_addr, size, flags);
}
```

### 1.5 启动时授权

在 `kernel/init.c` 的 init 过程中，为关键服务进程授予必要的能力：

```c
void init_thread(void) {
    // 创建键盘服务进程
    pcb_t *kb_proc = p_create("kb_server", PRIORITY_HIGH);
    cap_grant(kb_proc, CAP_IO_PORT, &(cap_io_t){.port_base = 0x60, .port_count = 8});
    cap_grant(kb_proc, CAP_IRQ_OWN,  &(uint8_t){1});

    // 创建 VGA 服务进程
    pcb_t *vga_proc = p_create("vga_server", PRIORITY_HIGH);
    cap_grant(vga_proc, CAP_MEM_MAP, &(cap_mem_t){
        .phys_base = 0xA0000, .size = 0x10000, .flags = MAP_READ | MAP_WRITE
    });
    cap_grant(vga_proc, CAP_IO_PORT, &(cap_io_t){
        .port_base = 0x3C0, .port_count = 0x20
    });

    // 创建 RTC 服务进程
    pcb_t *rtc_proc = p_create("rtc_server", PRIORITY_NORMAL);
    cap_grant(rtc_proc, CAP_IO_PORT, &(cap_io_t){.port_base = 0x70, .port_count = 2});
    cap_grant(rtc_proc, CAP_IRQ_OWN,  &(uint8_t){8});

    // 创建游戏进程
    pcb_t *game_proc = p_create("airplane", PRIORITY_LOW);
    // 游戏进程不直接拥有硬件能力，通过 VGA server 获取共享 framebuffer
    // 通过 kb_server 获取键盘事件
}
```

---

## 第 2 步：IPC 增强

> **当前 mailbox 偏向"通知"模式，微内核需要的是完整的进程间数据传输和同步 RPC。**

### 2.1 增强 mail 为消息载体

修改 `include/kernel/mailbox.h`，扩展 mail 结构：

```c
typedef struct mail {
    uint64_t    id;
    uint32_t    sender_pid;
    uint32_t    sender_tid;
    uint32_t    msg_type;         // 新增：消息类型（便于分发）
    int32_t     error_code;       // 新增：响应错误码（0 = 成功）

    // 数据传输
    void       *payload;          // 指向数据缓冲区
    size_t      payload_size;     // 数据大小

    // reply 通道（用于同步 RPC）
    uint64_t    reply_mailbox;    // 回复到此 mailbox（0 = 无回复）

    // 引用计数
    atomic_t    ref_count;        // 改用原子类型
    list_node_t node;
} mail_t;
```

### 2.2 实现同步 RPC（call + reply）

```c
// include/kernel/mailbox.h —— 新增 API
int mailbox_call(uint64_t target_mbox, mail_t *req, mail_t **resp, uint32_t timeout_ms);
```

```c
// kernel/ipc/mailbox.c —— 实现
int mailbox_call(uint64_t target_mbox, mail_t *req, mail_t **resp, uint32_t timeout_ms) {
    // 1. 创建一个临时 reply mailbox（仅调用者可见）
    uint64_t reply_mbox = mailbox_create(current_thread()->parent->pid, 0);

    // 2. 在请求中附加 reply mailbox id
    req->reply_mailbox = reply_mbox;

    // 3. 发送请求
    int ret = send_mail(target_mbox, req);
    if (ret != 0) {
        mailbox_destroy(reply_mbox);
        return ret;
    }

    // 4. 阻塞等待响应（使用 wait queue）
    mail_t *reply = mailbox_listen(reply_mbox, timeout_ms);

    // 5. 清理并返回
    mailbox_destroy(reply_mbox);
    if (reply == NULL)
        return -ETIMEDOUT;

    *resp = reply;
    return reply->error_code;
}
```

### 2.3 用 wait queue 替代忙等待

创建新文件 `include/kernel/wait.h`：

```c
#ifndef _KERNEL_WAIT_H_
#define _KERNEL_WAIT_H_

#include "lib/list.h"
#include "sync/spinlock.h"

typedef struct wait_queue {
    list_node_t  waiters;     // 阻塞在此的线程链表
    spinlock_t  *lock;
} wait_queue_t;

// 初始化 wait queue
void wait_queue_init(wait_queue_t *wq);

// 将当前线程放入等待队列并挂起
void wait_queue_sleep(wait_queue_t *wq);

// 唤醒等待队列中的一个线程
void wait_queue_wake_one(wait_queue_t *wq);

// 唤醒等待队列中的所有线程
void wait_queue_wake_all(wait_queue_t *wq);

#endif
```

配合修改 `include/kernel/process.h` 中的 TCB：

```c
typedef struct tcb {
    // ... 现有字段 ...
    wait_queue_t *waiting_on;   // 当前正在等待的队列（NULL = 不在等待）
    list_node_t   wait_node;    // 链入 wait_queue 的节点
} tcb;
```

### 2.4 改进 mailbox_listen

```c
// 用 wait queue 替换 thread_yield 忙等待
mail_t* mailbox_listen(uint64_t mbox_id, uint32_t timeout_ms) {
    mailbox_t *mbox = mailbox_find(mbox_id);
    if (!mbox) return NULL;

    mail_t *mail = NULL;
    spinlock_lock(mbox->lock);

    while (list_empty(&mbox->queue)) {
        // 将当前线程放入等待队列
        wait_queue_sleep(&mbox->wait_queue);
        spinlock_unlock(mbox->lock);

        // 主动让出 CPU
        thread_yield();

        // 被唤醒后重新获取锁
        spinlock_lock(mbox->lock);

        // 检查超时
        if (timeout_expired()) {
            spinlock_unlock(mbox->lock);
            return NULL;
        }
    }

    // 取出消息
    mail = list_first_entry(&mbox->queue, mail_t, node);
    list_del(&mail->node);
    spinlock_unlock(mbox->lock);
    return mail;
}
```

---

## 第 3 步：内存映射原语

> **驱动在用户态后，需要能映射 MMIO 区域和 DMA 缓冲区。现有的 VMM 系统（红黑树管理虚拟区域）是很好的基础。**

### 3.1 新增 syscall

```c
// 映射物理内存到当前进程的虚拟地址空间
// 返回映射后的虚拟地址
void* sys_mem_map(uint32_t phys_addr, size_t size, uint32_t flags);
// flags 位定义：
//   MAP_READ   (1 << 0)  —— 可读
//   MAP_WRITE  (1 << 1)  —— 可写
//   MAP_IO     (1 << 2)  —— MMIO 区域（禁用缓存）
//   MAP_USER   (1 << 3)  —— 用户态可访问

// 解除映射
int sys_mem_unmap(void *vaddr, size_t size);
```

### 3.2 实现要点

```c
void* sys_mem_map(uint32_t phys_addr, size_t size, uint32_t flags) {
    struct pcb *proc = current_thread()->parent;

    // 1. 能力检查：进程必须持有对应物理地址范围的 CAP_MEM_MAP
    cap_mem_t cap_mem = {.phys_base = phys_addr, .size = size, .flags = flags};
    if (!cap_check(proc, CAP_MEM_MAP, &cap_mem))
        return (void*)(intptr_t)(-EPERM);

    // 2. 页对齐
    uint32_t aligned_pa = ALIGN_DOWN(phys_addr, PAGE_SIZE);
    uint32_t offset     = phys_addr - aligned_pa;
    size_t   aligned_sz = ALIGN_UP(size + offset, PAGE_SIZE);

    // 3. 在进程的 VMM 空间中分配虚拟地址区域
    void *vaddr = vmm_alloc_region(proc, aligned_sz);
    if (!vaddr)
        return (void*)(intptr_t)(-ENOMEM);

    // 4. 逐页建立映射
    uint32_t pg_flags = PAGE_PRESENT | PAGE_RW;  // 内核有 RW 权限
    if (flags & MAP_USER)  pg_flags |= PAGE_USER;
    if (flags & MAP_IO)    pg_flags |= PAGE_PCD | PAGE_PWT;  // 禁用缓存

    for (size_t i = 0; i < aligned_sz; i += PAGE_SIZE) {
        arch_map_4kb((uint32_t)vaddr + i, aligned_pa + i, pg_flags, proc->pde);
    }

    // 5. 刷新 TLB
    arch_tlb_flush();

    // 6. 返回用户可用的虚拟地址（带偏移）
    return (void*)((uint8_t*)vaddr + offset);
}
```

### 3.3 共享内存支持（两个进程映射同一物理页）

```c
// 进程 A 将一段内存共享给进程 B
int sys_mem_share(uint32_t target_pid, void *local_vaddr, size_t size) {
    struct pcb *proc_a = current_thread()->parent;
    struct pcb *proc_b = pcb_find(target_pid);
    if (!proc_b) return -ESRCH;

    // 1. 查找 local_vaddr 对应的物理地址
    uint32_t phys_addr = vmm_virt_to_phys(proc_a, local_vaddr);
    if (phys_addr == 0) return -EINVAL;

    // 2. 为进程 B 创建新的虚拟映射到同一物理地址
    void *remote_vaddr = vmm_alloc_region(proc_b, size);
    for (size_t i = 0; i < size; i += PAGE_SIZE) {
        arch_map_4kb((uint32_t)remote_vaddr + i, phys_addr + i,
                     PAGE_PRESENT | PAGE_RW | PAGE_USER, proc_b->pde);
    }

    // 3. 授予进程 B 访问此内存的能力
    cap_grant(proc_b, CAP_MEM_MAP, &(cap_mem_t){
        .phys_base = phys_addr, .size = size,
        .flags = MAP_READ | MAP_WRITE
    });

    return 0;
}
```

---

## 第 4 步：IRQ 转发到用户态

> **当前 IRQ handler 在内核态直接执行驱动逻辑。微内核需要把硬件中断转化为 IPC 消息发给用户态驱动进程。**

### 4.1 修改内核 IRQ handler 为消息转发

```c
// kernel/irq.c —— 新的 IRQ 分发逻辑
typedef struct irqline {
    // ... 现有字段 ...
    uint64_t    owner_mailbox;    // 新增：拥有此 IRQ 的进程的 mailbox
    struct pcb *owner_process;    // 新增：拥有此 IRQ 的进程
} irqline_t;

// 中断处理入口（内核态，ISR 上下文）
static void irqline_dispatch(irqline_t *line) {
    if (line->owner_mailbox == 0) {
        // 没有用户态拥有者：内核内部处理（如调度 tick）
        return;
    }

    // 构造 IRQ 通知消息
    irq_notify_t notify = {
        .irq_num     = line->irq_num,
        .count       = atomic_fetch_add(&line->irq_count, 1),
        .timestamp   = timer_get_ticks(),
    };

    // 创建 mail 并发送
    mail_t *mail = mail_create(sizeof(irq_notify_t));
    if (!mail) return;

    memcpy(mail->payload, &notify, sizeof(irq_notify_t));
    mail->msg_type = MSG_IRQ_NOTIFY;

    // 非阻塞发送：如果 mailbox 满则丢弃（IRQ 上下文中不能阻塞）
    mailbox_try_send(line->owner_mailbox, mail);
}
```

### 4.2 用户态驱动等待 IRQ

```c
// servers/kb_server.c —— 用户态键盘驱动的主循环
void kb_server_main(void) {
    // 1. 创建自己的 mailbox
    uint64_t my_mbox = sys_mailbox_create(MAILBOX_FLAG_IRQ);

    // 2. 向内核注册 IRQ1 的所有权
    sys_irq_register(1, my_mbox);

    KLOG("kb_server: ready, listening on IRQ 1\n");

    // 3. 主事件循环
    while (1) {
        mail_t *msg = sys_mailbox_listen(my_mbox, 0);  // 永久阻塞等待

        if (msg->msg_type == MSG_IRQ_NOTIFY) {
            // 收到 IRQ 通知：读取 PS/2 数据端口
            uint8_t scancode = sys_inb(0x60);

            // 处理扫描码，构造键盘事件
            key_event_t event = process_scancode(scancode);
            if (event.valid) {
                // 转发给关注键盘事件的进程
                mail_t *key_mail = mail_create(sizeof(key_event_t));
                memcpy(key_mail->payload, &event, sizeof(key_event_t));
                key_mail->msg_type = MSG_KEY_EVENT;
                sys_mailbox_broadcast(KB_EVENT_MAILBOX, key_mail);
            }
        }

        mail_release(msg);
    }
}
```

### 4.3 保留在内核的中断

| IRQ | 用途 | 为什么留在内核 |
|-----|------|---------------|
| IRQ0 | PIT 调度时钟 | 线程调度需要内核态执行 |
| IRQ2 | 级联 | PIC 硬件要求 |

---

## 第 5 步：搬移驱动到用户态

> **基础设施齐全后，逐个将驱动从内核空间迁移到用户态服务进程。**
### 5.0 复用 bus-driver-device 架构（推荐做法）

> **核心思路：bus-driver-device 模型解决的是"绑定 + 生命周期"（probe/remove），
> 而不是"驱动代码跑在哪"。** 内核驱动与用户态驱动的差别只在 `probe` 的实现：
> 内核驱动直接调 `drv->probe(dev)`；用户态驱动则"拉起一个 ring3 的 server 线程 +
> 把 device 的资源翻译成 capability 授予它"，真正的硬件初始化（`irq_request`、
> `mailbox_listen`、`sys_inb`）由那个 server 在用户态自己完成。
>
> 设备侧**完全不用改**：`platform_devices_init()` 照旧注册 device，`platform_bus` 的
> `match` 照旧按 type 匹配。只需要给 `struct driver` 加几个字段 + `bus.c` 里分支一下。

#### 5.0.1 扩展 `struct driver`（`include/kernel/driver.h`）

```c
enum drv_class {
    DRV_CLASS_KERNEL = 0,   /* probe/remove 是内核函数指针（现状） */
    DRV_CLASS_USER   = 1,   /* probe 时拉起用户态 server 线程      */
};

struct driver {
    const char *type;
    list_node drv_node;
    void* ops;

    int           class;      /* DRV_CLASS_KERNEL / DRV_CLASS_USER */
    task_entry_t  entry;      /* user driver 的 ring3 入口          */
    int32_t       pid;        /* probe 后记录的 server 进程 pid     */
    int32_t       tid;        /* 对应线程 tid                      */

    int (*probe)(struct device *dev);   /* kernel driver 用 */
    int (*remove)(struct device *dev);
};
```

#### 5.0.2 `kernel/bus.c` 按 driver class 分支

```c
static int try_bind_and_probe(struct bus *bus, struct driver *drv, struct device *dev)
{
    if (!drv || !dev)
        return E_INVAL;
    if (dev->driver != NULL)
        return E_BUSY;
    if (!driver_matches(bus, drv, dev))
        return E_DRV_NOTFOUND;

    dev->driver = drv;

    int ret;
    if (drv->class == DRV_CLASS_USER)
        ret = user_driver_start(bus, drv, dev);   /* 拉起 server + 授能力 */
    else if (!drv->probe)
        ret = E_DRV_PROBE;
    else
        ret = drv->probe(dev);

    if (ret != 0)
        dev->driver = NULL;
    return ret;
}

static int unbind_driver_from_device(struct driver *drv, struct device *dev)
{
    if (!drv || !dev || dev->driver != drv)
        return E_INVAL;

    if (drv->class == DRV_CLASS_USER)
        user_driver_stop(drv);          /* thread_exit + cap_revoke_all */
    else if (drv->remove)
        drv->remove(dev);

    dev->driver = NULL;
    return 0;
}
```

#### 5.0.3 `user_driver_start`：把设备资源翻译成能力

```c
static int user_driver_start(struct bus *bus, struct driver *drv, struct device *dev)
{
    /* 1. 拉起 ring3 server（入口是 drv->entry） */
    int32_t pid = proc_create(PROC_PRIV_USER, drv->entry, 0);
    if (pid < 0)
        return E_DRV_PROBE;

    pcb* proc = get_process_by_pid(pid);
    if (!proc) {
        proc_exit(pid);
        return E_DRV_PROBE;
    }

    /* 2. 按设备资源逐条授予能力（platform_device 的 dev 是首成员，可直接强转） */
    struct platform_device* pdev = to_platform_device(dev);
    for (int i = 0; i < pdev->num_res; i++) {
        struct platform_resource* res = &pdev->resources[i];
        switch (res->type) {
        case PLAT_RES_IRQ:
            cap_grant(proc, CAP_IRQ_OWN, &res->irq.major);
            break;
        case PLAT_RES_IO:
            cap_grant(proc, CAP_IO_ACCESS, &(cap_io_port){
                .base = res->io.base, .count = res->io.size });
            break;
        case PLAT_RES_MEM:
            cap_grant(proc, CAP_MEM_MAP, &(cap_mem){
                .base = res->mem.addr, .size = res->mem.size,
                .flags = MAP_READ | MAP_WRITE });
            break;
        }
    }

    /* 3. 记录 pid/tid，便于 remove 时回收
     *    （tid = 进程主线程；目前 proc_create 不直接返回，可遍历 pcb->tcbs
     *      或给 proc_create 加个 out 参数返回主线程 tid） */
    drv->pid = pid;
    drv->tid = /* 主线程 tid */;

    return 0;   /* 绑定成功：真正的硬件初始化由 server 在 ring3 自己完成 */
}

static void user_driver_stop(struct driver *drv)
{
    if (drv->tid > 0)
        thread_exit(drv->tid);
    if (drv->pid > 0) {
        pcb* proc = get_process_by_pid(drv->pid);
        if (proc)
            cap_revoke_all(proc);
    }
    drv->pid = drv->tid = -1;
}
```

#### 5.0.4 注册一个用户态驱动（以 kb_server 为例）

```c
static struct driver kb_user_driver = {
    .type   = "keyboard",       /* 与 platform 设备表里的 type 一致 */
    .class  = DRV_CLASS_USER,
    .entry  = kb_server_main,   /* ring3 入口 */
};

int kb_user_driver_init(void)
{
    return platform_driver_register(&kb_user_driver);
}
```

`kb_server_main()`（用户态）相对现有 `drivers/test/kb_server.c` 的改动：

| 现在（宏内核写法） | 微内核写法 |
|---|---|
| `arch_inb(0x60)` 直接读端口 | `sys_inb(0x60)`（走 syscall / platform_bus ops） |
| 全局 `static struct kb_device kb_device` | 删掉内核全局态，状态放 server 自己的内存 |
| `kb_register_callback2` 直接操作内核链表 | 变成 IPC 请求 → kb server 的 mailbox 处理 |
| `KLOG(...)` | 通过 log server / 共享 buffer |

**不变的部分**：IRQ → mailbox 的投递链路（`irq_request` → `mailbox_listen`）完全复用，
这正是第 4 步已经做好的转发机制。

#### 5.0.5 与前面步骤的衔接

- 依赖第 1 步的 **capability**（`cap_grant` / `cap_revoke_all`）和第 4 步的 **IRQ→mailbox 转发**。
- 依赖 `proc_create(PROC_PRIV_USER, entry, 0)` 能拉起独立地址空间的用户进程（第 6 步的 demo 也用同一机制）。
- 粒度可自由选择：一个 server 进程可以挂多个 device（`drv->entry` 内部自己 `irq_request`
  多个 IRQ），也可以一个 device 一个进程。
- 若不想让内核"拉起"进程，也可以反过来：server 启动后通过新 syscall
  `sys_driver_register(type, mailbox_id)` 把自己注册为某 type 的 user driver，
  `try_bind_and_probe` 找到已注册的 server 时只做绑定 + 授能力、不 spawn 进程。
### 5.1 目录结构调整

```
servers/                   # 新建：用户态服务进程
├── kb_server.c            # 键盘服务
├── vga_server.c           # VGA/终端服务
├── rtc_server.c           # RTC 时间服务
├── pci_server.c           # PCI 总线枚举（可选，当前无 PCI 设备）
└── makefile               # servers 的构建规则
```

### 5.2 键盘服务（kb_server）

| 属性 | 说明 |
|------|------|
| 需要的能力 | `CAP_IO_PORT(0x60-0x64)`, `CAP_IRQ_OWN(1)` |
| 提供的接口 | 键盘事件订阅/取消订阅 |
| 使用的 mailbox | `kb_irq_mbox`（收 IRQ），`kb_event_mbox`（广播按键） |

关键设计决策：
- 键盘服务**不直接**写 VGA 显存（与现有代码不同）。它只广播按键事件。
- 任何进程可以订阅按键事件，实现解耦。

### 5.3 VGA/终端服务（vga_server）

| 属性 | 说明 |
|------|------|
| 需要的能力 | `CAP_MEM_MAP(0xA0000-0xAFFFF)`, `CAP_IO_PORT(0x3C0-0x3DA)` |
| 提供的接口 | 字符输出、模式切换、framebuffer 获取、命令注册 |
| 使用的 mailbox | `vga_cmd_mbox`（收命令） |

关键设计决策：
- VGA framebuffer 通过**共享内存**提供给游戏进程，而非每次 `putchar` 都发 IPC。
- 终端命令系统（`terminal_register_command`）保留在 VGA server 中。

```c
// servers/vga_server.c —— 核心结构
void vga_server_main(void) {
    // 1. 映射 VGA 显存到自己的地址空间
    uint8_t *fb = (uint8_t*)sys_mem_map(0xA0000, 0x10000,
                                         MAP_READ | MAP_WRITE | MAP_IO);

    // 2. 初始化 VGA 硬件为文本模式
    vga_set_text_mode();

    // 3. 服务循环
    while (1) {
        mail_t *req = sys_mailbox_listen(VGA_CMD_MAILBOX, 0);

        switch (req->msg_type) {
        case VGA_CMD_SET_MODE:
            handle_set_mode(req);
            break;
        case VGA_CMD_PUTCHAR:
            handle_putchar(req, fb);
            break;
        case VGA_CMD_SCROLL:
            handle_scroll(fb);
            break;
        case VGA_CMD_GET_FB:
            // 将 framebuffer 共享给请求者
            handle_share_fb(req);
            break;
        case VGA_CMD_REGISTER_CMD:
            handle_register_command(req);  // 注册自定义终端命令
            break;
        }

        // 回复调用者
        sys_mailbox_send(req->reply_mailbox, response);
    }
}
```

### 5.4 RTC 时间服务（rtc_server）

| 属性 | 说明 |
|------|------|
| 需要的能力 | `CAP_IO_PORT(0x70-0x71)`, `CAP_IRQ_OWN(8)`（可选，周期性更新） |
| 提供的接口 | 时间查询、延迟请求 |
| 使用的 mailbox | `rtc_req_mbox`（收请求） |

**注意**：RTC 只提供**时间查询**。`timer_delay_ms` 的 PIT 部分留在内核（因为需要 I/O 端口操作），通过 syscall 提供：

```c
// syscall: 用户态可调用的延迟
int sys_sleep_ms(uint32_t ms);
```

### 5.5 命名服务（可选）

为了解耦服务发现，可以添加一个简单的命名服务：

```c
// 注册服务名 → mailbox ID 映射
int sys_name_register(const char *name, uint64_t mailbox_id);

// 查询服务
uint64_t sys_name_lookup(const char *name);
```

---

## 第 6 步：Demo 程序独立运行

> **将 airplane 和 snake 从直接调用内核 API 改为通过 IPC 与用户态服务通信。**

### 6.1 改造 airplane

```c
// demo/airplane.c —— 微内核版本

// 全局句柄（启动时通过命名服务获取）
static uint8_t *framebuffer = NULL;
static uint64_t vga_mbox = 0;
static uint64_t kb_event_mbox = 0;

void airplane_main(void) {
    // 1. 查找服务
    vga_mbox       = sys_name_lookup("vga");
    kb_event_mbox  = sys_name_lookup("keyboard_events");

    // 2. 获取共享 framebuffer
    framebuffer = (uint8_t*)sys_get_shared_fb(vga_mbox);

    // 3. 设置 graphics 模式（通过 VGA server）
    vga_cmd_t cmd = {.type = VGA_CMD_SET_MODE, .mode = 0x13};
    sys_mailbox_call(vga_mbox, &cmd, NULL, 500);

    // 4. 游戏主循环
    while (1) {
        // 非阻塞检查键盘输入
        mail_t *key = sys_mailbox_try_recv(kb_event_mbox);
        if (key) {
            handle_input(key);
            mail_release(key);
        }

        update_game_state();
        render_frame(framebuffer);
        sys_sleep_ms(16);  // ~60 FPS
    }
}
```

### 6.2 改造 snake

与 airplane 相同的模式：
1. 通过命名服务获取 VGA 和键盘服务句柄
2. 获取共享 framebuffer
3. 主循环：轮询键盘事件 → 更新游戏状态 → 渲染

### 6.3 构建系统调整

```makefile
# makefile 新增
SERVERS_DIR := servers
SERVERS_SRC := $(wildcard $(SERVERS_DIR)/*.c)
SERVERS_OBJ := $(SERVERS_SRC:$(SERVERS_DIR)/%.c=$(OBJS_DIR)/server_%.o)

DEMO_DIR    := demo
DEMO_SRC    := $(wildcard $(DEMO_DIR)/*.c)
DEMO_OBJ    := $(DEMO_SRC:$(DEMO_DIR)/%.c=$(OBJS_DIR)/demo_%.o)

# servers 和 demo 编译为用户态代码
# （编译选项中去掉内核相关的 include，链接时标记为用户态 ELF）
$(OBJS_DIR)/server_%.o: $(SERVERS_DIR)/%.c
	$(CC) $(CFLAGS) -D__USER_MODE__ -c $< -o $@

$(OBJS_DIR)/demo_%.o: $(DEMO_DIR)/%.c
	$(CC) $(CFLAGS) -D__USER_MODE__ -c $< -o $@
```

---

## 工作量估算

| 步骤 | 内容 | 预计时间 | 难度 | 新增/修改文件数 |
|------|------|---------|------|---------------|
| 0 | 修现有 bug | 2-3 天 | ⭐ | ~5 个文件 |
| 1 | 能力系统 | 5-7 天 | ⭐⭐⭐ | ~4 个新文件 + 3 个修改 |
| 2 | IPC 增强 | 5-7 天 | ⭐⭐⭐ | ~3 个新文件 + 2 个修改 |
| 3 | 内存映射原语 | 3-5 天 | ⭐⭐ | ~1 个新文件 + 2 个修改 |
| 4 | IRQ 转发 | 3-5 天 | ⭐⭐⭐ | ~1 个新文件 + 2 个修改 |
| 5 | 搬移驱动 | 5-7 天 | ⭐⭐ | ~4 个新文件 |
| 6 | demo 独立运行 | 3-4 天 | ⭐⭐ | ~2 个修改 |
| **总计** | | **4-6 周** | | **~25 个文件** |

---

## 重要提醒

### 1. 微内核的本质是用 IPC 开销换隔离性

你现有的 mailbox 设计已经有一个很好的基础，但要注意：

- **不要追求纯微内核**：把调度时钟留内核。把高频路径优化好。
- **共享内存是你的朋友**：VGA framebuffer 不要每次 `putchar` 都发 IPC——map 一次，共享访问。
- **批量处理**：可能的话，将多个操作合并为一次 IPC 调用。

### 2. 先 benchmark，再优化

改造前，先写一个简单的 IPC ping-pong 延迟测试：

```c
// 测试：从线程 A 发消息到线程 B，测 round-trip 延迟
void ipc_latency_test(void) {
    uint64_t start = timer_get_ticks();
    for (int i = 0; i < 10000; i++) {
        mailbox_call(target_mbox, req, &resp, 100);
    }
    uint64_t end = timer_get_ticks();
    KLOG("IPC round-trip: %d us\n", (end - start) / 10);
}
```

确保 RPC 延迟在**百微秒级以内**再继续后续步骤。

### 3. 逐步迁移，保持可运行

每一步完成后都应该有一个**可以启动并演示的版本**：

- 第 0 步后：现有功能正常，bug 修复
- 第 1 步后：能力系统就绪，但所有进程仍有全部能力（向后兼容）
- 第 2 步后：新 IPC API 可用，旧 API 暂存
- 第 3 步后：内存映射 syscall 可用
- 第 4 步后：可以手动测试 IRQ 转发
- 第 5 步后：键盘/VGA/RTC 在用户态运行
- 第 6 步后：airplane/snake 作为独立进程运行

**永远不要一次性改完所有东西再测试。**

### 4. 最终目标架构

完成改造后，系统将达到类似 **MINIX 3 / seL4** 的架构风格：

- 内核只保留调度、内存管理、IPC、IRQ 分发
- 所有设备驱动在用户态隔离运行
- 驱动崩溃不影响内核和其他服务
- 新驱动可以动态启动和重启

这是一个完整的、有教育意义的第二项目阶段。
