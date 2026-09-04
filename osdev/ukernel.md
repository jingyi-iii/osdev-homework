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
│  │ (PIT IRQ0)  │  │ rbtree VMM  │  │ 固定号 0..4（内核侧）   │  │
│  ├─────────────┼──┼─────────────┼──┼──────────────────────┤  │
│  │ capability  │  │ mailbox     │  │ portal (RPC)         │  │
│  │ (信任根)     │  │ (哑传输)     │  │ shm (共享内存)        │  │
│  │ IRQ 顶层分发 │  │ IRQ→mail 转发│  │ 固定 ns portal 引导   │  │
│  └─────────────┴──┴─────────────┴──┴──────────────────────┘  │
└──────────────────────────────────────────────────────────────┘
        │ GRUB multiboot modules（独立 ELF，各自地址空间）
        ▼
┌──────────────────────────────────────────────────────────────┐
    namespace_server.elf  唯一固定 portal（PORTAL_ID_NAMESPACE）：name → id 注册表
    terminal_server.elf   动态 console portal（注册 "console"）+ 图形 mode 0x13
    kb_server.elf         IRQ1 → 广播 MSG_KEY_EVENT（magic 以 "kb" 注册）
    log_server2.elf       动态 portal，注册 "log" → 写 COM1
    rtc_server.elf        动态 portal，注册 "rtc"：CMOS 时间 + PIT 计数 sleep
    portal_test.elf       portal RPC 测试
    hello.elf             demo：console portal + log portal + rtc
```

### 1.2 固定 syscall ABI（`include/kernel/uapi.h`）

用户程序通过 `int $100` 进入内核：`ebx=号, ecx=config, edx=size, eax=返回`。号码是**固定 ABI**，
独立链接的用户 ELF 无需链接内核符号即可调用。

| 号 | 宏 | 内核 handler | 说明 |
|----|-----|--------------|------|
| 0 | `SYSCALL_PROC_THREAD` | `kernel/process.c` | 进程/线程控制（create / exit / yield / block / unblock / get pid/tid） |
| 1 | `SYSCALL_IO` | `kernel/io.c` | 端口 I/O；按 `CAP_ACCESS_IO` 端口范围检查 |
| 2 | `SYSCALL_IRQ` | `kernel/irq.c` | 请求 / 释放 / 掩码 IRQ |
| 3 | `SYSCALL_MAILBOX` | `kernel/ipc/mailbox.c` | mailbox 收发（哑传输） |
| 4 | `SYSCALL_PORTAL` | `kernel/ipc/portal.c` | portal 同步 RPC（用户驱动服务统一走 portal/mailbox，无固定 driver syscall） |

### 1.3 启动流程（`kernel/init.c` 的 `init_thread`）

```
init_thread
├─ kterm_switch_to_text_mode() / kterm_clear()          # 内核自带 VGA 文本输出
├─ load_user_elf_by_name("namespace_server.elf", grant_ns_caps)
│     # CAP_IPC → 发布固定 PORTAL_ID_NAMESPACE
├─ load_user_elf_by_name("terminal_server.elf", grant_terminal_caps)
│     # VGA {0x3C0,32} + CAP_IPC → 动态 portal，注册 "console"
├─ load_user_elf_by_name("kb_server.elf", grant_kb_caps)
│     # CAP_OWN_IRQ(0x21), PS/2 {0x60,5}, COM1 {0x3F8,8}, CAP_IPC → 注册 "kb"
├─ load_user_elf_by_name("log_server2.elf", grant_log_caps)
│     # COM1 {0x3F8,8} + CAP_IPC → 动态 portal，注册 "log"
├─ load_user_elf_by_name("rtc_server.elf", grant_rtc_caps)
│     # CMOS {0x70,2} + PIT {0x40,4} + CAP_IPC → 动态 portal，注册 "rtc"
├─ load_user_elf_by_name("portal_test.elf", grant_demo_caps)
│     # VGA, COM1, CAP_IPC（PIT/PPI/CMOS 已收回：延时走 rtc server）
├─ load_user_elf_by_name("hello.elf", grant_hello_caps)  # demo：console portal + log portal
├─ load_user_elf_by_name("process_test.elf", grant_demo_caps)  # demo 测试菜单（按键驱动）
└─ proc_exit(proc_get_pid())
```

- server 以 **GRUB multiboot module** 形式加载（`config/grub.cfg`），按 cmdline basename 匹配。
- **加载顺序即进程创建顺序**：namespace server 必须先起来（唯一固定 portal），其余 server 才能注册动态 id、客户端才能解析；各客户端（console_putstr / user_log_write / 按键订阅）都有重试，顺序只是优化不是硬依赖。
- `grant_demo_caps` 还通过 `cap_inherit_all` 让 demo 创建的子进程继承能力。

### 1.4 组件清单总览

| 类别 | 组件 | 位置 | 状态 |
|------|------|------|------|
| ✅ 已落地 | 能力系统 | `kernel/capability.c` + `include/kernel/capability.h` | 6 类 cap，全部 syscall gate 已接入 |
| ✅ 已落地 | 固定 syscall ABI | `include/kernel/uapi.h` | 号 0..4 固定 |
| ⚰️ 已移除 | 用户态 syscall 注册（SYSCALL_SYSCTL/SYSCALL_LOG） | 曾 `kernel/syscall.c` CR3 切换执行用户 handler | 2026-09 移除：log 改为 namespace 里的 portal 服务（"log"），避免 ring-0 跑用户代码 |
| ✅ 已落地 | portal 同步 RPC | `kernel/ipc/portal.c` + `SYSCALL_PORTAL` | console portal 在用 |
| ✅ 已落地 | mailbox 哑传输 | `kernel/ipc/mailbox.c` + `SYSCALL_MAILBOX` | magic 不透明标签（2026-09 重构） |
| ✅ 已落地 | IRQ→mail 转发 | `kernel/irq.c` `dispatch_user_mode_irq` | `MAIL_MAGIC_IRQ` 定向投到注册线程 mailbox |
| ✅ 已落地 | VMM + cap 检查 | `kernel/mm/vmm.c` | map/unmap/alloc + `CAP_MAP_MEM` |
| ✅ 已落地 | shm 共享内存 | `kernel/ipc/shm.c` | portal 的数据通道 |
| ✅ 已落地 | 异常 dump | `arch/i386/irq.c` `exception_handler` | 寄存器/错误码/CR2/栈回溯 |
| ✅ 已落地 | 地基修复 | paging / list / heap | `split_4mb_pde(pde*)`、`list_for_each_safe`、`kmalloc` 8 字节对齐 |
| ✅ 已落地 | terminal / graphics server | `user/server/display/terminal_server.c` | 动态 console portal（注册 "console"）+ 按键回显（订阅 ns 解析的 kb magic）+ **图形模式 0x13**：`ESC 'G'` 控制帧 SET_MODE 切换、BLIT 把客户端 shm 的帧缓冲拷到 0xA0000 |
| ✅ 已落地 | kb server | `user/server/input/kb_server.c` | 收 IRQ 读 scancode → 广播 `MSG_KEY_EVENT`（以 "kb" 注册 magic+tid）；可打印键兼写 COM1 |
| ✅ 已落地 | 用户态 LOG server | `user/server/serial/log_server2.c` + `userlib`（`user_log_str/write`） | 动态 portal，注册 "log" → 写 COM1（2026-09 接线） |
| ⚰️ 死代码 | `driver.h` / `device.h` | `include/kernel/driver.h` / `device.h` | platform_bus 退役后的残留 |
| ✅ 已落地 | RTC / sleep server | `user/server/clock/rtc_server.c` + `user/server/server_msgs.h` | 动态 portal，注册 "rtc"；`RTC_CMD_GET_TIME`（CMOS BCD）+ `RTC_CMD_SLEEP_MS`（latch PIT ch0 计数，内核从不重编程 PIT）；userlib `user_rtc_time/user_rtc_sleep_ms`，demo `timer_delay_ms` 已接入（失败 fallback yield 循环） |
| ✅ 已落地 | 命名服务（namespace） | `user/server/ns/namespace_server.c` + `user/ns_proto.h` | 用户态 portal 服务（固定 PORTAL_ID_NAMESPACE）；`ns_register`/`ns_lookup` → {portal_id, mailbox_tid, mail_magic}（协议不进内核 ABI） |
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
- `mail` 携带 `magic`（`u32` 不透明标签，2026-09 重构）+ 内联 `data[256]`（2026-09 精简：只留
  magic / sender_tid / receiver_tid / data / data_size，sender_pid/receiver_pid 已删）。
- magic 常量：内核自有 `MAIL_MAGIC_IRQ = 0x66666666`（`include/kernel/uapi.h`）；用户自有的事件
  magic（如按键 `MSG_KEY_EVENT`）在 `user/server/server_msgs.h`。**内核只搬运、从不解释 magic**——
  新增应用消息类型零内核改动。
- 投递：`send` 定向（按 receiver_tid）；`MAIL_ANY_TID` 广播（sender_pid/receiver_pid 字段已删）；`try_get_mail` 非阻塞取信。
- **订阅/广播（2026-09 完成，内核侧）**：`mailbox_subscribe_mail(mb, magic)` / `mailbox_unsubscribe_mail`
  维护每个 mailbox 的 `subscriptions[16]`；广播时只投递给订阅了 `m->magic` 的线程（有 handler 直接
  回调 / 无 handler 入队克隆）。`magic == 0` 永不匹配（subscribe 拒绝 0）；引用计数按「已订阅且有
  handler」精确统计，杜绝中途误释放 / double-free。
- 用户侧 `user_mail_listen()`（`user/userlib.c`）= `LISTEN` + `user_yield()` **忙等轮询**。
- 用户侧封装（2026-09）：`user_mail_alloc/send`（定向或 `USER_MAIL_ANY_TID` 广播）、`user_mail_subscribe/
  unsubscribe`（作用于调用线程自己的 mailbox）、`user_mail_listen/release`；`user_mail` 视图可读写
  `magic`/`data`，收发方无需内核符号。

**portal（同步 RPC）** — `include/ipc/portal.h` / `kernel/ipc/portal.c`：
- `portal_call(portal_id, va, size)`：内核 `shm_share` 客户端缓冲 → 入队 → 客户端阻塞在 per-request
  semaphore → server `WAIT/GET_REQ/REPLY` 唤醒 → 返回 `int ret`。
- terminal server 在 namespace 注册 "console"，`console_putstr()` 经 namespace 解析后打印。
- **portal 已覆盖「同步 RPC」需求，`mailbox_call` 可以不实现**。

> 结论：mailbox 的**订阅/广播（内核侧）**与**用户侧半环**均已完成（userlib 封装、`server_msgs.h` 的
> `MSG_KEY_EVENT`、kb_server 广播、terminal 订阅回显）。RPC 半环由 portal 补齐。

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
| 驱动注册 | `platform_driver_register` | server 在 namespace 注册 name → {portal_id, mailbox_tid, mail_magic} |

- ✅ `terminal_server`：console portal 打印（注册 "console"；直写 `0xB8000` + 端口 `0x3D4/0x3D5` 移光标）+ 按键回显线程（ns 解析 kb magic 后订阅，uspinlock 保护 VGA 状态）。**兼作 graphics server**：解析 `ESC 'G'` 控制帧（`server_msgs.h` 的 `gfx_ctrl`）——`SET_MODE` 用 VGA 寄存器组切 mode 0x13/0x03，`BLIT` 把客户端 shm 的帧缓冲 memcpy 到 `0xA0000`；图形模式下文本打印与按键回显暂停。
- ✅ `kb_server`：IRQ1 → mailbox → 排空 8042 → 广播 `MSG_KEY_EVENT`（`key_event{scancode,ascii,pressed}`）；可打印键兼写 COM1。
- ✅ `rtc_server`：portal 服务注册 "rtc"（CMOS `{0x70,2}` + PIT `{0x40,4}` 经 `SYSCALL_IO`）——`RTC_CMD_GET_TIME` 读 BCD 时间、`RTC_CMD_SLEEP_MS` 用 latch PIT ch0 计数自旋（内核从不重编程 PIT，计数 ~0.838µs）。userlib `user_rtc_time/user_rtc_sleep_ms`；demo `timer_delay_ms` 走 SLEEP_MS RPC，失败 fallback yield 循环（server 未注册时 demo 进程自己已无 PIT/CMOS 端口能力）。
- ⚠️ kb 未处理 E0 扩展码（方向键留给 demo 需要时再加）。

### 2.6 内核侧（调度 / VMM / 异常 / 日志）

- 调度：单核，PIT IRQ0 tick，`schedule_from_isr`；进程/线程用全局链表 + wait queue。
- 日志：`arch/i386/klog.c`（ring-0 直写 COM1）+ `arch/i386/kterm.c`（VGA 文本启动横幅）。
  用户态 LOG：namespace "log" → log_server2.elf（ring-3 服务写 COM1）；客户端用
  `userlib` 的 `user_log_str/user_log_write`（2026-09 接线，hello.elf 验证通过）。
- 异常：`exception_handler` dump 全部寄存器 + 错误码 + CR2/PF 类型 + 栈回溯，然后 halt。

### 2.7 已知问题与坑（GOTCHA）

1. **低 16MB 内核区仍 user-accessible**：`arch_paging_init` 对前 16MB 身份映射用 `PTE_USER`，
   `copy_*_user` 无法区分用户指针与指向低端内核区的指针。需地址空间重映射根治（见 P3）。
2. **第一个任务之前禁止 `int $100`**：`irq.S` 的 `RESTORE_REGS_KEEP_EAX` 会解引用 `curr_task_ctx`（NULL）
   → 栈损坏 → 重启循环。早期 boot 的 LOG 必须走 `klog_write` 直写。
3. **mailbox 广播通配符两侧必须一致**：`MAIL_ANY_TID`（`include/ipc/mailbox.h`）必须等于
   `USER_MAIL_ANY_TID`（`user/userlib.h`）。曾因内核侧被改成 `-0xab`（旧 MAIL_ANY_PID 的值）而用户侧仍是
   `-0xcd`，导致 kb 广播被内核当成「点对点投递」而丢弃（症状：kb 的 COM1 诊断 `[x]` 照常，订阅者却全部
   静默）。键值现为 `-0xcd`，改任何一侧都要同步。
4. **GRUB module 的 cmdline 尾 token 就是名字**：`module /boot/xxx.elf xxx.elf` 缺尾 token 则 cmdline 为空，
   `modname_is` 匹配失败。
5. **multiboot v1 位号容易数错**：`MODULES = (1<<3)`（不是 `1<<4`）。
6. **模块落在 16MB 之外**：GRUB 把模块放内核镜像之后（~20MB），而 16MB..64MB 是 `PTE_KERNEL`，
   `copy_from_user`（要求 `PTE_USER`）会拒绝 → `proc_load_from_elf` 对 `PROC_PRIV_KERNEL` 调用方直接 `memcpy`。
7. **QEMU 调试**：`-monitor telnet:127.0.0.1:PORT,server,nowait` + `xp /Nwx ADDR` 查物理内存；
   `-kernel` 无法加载 multiboot module，调试必须走 ISO（`make run_debug`）。
8. **GRUB 模块区在 PMM bitmap 之上 = 空闲内存**：`pmm_init` 只保留 bitmap 以下的页，bitmap 以上
   全部释放；而 GRUB 把模块恰好放在内核镜像之后（~20MB，高于 ~21MB 的 bitmap）。启动期任何分配
   （`pmm_alloc_page` 会清零）都可能把手出的页落在模块区 → 模块内容被清零，装载时 elf 校验失败
   （症状：`user elf: validation failed`，`xp` 看模块物理内存全 0；首个 ~1MB 的 process_test.elf
   触发）。已在 `arch_paging_init` 里用新增的 `pmm_mark_used()` 保留全部模块区间。

---

## 三、向微内核演进的路线图

> 原则不变：**只有需要 CPU 特权级的代码留在内核**；把调度时钟留内核；共享内存优先；逐步迁移、每步可运行。

### P0 — 事件推送（mailbox 的「另一半」）⭐ 最高优先

**目标**：让 server 能把类型化事件推给 N 个订阅者（键盘 → 游戏）。

**已完成（2026-09）——事件推送全链路（除游戏 consumer）**：
- 内核（`kernel/ipc/mailbox.c`）：订阅注册表 + 广播按 magic 过滤；`magic == 0` 永不匹配；引用计数精确统计；
  SUBSCRIBE/UNSUBSCRIBE 支持 `mb == NULL`（自己的 mailbox）
- userlib：`user_mail_alloc/send/subscribe/unsubscribe/listen/release` + `user_mail` 视图 + `USER_MAIL_ANY_TID`
- 事件源（`user/server/input/kb_server.c`）：IRQ1 scancode → `MSG_KEY_EVENT` 广播，可打印键兼写 COM1
- consumer 样板（`user/server/display/terminal_server.c`）：开用户线程 ns_lookup("kb") 拿 magic →
  `user_mail_subscribe` → `user_mail_listen` 过滤回显到屏幕；共享头 `user/server/server_msgs.h`（`MSG_KEY_EVENT` + `key_event`）

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
| 1. ✅ 图形服务（已落地，terminal server 兼任） | terminal_server 解析 `ESC 'G'` 控制帧：`GFX_CTRL_SET_MODE` 用 VGA 寄存器组切 mode 0x13/0x03（0x13 寄存器表同标准 VGA，端口都在 `{0x3C0,32}` grant 内）；`GFX_CTRL_BLIT` 把客户端帧缓冲拷到 0xA0000 | `user/server/display/terminal_server.c` |
| 2. ✅ 帧缓冲传送（已落地） | userlib `gfx_set_mode/gfx_blit_shared`（shm_share 静态 header+fb 一次调用，非每次画点 IPC）；demo_common 提供共享 `gfx_fb` + `gfx_clear_screen/put_pixel/fill_rect/flush` | `user/userlib.c`、`user/demo/demo_common.c` |
| 3. ✅ 命名服务（已落地，用户态版） | `ns_register` / `ns_lookup` → {portal_id, mailbox_tid, mail_magic}（`user/server/ns/namespace_server.c` + `user/ns_proto.h`，固定 `PORTAL_ID_NAMESPACE`） | 用户态 server，非内核 syscall |
| 4. ✅ timer / sleep（已落地，用户态版） | rtc server `RTC_CMD_SLEEP_MS`：latch PIT ch0 计数自旋（~0.838µs/计数，wrap 用 u32 累加）；未加内核 `sys_sleep_ms` | `user/server/clock/rtc_server.c` |
| 5. demo 重写 | 去掉 `drivers/*.h`，改用 `userlib` + portal（console）+ mailbox（按键事件） | 🟡 部分：process_test.elf 已跑 4 套件 |
| 6. 构建 | makefile 加 demo ELF target + grub.cfg 加载 | ✅ process_test.elf target + grub.cfg 已加 |

### P2 — 服务补齐

| 子项 | 内容 | 位置 |
|------|------|------|
| 1. ✅ RTC server（已落地） | `rtc_server`：`CAP_ACCESS_IO(0x70-0x71)` + PIT `{0x40,4}`，portal 服务注册 "rtc"（GET_TIME / SLEEP_MS，同 log/terminal 模式） | `user/server/clock/` |
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
| P1 | 游戏 demo 上共享帧缓冲（graphics/timer/命名服务已完成） | 2-4 天 | ⭐⭐⭐ | `user/demo/*`、`user/server/display/` |
| P2 | 清理（RTC + log server 已接线） | ~半天 | ⭐⭐ | `user/server/`、`makefile` |
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
- ✅ 2026-09：namespace server —— 唯一固定 portal `PORTAL_ID_NAMESPACE`；console/log 改动态 id 并注册，客户端 `ns_lookup` 解析（console_putstr / user_log_write / 按键订阅）
- ✅ 2026-09：process_test.elf（demo 测试菜单）纳入 ISO 构建；demo_common 等完成 namespace 适配
- ✅ 2026-09：修复 mailbox 广播 —— `MAIL_ANY_TID`/`USER_MAIL_ANY_TID` 不一致导致按键广播被当点对点丢弃
- ✅ 2026-09：rtc_server.elf —— 用户态 RTC/sleep portal 服务（"rtc"）；demo `timer_delay_ms` 走 SLEEP_MS RPC，demo 进程的 PIT/PPI/CMOS 端口能力已收回
- ✅ 2026-09：terminal_server 图形模式 —— `ESC 'G'` 控制帧切 mode 0x13 + BLIT；process_test 菜单 `[0]` 弹跳方块 demo（250 帧 @20ms，rtc sleep 驱动帧率，结束回文本）；另修复 **PMM 未保留 GRUB 模块区** 导致 ~1MB 模块被启动期分配清零、elf 校验失败的问题（`pmm_mark_used`）
- P0 后：键盘事件能广播、游戏能收到（游戏 consumer 随 P1 demo）
- P1 后：airplane/snake 作为独立进程在共享帧缓冲上运行
- P2 后：死代码清理完毕（RTC 已可查时间，LOG 已走用户态 log server）
- P3 后：用户地址空间与内核低端隔离，`copy_*_user` 语义干净

**永远不要一次性改完所有东西再测试。**
