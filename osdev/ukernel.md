# 微内核（Microkernel）架构改造路线图（重写版 · 2026-09）

> 本文档基于对当前仓库代码的**完整重扫描**编写，取代旧版路线图。
>
> 旧版的 `platform_bus` / `DRIVER_CLASS_USER` / 顶层 `servers/` 目录方案，已被实际落地的
> **「独立用户 ELF server + 固定 syscall ABI + portal RPC」** 架构取代。凡与此冲突的旧章节
> （第 5 步的 bus-driver-device 改造等）一律不再适用，本文档按真实代码重新组织。

---

## 目录

1. [当前实际架构](#一当前实际架构)
2. [逐模块现状核对](#二逐模块现状核对)
3. [已知问题与坑（GOTCHA）](#三已知问题与坑gotcha)
4. [向微内核演进的路线图](#四向微内核演进的路线图)
5. [工作量估算](#五工作量估算)
6. [重要提醒](#六重要提醒)

---

## 一、当前实际架构

### 1.1 架构图

```
┌──────────────────────────────────────────────────────────────┐
│  µKernel（myos.bin，i686，单核）                              │
│  ┌─────────────┐  ┌─────────────┐  ┌──────────────────────┐  │
│  │ scheduler   │  │ VMM / PMM   │  │ syscall 分发 (int $100)│  │
│  │ (PIT IRQ0)  │  │ rbtree VMM  │  │ 固定号 0..6（内核侧）   │  │
│  ├─────────────┼──┼─────────────┼──┼──────────────────────┤  │
│  │ capability  │  │ mailbox     │  │ portal (RPC)         │  │
│  │ (信任根)     │  │ (哑传输)     │  │ shm (共享内存)        │  │
│  │ IRQ 顶层分发 │  │ IRQ→mail 转发│  │ 用户 syscall 注册     │  │
│  └─────────────┴──┴─────────────┴──┴──────────────────────┘  │
└──────────────────────────────────────────────────────────────┘
        │ GRUB multiboot modules（独立 ELF，各自地址空间）
        ▼
┌──────────────────────────────────────────────────────────────┐
    terminal_server.elf   console portal 打印 (PORTAL_ID_CONSOLE)
    kb_server.elf         IRQ1 → 广播 MSG_KEY_EVENT（订阅者收）
    log_server2.elf       SYSCALL_SYSCTL 认领 SYSCALL_LOG → 写 COM1
    portal_test.elf       portal RPC 测试
    hello.elf             demo：console portal + SYSCALL_LOG
```

### 1.2 固定 syscall ABI（`include/kernel/uapi.h`）

用户程序通过 `int $100` 进入内核：`ebx=号, ecx=config, edx=size, eax=返回`。号码是**固定 ABI**，
独立链接的用户 ELF 无需链接内核符号即可调用。

| 号 | 宏 | 内核 handler | 说明 |
|----|-----|--------------|------|
| 0 | `SYSCALL_PROC_THREAD` | `kernel/process.c` | 进程/线程控制（create / exit / yield / block / unblock） |
| 1 | `SYSCALL_IO` | `kernel/io.c` | 端口 I/O；按 `CAP_ACCESS_IO` 端口范围检查 |
| 3 | `SYSCALL_IRQ` | `kernel/irq.c` | 请求 / 释放 / 掩码 IRQ |
| 4 | `SYSCALL_MAILBOX` | `kernel/ipc/mailbox.c` | mailbox 收发（哑传输） |
| 5 | `SYSCALL_PORTAL` | `kernel/ipc/portal.c` | portal 同步 RPC |
| 6 | `SYSCALL_SYSCTL` | `kernel/syscall.c` | **用户态**注册/注销固定 syscall 号 |
| 7 | `SYSCALL_LOG` | `user/server/serial/log_server2.c`（SYSCALL_SYSCTL 认领） | LOG → 用户态 log server 写 COM1（2026-09 接线） |

**用户态 syscall 注册（`SYSCALL_SYSCTL`）**：ring-3 server 可认领一个固定号（如 `SYSCALL_LOG`），
内核保存 handler 连同注册进程的页目录；`syscall_dispatch()` 先切换 CR3 到该进程再在 ring-0 执行
handler，结束后切回调用者。log server 就是靠它在自己的地址空间里写 COM1。

### 1.3 启动流程（`kernel/init.c` 的 `init_thread`）

```
init_thread
├─ kterm_switch_to_text_mode() / kterm_clear()          # 内核自带 VGA 文本输出
├─ load_user_elf_by_name("terminal_server.elf", grant_terminal_caps)
│     # VGA 端口 {0x3C0, 32} + CAP_IPC → 发布 PORTAL_ID_CONSOLE
├─ load_user_elf_by_name("kb_server.elf", grant_kb_caps)
│     # CAP_OWN_IRQ(0x21), PS/2 {0x60,5}, COM1 {0x3F8,8}, CAP_IPC
├─ load_user_elf_by_name("log_server2.elf", 0)   # 认领 SYSCALL_LOG（无需 cap）
├─ load_user_elf_by_name("portal_test.elf", grant_demo_caps)
│     # VGA, COM1, PIT {0x40,4}, PPI {0x61,1}, CMOS {0x70,2}, CAP_IPC
├─ load_user_elf_by_name("hello.elf", grant_hello_caps)  # demo：console portal + SYSCALL_LOG
└─ proc_exit(proc_get_pid())
```

- server 以 **GRUB multiboot module** 形式加载（`config/grub.cfg`），按 cmdline basename 匹配。
- **加载顺序即进程创建顺序**：terminal server 必须先发布 console portal，portal_test 才能打印。
- `grant_demo_caps` 还通过 `cap_inherit_all` 让 demo 创建的子进程继承能力。

### 1.4 组件清单总览

| 类别 | 组件 | 位置 | 状态 |
|------|------|------|------|
| ✅ 已落地 | 能力系统 | `kernel/capability.c` + `include/kernel/capability.h` | 6 类 cap，全部 syscall gate 已接入 |
| ✅ 已落地 | 固定 syscall ABI | `include/kernel/uapi.h` | 号 0..7 固定 |
| ✅ 已落地 | 用户态 syscall 注册 | `kernel/syscall.c`（`syscall_register_user` + CR3 切换） | log server 的模式 |
| ✅ 已落地 | portal 同步 RPC | `kernel/ipc/portal.c` + `SYSCALL_PORTAL` | console portal 在用 |
| ✅ 已落地 | mailbox 哑传输 | `kernel/ipc/mailbox.c` + `SYSCALL_MAILBOX` | magic 不透明标签（2026-09 重构） |
| ✅ 已落地 | IRQ→mail 转发 | `kernel/irq.c` `dispatch_user_mode_irq` | `MAIL_MAGIC_IRQ` 定向投到注册线程 mailbox |
| ✅ 已落地 | VMM + cap 检查 | `kernel/mm/vmm.c` | map/unmap/alloc + `CAP_MAP_MEM` |
| ✅ 已落地 | shm 共享内存 | `kernel/ipc/shm.c` | portal 的数据通道 |
| ✅ 已落地 | 异常 dump | `arch/i386/irq.c` `exception_handler` | 寄存器/错误码/CR2/栈回溯 |
| ✅ 已落地 | 地基修复 | paging / list / heap | `split_4mb_pde(pde*)`、`list_for_each_safe`、`kmalloc` 8 字节对齐 |
| 🟡 半成品 | terminal server | `user/server/display/terminal_server.c` | 文本 console portal + 按键回显（订阅 MSG_KEY_EVENT）；**无图形模式** |
| ✅ 已落地 | kb server | `user/server/input/kb_server.c` | 收 IRQ 读 scancode → 广播 `MSG_KEY_EVENT`；可打印键兼写 COM1 |
| ✅ 已落地 | 用户态 LOG server | `user/server/serial/log_server2.c` + `userlib`（`user_log_str/write`） | SYSCALL_SYSCTL 认领 SYSCALL_LOG → COM1（2026-09 接线） |
| ⚰️ 死代码 | `driver.h` / `device.h` | `include/kernel/driver.h` / `device.h` | platform_bus 退役后的残留 |
| ⚰️ 死代码 | `user/demo/*` | `user/demo/` | 仍 `#include "drivers/*.h"`（已不存在），未构建 |
| ❌ 缺失 | 命名服务 | — | `sys_name_register` / `sys_name_lookup` 无 |
| ❌ 缺失 | RTC server | — | 无 |
| ❌ 缺失 | timer / sleep syscall | — | 用户态无 `sys_sleep_ms` |
| ✅ 已落地 | mailbox 订阅/广播 | `kernel/ipc/mailbox.c` + `user/userlib.h` | 内核订阅注册表 + 广播过滤；userlib 封装 `user_mail_subscribe/send`（2026-09） |
| 🟡 半成品 | 事件端到端接线 | `terminal_server.c` / 游戏 | kb 广播 + terminal 订阅回显完成（2026-09）；游戏 consumer 随 P1 demo |
| ❌ 缺失 | 地址空间根治 | — | 低 16MB 仍 user-accessible |

---

## 二、逐模块现状核对

### 2.1 能力系统（第 1 步 ✅）

`include/kernel/capability.h` / `kernel/capability.c`：

```c
typedef enum {
    CAP_OWN_IRQ,          // 拥有某个 IRQ 线
    CAP_MAP_MEM,          // 映射某段物理内存
    CAP_ACCESS_IO,        // 访问某段 I/O 端口范围
    CAP_IPC,              // 使用 mailbox/portal IPC
    CAP_CREATE_KRNL_PROC,     // 创建内核特权进程
    CAP_CREATE_KRNL_THREAD,   // 创建内核特权线程
} cap_type;
```

- `cap_check` / `cap_grant` / `cap_revoke` / `cap_revoke_all` / `cap_inherit_all` 全部可用。
- 已接入的检查点：`kernel/io.c`（端口范围）、`kernel/irq.c`（IRQ 归属）、`kernel/ipc/mailbox.c`
  （CAP_IPC 门）、`kernel/mm/vmm.c`（`CAP_MAP_MEM`）、`kernel/process.c`（进程/线程创建）。
- **微内核信任根已经就位**，后续新增 server 只需在 `init.c` 里加一个 grant 函数。

### 2.2 IPC：mailbox + portal（第 2 步部分完成）

**mailbox（异步消息，哑传输）** — `include/ipc/mailbox.h` / `kernel/ipc/mailbox.c`：
- `mail` 携带 `magic`（`u32` 不透明标签，2026-09 重构）+ 内联 `data[256]`。
- magic 常量：内核自有 `MAIL_MAGIC_IRQ = 0x66666666`（`include/kernel/uapi.h`）；用户自有的事件
  magic（如按键 `MSG_KEY_EVENT`）计划放 `user/server/server_msgs.h`（**该文件尚未创建**）。
  **内核只搬运、从不解释 magic**——新增应用消息类型零内核改动。
- 投递：`send` 定向（按 receiver_tid）；`MAIL_ANY_PID/TID` 广播；`try_get_mail` 非阻塞取信。
- **订阅/广播（2026-09 完成，内核侧）**：`mailbox_subscribe_mail(mb, magic)` / `mailbox_unsubscribe_mail`
  维护每个 mailbox 的 `subscriptions[16]`；广播时只投递给订阅了 `m->magic` 的线程（有 handler 直接
  回调 / 无 handler 入队克隆）。`magic == 0` 永不匹配（subscribe 拒绝 0）；引用计数按「已订阅且有
  handler」精确统计，杜绝中途误释放 / double-free。
- 用户侧 `user_mail_listen()`（`user/userlib.c`）= `LISTEN` + `user_yield()` **忙等轮询**。
- 用户侧封装（2026-09）：`user_mail_alloc/send`（定向或 `USER_MAIL_ANY_*` 广播）、`user_mail_subscribe/
  unsubscribe`（作用于调用线程自己的 mailbox）、`user_mail_listen/release`；`user_mail` 视图可读写
  `magic`/`data`，收发方无需内核符号。

**portal（同步 RPC）** — `include/ipc/portal.h` / `kernel/ipc/portal.c`：
- `portal_call(portal_id, va, size)`：内核 `shm_share` 客户端缓冲 → 入队 → 客户端阻塞在 per-request
  semaphore → server `WAIT/GET_REQ/REPLY` 唤醒 → 返回 `int ret`。
- terminal server 发布 `PORTAL_ID_CONSOLE`，`console_putstr()` 靠它打印。
- **portal 已覆盖「同步 RPC」需求，`mailbox_call` 可以不实现**。

> 结论：mailbox 的**订阅/广播机制（内核侧）已完成**（订阅注册表 + 按 magic 过滤）；仍缺的是把它接到
> 具体事件上的用户侧半环（userlib 封装、事件 magic 头、kb_server 广播、consumer）。RPC 半环已由 portal 补齐。

### 2.3 IRQ 转发（第 4 步 ✅）

- 用户态：`user_irq_request(major, minor)` → `SYSCALL_IRQ` → 内核记录 `irq->owner`（注册线程 tcb）+ `tid`。
- ISR：`dispatch_user_mode_irq()` 构造 `MAIL_MAGIC_IRQ` mail，`send_mail`（定向，不走订阅过滤）到 `t->mailbox`，非阻塞。
- `irqline` 没有路线图设想的 `owner_mailbox/owner_process` 字段——实际用 `irq->owner`（tcb 缓存）达成同一目的。
- 内核线程化 IRQ（`irq_request_threaded` + semaphore）和同步 IRQ 保留在 `irqline_handler` 的分支里。
- **IRQ0（PIT 调度时钟）留在内核**，正确。

### 2.4 内存映射 + shm（第 3 步 ✅ 功能等价）

- `vmm_map_memory(proc, phys, size, flags)` / `vmm_unmap_memory`：页对齐、`CAP_MAP_MEM` 检查、
  逐页 `arch_map_4kb`、失败回滚。纯内核 API（ELF 装载 / shm 使用）；`SYSCALL_VMM` ring-3 gate 已移除。
- `vmm_alloc_pages` / `vmm_free_pages` / `vmm_map_fixed`（ELF 装载用）/ `vmm_va_to_pa`：同上，内核内部使用。
- `shm_share(pid, va, size, out)` / `shm_unshare`：portal 的数据通道。

### 2.5 用户态 server（第 5 步 ✅ 以实际方案落地）

实际方案**不是**旧路线图的 platform_bus + `user_driver_start`，而是：

| | 旧路线图 | 实际落地 |
|---|---|---|
| server 形式 | `servers/*.c` 编进内核，内核 spawn 线程 | 独立 ring-3 ELF，GRUB module 加载 |
| 通信 | mailbox call + capability | 固定 syscall + portal RPC + mailbox |
| 资源授予 | `user_driver_start` 按 device 资源授予 | `init.c` 按 server 手写 grant 函数 |
| 驱动注册 | `platform_driver_register` | `SYSCALL_SYSCTL` 认领固定 syscall 号 |

- ✅ `terminal_server`：console portal 打印（直写 `0xB8000` + 端口 `0x3D4/0x3D5` 移光标）+ 按键回显线程（订阅 `MSG_KEY_EVENT`，uspinlock 保护 VGA 状态）。
- ✅ `kb_server`：IRQ1 → mailbox → 排空 8042 → 广播 `MSG_KEY_EVENT`（`key_event{scancode,ascii,pressed}`）；可打印键兼写 COM1。
- ⚠️ terminal 仍无图形模式；kb 未处理 E0 扩展码（方向键留给 demo 需要时再加）。

### 2.6 内核侧（调度 / VMM / 异常 / 日志）

- 调度：单核，PIT IRQ0 tick，`schedule_from_isr`；进程/线程用全局链表 + wait queue。
- 日志：`kernel/auxiliary/klog.c`（ring-0 直写 COM1）+ `kernel/auxiliary/kterm.c`（VGA 文本启动横幅）。
  用户态 LOG：`SYSCALL_LOG` → log_server2.elf（SYSCALL_SYSCTL 认领，ring-0 handler 写 COM1）；客户端用
  `userlib` 的 `user_log_str/user_log_write`（2026-09 接线，hello.elf 验证通过）。
- 异常：`exception_handler` dump 全部寄存器 + 错误码 + CR2/PF 类型 + 栈回溯，然后 halt。

### 2.7 已知问题与坑（GOTCHA）

1. **低 16MB 内核区仍 user-accessible**：`arch_paging_init` 对前 16MB 身份映射用 `PTE_USER`，
   `copy_*_user` 无法区分用户指针与指向低端内核区的指针。需地址空间重映射根治（见 P3）。
2. **第一个任务之前禁止 `int $100`**：`irq.S` 的 `RESTORE_REGS_KEEP_EAX` 会解引用 `curr_task_ctx`（NULL）
   → 栈损坏 → 重启循环。早期 boot 的 LOG 必须走 `klog_write` 直写。
3. **用户态 syscall handler 必须关中断**：`call_user_handler` 要在 handler 周围 `cli`，否则 timer ISR
   在 handler 中途切任务 → 恢复时 CR3 错误。
4. **GRUB module 的 cmdline 尾 token 就是名字**：`module /boot/xxx.elf xxx.elf` 缺尾 token 则 cmdline 为空，
   `modname_is` 匹配失败。
5. **multiboot v1 位号容易数错**：`MODULES = (1<<3)`（不是 `1<<4`）。
6. **模块落在 16MB 之外**：GRUB 把模块放内核镜像之后（~20MB），而 16MB..64MB 是 `PTE_KERNEL`，
   `copy_from_user`（要求 `PTE_USER`）会拒绝 → `proc_load_from_elf` 对 `PROC_PRIV_KERNEL` 调用方直接 `memcpy`。
7. **QEMU 调试**：`-monitor telnet:127.0.0.1:PORT,server,nowait` + `xp /Nwx ADDR` 查物理内存；
   `-kernel` 无法加载 multiboot module，调试必须走 ISO（`make run_debug`）。

---

## 三、向微内核演进的路线图

> 原则不变：**只有需要 CPU 特权级的代码留在内核**；把调度时钟留内核；共享内存优先；逐步迁移、每步可运行。

### P0 — 事件推送（mailbox 的「另一半」）⭐ 最高优先

**目标**：让 server 能把类型化事件推给 N 个订阅者（键盘 → 游戏）。

**已完成（2026-09）——事件推送全链路（除游戏 consumer）**：
- 内核（`kernel/ipc/mailbox.c`）：订阅注册表 + 广播按 magic 过滤；`magic == 0` 永不匹配；引用计数精确统计；
  SUBSCRIBE/UNSUBSCRIBE 支持 `mb == NULL`（自己的 mailbox）
- userlib：`user_mail_alloc/send/subscribe/unsubscribe/listen/release` + `user_mail` 视图 + `USER_MAIL_ANY_*`
- 事件源（`user/server/input/kb_server.c`）：IRQ1 scancode → `MSG_KEY_EVENT` 广播，可打印键兼写 COM1
- consumer 样板（`user/server/display/terminal_server.c`）：开用户线程 `user_mail_subscribe(MSG_KEY_EVENT)`
  → `user_mail_listen` 过滤回显到屏幕；共享头 `user/server/server_msgs.h`（`MSG_KEY_EVENT` + `key_event`）

**剩余**：

| 子项 | 内容 | 位置 |
|------|------|------|
| 1. 游戏 consumer | 游戏 `user_mail_subscribe(MSG_KEY_EVENT)` + `user_mail_listen` 轮询（随 P1 demo 一起做） | `user/demo/*` |
| 2.（可选）阻塞 listen | 用 wait_queue 把 `user_mail_listen` 从忙等改成真阻塞 | `kernel/ipc/mailbox.c` |
| 3.（可选）扩展键 | kb 处理 E0 前缀方向键 / shift 状态 | `user/server/input/kb_server.c` |

> 不做：`mailbox_call`（portal 已覆盖同步 RPC）。

### P1 — Demo 独立运行 ⭐

**目标**：`user/demo/` 从「引用不存在头文件的死代码」变成真正跑起来的独立进程。

| 子项 | 内容 | 位置 |
|------|------|------|
| 1. 图形服务 | terminal_server 目前是纯文本 console portal。新建/扩展出 **graphics server**（mode 0x13 帧缓冲） | `user/server/display/` |
| 2. 共享 framebuffer | gfx server 通过 `shm_share` 把帧缓冲共享给游戏（一次性映射，不是每次画点发 IPC） | `kernel/ipc/shm.c` |
| 3. 命名服务 | `sys_name_register(name, id)` / `sys_name_lookup(name)`（server 注册名 → portal/mailbox id） | `kernel/` + `uapi.h` |
| 4. timer / sleep | 新增 `sys_sleep_ms`（PIT 留内核，`timer_delay_ms` 的 PIT 部分内核化，端口走 `SYSCALL_IO` 也行） | `kernel/` |
| 5. kb 事件通道 | 依赖 P0 | — |
| 6. demo 重写 | 去掉 `drivers/*.h`，改用 `userlib` + portal（console）+ mailbox（按键事件） | `user/demo/*` |
| 7. 构建 | makefile 加 demo ELF target + grub.cfg 加载 | `makefile` / `config/grub.cfg` |

### P2 — 服务补齐

| 子项 | 内容 | 位置 |
|------|------|------|
| 1. RTC server | 新建 `rtc_server`：`CAP_ACCESS_IO(0x70-0x71)`，提供时间查询（`SYSCALL_LOG` 同款 sysctl 模式或 portal） | `user/server/` |
| 2. 清理死代码 | 删除或归档 `include/kernel/driver.h`、`include/kernel/device.h`；`user/demo/` 在新版跑通后替换 | — |
| 3. 用户态驱动 API | 若 demo 需要 `gfx_*` / `kb_poll` / `timer_*`，在 `user/server/server_msgs.h` 旁建一份用户侧 API 头，替代已删的 `drivers/*.h` | `user/` |

### P3 — 地址空间根治（第 0 步遗留）

**问题**：前 16MB 身份映射带 `PTE_USER` → `copy_*_user` 无法区分用户指针与低端内核指针。

**方向**：
1. 内核页表与用户页表分离：用户进程的页目录**不映射**低 16MB（或只映射必要部分）。
2. 内核映射改为只对内核可见（去掉 `PTE_USER`）。
3. 相应调整 `proc_load_from_elf` 的装载路径（现在靠低端身份映射 + `memcpy` 规避）。

> 这是**结构性改动**，牵一发动全身（syscall 的 kernel heap 低端映射、module 装载、portal 的 shm 共享都依赖低端映射）。
> 建议放在 P0/P1 全部跑通、功能冻结后再动。

---

## 四、工作量估算

| 阶段 | 内容 | 预计 | 难度 | 主要文件 |
|------|------|------|------|---------|
| P0（除游戏 consumer 已完成） | 游戏 consumer（随 P1 demo） | 半天 | ⭐⭐ | `user/demo/*` |
| P1 | graphics + 命名服务 + timer + demo | 5-8 天 | ⭐⭐⭐ | `user/server/display/`、`user/demo/*`、`makefile` |
| P2 | RTC + 清理（log server 已接线） | ~1 天 | ⭐⭐ | `user/server/`、`makefile` |
| P3 | 地址空间重映射 | 2-3 天 | ⭐⭐⭐⭐ | `arch/i386/paging.c`、`kernel/mm/vmm.c`、`kernel/syscall.c` |
| **总计** | | **2-3 周** | | |

---

## 五、重要提醒

### 1. 微内核的本质是用 IPC 开销换隔离性
- **不要追求纯微内核**：调度时钟（IRQ0）留内核；高频路径（按键轮询）别走 portal。
- **共享内存优先**：帧缓冲一次 `shm_share`，不要每次画点发 IPC。
- **批量处理**：能合并成一次 IPC 的操作合并。

### 2. 先 benchmark，再优化
写一个 IPC ping-pong 延迟测试（portal 或 mailbox 往返），确认延迟在**百微秒级以内**再继续。

### 3. 逐步迁移，保持可运行
每一步完成都应有一个可启动、可演示的版本：
- ✅ 现在：内核 + terminal/kb/portal 三个 server 可启动，console 可打印，键盘有 COM1 诊断
- ✅ 2026-09：mailbox 订阅/广播 + kb_server 广播 + terminal 按键回显（P0 主线完成）
- ✅ 2026-09：log server 接线 —— SYSCALL_LOG → log_server2.elf → COM1（hello.elf 验证通过）
- P0 后：键盘事件能广播、游戏能收到（游戏 consumer 随 P1 demo）
- P1 后：airplane/snake 作为独立进程在共享帧缓冲上运行
- P2 后：RTC 可查时间（LOG 已走用户态 log server）
- P3 后：用户地址空间与内核低端隔离，`copy_*_user` 语义干净

**永远不要一次性改完所有东西再测试。**
