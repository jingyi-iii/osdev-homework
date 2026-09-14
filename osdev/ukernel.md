# 微内核（Microkernel）架构改造路线图（重写版 · 2026-09）

> 本文档基于对当前仓库代码的**完整重扫描**编写，取代旧版路线图。
>
> **2026-09-14 更新**：对 HEAD（`c0905e4`）做了全仓重扫 + QEMU 无头实测复验（82 次启动采样、
> 截屏、串口日志、菜单 [9] benchmark 实测）。本次新增：`pit_ramp.elf` 首进程与 PIT 1000 Hz 化、
> rtc_server 的 TSC deadline 睡眠、`hello.elf` 退出启动集、ipc_bench 并入 process_test 菜单、
> syscall 号重排（§1.2）、以及两项**在案未决缺陷**：启动期 #GP 竞态（≈7% 启动，见 GOTCHA 13）与
> mode-0x13 字体回归（见 GOTCHA 9）。
>
> **2026-09-14 修复（同日）**：上述两项缺陷**均已修复并验证**——启动期 #GP 竞态（根因：syscall 出口的
> EAX 交付在切换线程时把调用方的返回值带给了被恢复的线程；改为「结果停放保存帧 + 全量恢复」，并删除
> `user_service.c` 的裸 I/O 分支，见 GOTCHA 13）；mode-0x13 字体回归（惰性快照 plane 2 + 回文本重载，
> 见 GOTCHA 9）。**修复后 72 次启动采样 0 崩溃**（修复前同条件 ≈7%），菜单/bench/图形往返/线程风暴
> 交互验证全绿。
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
│  │ IRQ 顶层分发 │  │ IRQ→mail 转发│  │ 固定 ns portal 引导   │  │
│  └─────────────┴──┴─────────────┴──┴──────────────────────┘  │
└──────────────────────────────────────────────────────────────┘
        │ GRUB multiboot modules（独立 ELF，各自地址空间）
        ▼
┌──────────────────────────────────────────────────────────────┐
    pit_ramp.elf          首进程：TSC 校准 + PIT ch0 爬坡至 1000 Hz（write-test 每步），
                          完成后退出；init_thread 等它结束才加载其它 ELF
    namespace_server.elf  唯一固定 portal（PORTAL_ID_NAMESPACE）：name → id 注册表
    terminal_server.elf   动态 console portal（注册 "console"）+ 图形 mode 0x13
    kb_server.elf         IRQ1 → 广播 MSG_KEY_EVENT（magic 以 "kb" 注册）
    log_server2.elf       动态 portal，注册 "log" → 写 COM1
    rtc_server.elf        动态 portal，注册 "rtc"：CMOS 时间 + TSC deadline 睡眠
    portal_test.elf       portal RPC 测试
    process_test.elf      按键菜单（thread/process/rbtree/sched_mix 套件 + 图形 demo +
                          内嵌 ipc_bench 菜单项 9）
    （hello.elf 仍在 makefile 有构建目标，但已移出启动集，ISO 不加载、grub.cfg 无此项）
```

### 1.2 固定 syscall ABI（`include/kernel/uapi.h`）

用户程序通过 `int $100` 进入内核：`ebx=号, ecx=config, edx=size, eax=返回`。号码是**固定 ABI**，
独立链接的用户 ELF 无需链接内核符号即可调用。

> ⚠️ **2026-09-05（`04e452d`）号位重排**：IO/IRQ/MAILBOX/PORTAL/HEAP 的顺序变了（下表为当前值）。
> 用户侧 `user/userlib.h` 的命令号/结构布局必须与内核同步，改动任何一侧都要两侧一起改。

| 号 | 宏 | 内核 handler | 说明 |
|----|-----|--------------|------|
| 0 | `SYSCALL_PROC_THREAD` | `kernel/process.c` | 进程/线程控制（create / exit / yield / block / unblock / get pid/tid） |
| 1 | `SYSCALL_IRQ` | `kernel/irq.c` | 请求 / 释放 / 掩码 IRQ |
| 2 | `SYSCALL_MAILBOX` | `kernel/ipc/mailbox.c` | mailbox 收发（哑传输） |
| 3 | `SYSCALL_PORTAL` | `kernel/ipc/portal.c` | portal 同步 RPC（用户驱动服务统一走 portal/mailbox，无固定 driver syscall） |
| 4 | `SYSCALL_HEAP` | `kernel/mm/heap.c` | 用户堆 `malloc/free`——共享 user-heap 区 `[0xC0000000,0xC1000000)`（2026-09） |
| 5 | `SYSCALL_IO` | `kernel/io.c` | 端口 I/O；按 `CAP_ACCESS_IO` 端口范围检查 |
| 6 | `SYSCALL_MMIO` | `kernel/mmio.c` | 把授权 MMIO 窗口映射到调用者自选高 VA（`CAP_MAP_MEM`；terminal 的 VGA 窗口走这里） |

### 1.3 启动流程（`kernel/init.c` 的 `init_thread`）

```
init_thread
├─ kterm_switch_to_text_mode() / kterm_clear()          # 内核自带 VGA 文本输出
├─ load_user_elf_by_name("pit_ramp.elf", grant_pit_ramp_caps)   # ★ 首个用户进程
│     # PIT {0x40,4} + COM1 {0x3F8,8}；TSC 校准 → PIT ch0 爬坡 {100,200,500,1000} Hz
│     #（每步 write-test，失败回滚上一档），日志直写 COM1（此时还没有 log server）
│     # init_thread 用 hlt 轮询等它退出（≤20000 次自旋）后才继续加载
├─ load_user_elf_by_name("namespace_server.elf", grant_ns_caps)
│     # CAP_IPC → 发布固定 PORTAL_ID_NAMESPACE
├─ load_user_elf_by_name("terminal_server.elf", grant_terminal_caps)
│     # VGA {0x3C0,32} + CAP_IPC + CAP_MAP_MEM({0xB8000,4K}/{0xA0000,64K}) → 注册 "console"
├─ load_user_elf_by_name("kb_server.elf", grant_kb_caps)
│     # CAP_OWN_IRQ(0x21), PS/2 {0x60,5}, COM1 {0x3F8,8}, CAP_IPC → 注册 "kb"
├─ load_user_elf_by_name("log_server2.elf", grant_log_caps)
│     # COM1 {0x3F8,8} + CAP_IPC → 动态 portal，注册 "log"
├─ load_user_elf_by_name("rtc_server.elf", grant_rtc_caps)
│     # CMOS {0x70,2} + CAP_IPC（PIT 已不授予）→ 动态 portal，注册 "rtc"
├─ load_user_elf_by_name("portal_test.elf", grant_demo_caps)
│     # VGA, COM1, CAP_IPC
├─ load_user_elf_by_name("process_test.elf", grant_demo_caps)  # demo 测试菜单（按键驱动）
│     # 选项 9 = 内嵌的 ipc_bench（不再是独立 ELF）
└─ proc_exit(proc_get_pid())
```

- server 以 **GRUB multiboot module** 形式加载（`config/grub.cfg`），按 cmdline basename 匹配。
- **pit_ramp 先行**：PIT IRQ0 频率是全局共享状态，先爬坡、后加载其它进程，避免客户端做
  「爬坡完成了吗」探测。PIT 之后由内核一直使用，**唯一会重编程 PIT 的是 pit_ramp.elf**。
- **加载顺序即进程创建顺序**：namespace server 必须先起来（唯一固定 portal），其余 server 才能注册动态 id、客户端才能解析；各客户端（console_putstr / user_log_write / 按键订阅）都有重试，顺序只是优化不是硬依赖。
- `grant_demo_caps` 还通过 `cap_inherit_all` 让 demo 创建的子进程继承能力。
- **hello.elf 已移出启动集**（它的 2000 ms 启动睡眠会独占单线程 rtc server）——构建目标保留，
  要回归需要把 `hello.c` 重新做成 GRUB module 并在 `init.c` 加回加载调用。

### 1.4 组件清单总览

| 类别 | 组件 | 位置 | 状态 |
|------|------|------|------|
| ✅ 已落地 | 能力系统 | `kernel/capability.c` + `include/kernel/capability.h` | 6 类 cap，全部 syscall gate 已接入 |
| ✅ 已落地 | 固定 syscall ABI | `include/kernel/uapi.h` | 号 0..6 固定 |
| ⚰️ 已移除 | 用户态 syscall 注册（SYSCALL_SYSCTL/SYSCALL_LOG） | 曾 `kernel/syscall.c` CR3 切换执行用户 handler | 2026-09 移除：log 改为 namespace 里的 portal 服务（"log"），避免 ring-0 跑用户代码 |
| ✅ 已落地 | portal 同步 RPC | `kernel/ipc/portal.c` + `SYSCALL_PORTAL` | console portal 在用 |
| ✅ 已落地 | mailbox 哑传输 | `kernel/ipc/mailbox.c` + `SYSCALL_MAILBOX` | magic 不透明标签；2026-09 mail/mailmeta 拆分（用户堆 mail + 内核堆簿记） |
| ✅ 已落地 | IRQ→mail 转发 | `kernel/irq.c` `dispatch_user_mode_irq` | `MAIL_MAGIC_IRQ` 定向投到注册线程 mailbox |
| ✅ 已落地 | VMM + cap 检查 | `kernel/mm/vmm.c` | map/unmap/alloc + `vmm_map_fixed(...,own_phys)`/`vmm_unmap_fixed` + `CAP_MAP_MEM` |
| ✅ 已落地 | shm 共享内存 | `kernel/ipc/shm.c` | portal 的数据通道 |
| ✅ 已落地 | 异常 dump | `arch/i386/irq.c` `exception_handler` | 寄存器/错误码/CR2/栈回溯 |
| ✅ 已落地 | 地基修复 | paging / list / heap | `split_4mb_pde(pde*)`、`list_for_each_safe`、`kmalloc` 8 字节对齐 |
| ✅ 已落地 | PIT 爬坡首进程 | `user/pit_ramp.c` | 启动第一个用户 ELF：TSC 校准 + PIT ch0 爬坡 {100,200,500,1000} Hz（每步 write-test / 失败回滚），完成即退出；init 等它结束才加载其余 ELF |
| ✅ 已落地 | terminal / graphics server | `user/server/display/terminal_server.c` | 动态 console portal（注册 "console"）+ 按键回显 + **图形 mode 0x13**：VGA 窗口经 `SYSCALL_MMIO` 映射到高 VA（0xE0000000/0xE0010000），`BLIT` 把客户端 fb 拷到 gfx 别名；mode 0x13 往返后由**惰性字体快照/回载**恢复字形（2026-09-14，见 GOTCHA 9） |
| ✅ 已落地 | kb server | `user/server/input/kb_server.c` | 收 IRQ 读 scancode → 广播 `MSG_KEY_EVENT`（以 "kb" 注册 magic+tid）；可打印键兼写 COM1 |
| ✅ 已落地 | 用户态 LOG server | `user/server/serial/log_server2.c` + `userlib`（`user_log_str/write`） | 动态 portal，注册 "log" → 写 COM1（2026-09 接线） |
| ✅ 已删除 | `driver.h` / `device.h`（platform_bus 残留）| 曾 `include/kernel/` | 2026-09 删除；驱动接口统一走 userlib + portal/mailbox + namespace |
| ✅ 已落地 | RTC / sleep server | `user/server/clock/rtc_server.c` + `user/server/server_msgs.h` | 动态 portal，注册 "rtc"；`RTC_CMD_GET_TIME`（CMOS BCD）+ `RTC_CMD_SLEEP_MS`（**TSC deadline 自旋，TSC 对 RTC 1 Hz 秒沿校准**——分频无关；PIT 只归 pit_ramp）；userlib `user_rtc_time/user_rtc_sleep_ms`，demo `timer_delay_ms` 已接入（失败 fallback yield 循环） |
| ✅ 已落地 | 命名服务（namespace） | `user/server/ns/namespace_server.c` + `user/ns_proto.h` | 用户态 portal 服务（固定 PORTAL_ID_NAMESPACE）；`ns_register`/`ns_lookup` → {portal_id, mailbox_tid, mail_magic}（协议不进内核 ABI） |
| ✅ 已落地 | mailbox 订阅/广播 | `kernel/ipc/mailbox.c` + `user/userlib.h` | 内核订阅注册表 + 广播过滤；userlib 封装 `user_mail_subscribe/send`（2026-09） |
| 🟡 半成品 | 事件端到端接线 | `terminal_server.c` / 游戏 | kb 广播 + terminal 订阅回显完成（2026-09）；游戏 consumer 随 P1 demo |
| ✅ 已落地 | MMIO 窗口 syscall | `kernel/mmio.c` + `SYSCALL_MMIO` | ring3 映射授权 MMIO 到自选高 VA（fixed + own_phys=0）；gate 校验 va≥USER_HEAP_END |
| ✅ 已落地 | 低 16MB 隔离 | `arch/i386/paging.c` | 前 16MB 恒等映射改 `PTE_KERNEL`（2026-09）；ring3 不可再读写内核 text/堆/页表 |
| ✅ 已消除 | 用户栈落低物理页 | `arch/i386/paging.c` + `vmm.c` | `USER_ANON_BASE=0xD0000000` 锚定所有用户线程栈 + `arch_unmap_4kb` 恒等带防御（2026-09-13）；用户 VA 永不进入内核恒等带 |
| ✅ 已修复 | 启动期 #GP 竞态 | `arch/i386/irq.S` + `user/server/user_service.c` | 2026-09-14 修复：syscall 出口「结果停放保存帧 + 全量恢复」（旧 `RESTORE_REGS_KEEP_EAX` 在切换线程时把调用方返回值带给了被恢复的线程）；裸 I/O 分支删除。修复后 72 次启动 0 崩溃，见 GOTCHA 13 |
| ✅ 已修复 | mode-0x13 后字体丢失 | `user/server/display/terminal_server.c` | 2026-09-14 修复：惰性快照 plane 2（首次图形会话前）+ `gfx_enter_text()` 回载（`sr[4]`=0x06 → 0x02）；多次往返截屏验证字形完好，见 GOTCHA 9 |

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
- **mail/mailmeta 拆分（2026-09）**：`mail`（用户可见，共享 user-heap `malloc`，ring3 直接读写，**无内核指针**）
  与 `mailmeta`（内核堆 `kmalloc`：ref_count/sp_lock/队列节点/`payload`）分离。内核经全局 inflight 注册表
  `get_mailmeta(m)` 按 payload 反查 meta——ring3 不再持有任何内核簿记指针，伪造/已释放的 mail* 安全失败。
  mailbox syscall gate 拒绝 ring3 传入**非空 `mb`**（`mb==NULL` 由内核解析为调用线程自己的 mailbox）。
- **订阅/广播（2026-09 完成，内核侧）**：`mailbox_subscribe_mail(mb, magic)` / `mailbox_unsubscribe_mail`
  维护每个 mailbox 的 `subscriptions[16]`；广播时只投递给订阅了 `m->magic` 的线程（有 handler 直接
  回调 / 无 handler 入队克隆）。`magic == 0` 永不匹配（subscribe 拒绝 0）；引用计数按「已订阅且有
  handler」精确统计，杜绝中途误释放 / double-free。
- 用户侧 `user_mail_listen()` / `user_irq_wait()`（`user/userlib.c`）= **两段式阻塞收信**（2026-09 从忙等
  轮询改为真阻塞）：`MAILBOX_CTRL_LISTEN_BLOCK` 在**内核 tail-block**（deferred switch 到 gate 出口，
  唤醒后直接回用户态）→ 再发一个非阻塞 `LISTEN` 取信（portal `WAIT_REPLY → GET_RESULT` 同款模式）；
  唤醒后用户态自动重试吸收 spurious wake。
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
- 内核同步 IRQ（`irq_request`）保留在 `irqline_handler` 的分支里；内核线程化 IRQ（`irq_request_threaded`
  + 信号量 + `irq_defer_unmask`）2026-09 已整块删除——树内无调用者，且其 ring3 路由允许内核线程以
  CPL0 执行调用方提供的回调指针（见下"已删除"记录）。
- **IRQ0（PIT 调度时钟）留在内核**，正确。

### 2.4 内存映射 + shm（第 3 步 ✅ 功能等价）

- `vmm_map_memory(proc, phys, size, flags)` / `vmm_unmap_memory`：页对齐、`CAP_MAP_MEM` 检查、
  逐页 `arch_map_4kb`、失败回滚。纯内核 API（shm 使用）；`SYSCALL_VMM` ring-3 gate 已移除。
- `vmm_alloc_pages` / `vmm_free_pages` / `vmm_va_to_pa`：内核内部使用（分配用户页/栈）。
- `vmm_map_fixed(proc, pa, vaddr, size, flags, own_phys)` / `vmm_unmap_fixed(proc, vaddr, size)`
  （2026-09 起带 own_phys）：调用者指定 VA 的映射。`own_phys=1`（ELF loader）页归映射所有，销毁地址空间
  或 `vmm_unmap_fixed` 时还 PMM；`own_phys=0`（MMIO）纯别名、永不还 PMM。`kernel/mmio.c`（`SYSCALL_MMIO`）
  对 ring3 暴露 fixed + own_phys=0 子集。
- `shm_share(pid, va, size, out)` / `shm_unshare`：portal 的数据通道。

### 2.5 用户态 server（第 5 步 ✅ 以实际方案落地）

实际方案**不是**旧路线图的 platform_bus + `user_driver_start`，而是：

| | 旧路线图 | 实际落地 |
|---|---|---|
| server 形式 | `servers/*.c` 编进内核，内核 spawn 线程 | 独立 ring-3 ELF，GRUB module 加载 |
| 通信 | mailbox call + capability | 固定 syscall + portal RPC + mailbox |
| 资源授予 | `user_driver_start` 按 device 资源授予 | `init.c` 按 server 手写 grant 函数 |
| 驱动注册 | `platform_driver_register` | server 在 namespace 注册 name → {portal_id, mailbox_tid, mail_magic} |

- ✅ `pit_ramp`（首进程）：TSC 校准 + PIT ch0 爬坡 {100,200,500,1000} Hz，每步 write-test（相对 wrap-rate ≥1.3× 才算过，失败回滚上一档），日志直写 COM1；跑完即 exit，`init_thread` hlt 轮询等它结束。当前实测爬满 **1000 Hz**（QEMU 稳定；10 kHz 会让 ipc_bench 的 TSC 校准读数抖动，梯子顶格留在 1000）。
- ✅ `terminal_server`：console portal 打印（注册 "console"）+ 按键回显线程（ns 解析 kb magic 后订阅，uspinlock 保护 VGA 状态）。
  **VGA 窗口经 `SYSCALL_MMIO` 映射到自选高 VA**：文本 `0xB8000`→`0xE0000000`、mode-13 fb `0xA0000`→`0xE0010000`
  （init.c 授 `CAP_MAP_MEM` {0xB8000,0x1000}+{0xA0000,0x10000}）；低地址直写已不可用（低 16MB supervisor），
  映射失败则指针 NULL、绘制路径 no-op。**兼作 graphics server**：解析 `ESC 'G'` 控制帧（`server_msgs.h` 的
  `gfx_ctrl`）——`SET_MODE` 用 VGA 寄存器组切 mode 0x13/0x03（并恢复 16 色文本调色板），`BLIT` 把客户端帧缓冲拷到 gfx 高 VA 别名；
  图形模式下文本打印与按键回显暂停。mode 0x13 会打烂 plane 2 字库：terminal 在**首次图形会话前惰性快照**、回文本时 `font_load()` 重载
  （2026-09-14 修复，见 GOTCHA 9）。
- ✅ `kb_server`：IRQ1 → mailbox → 排空 8042 → 广播 `MSG_KEY_EVENT`（`key_event{scancode,ascii,pressed}`）；可打印键兼写 COM1。
- ✅ `rtc_server`：portal 服务注册 "rtc"（仅 CMOS `{0x70,2}` 经 `SYSCALL_IO`）——`RTC_CMD_GET_TIME` 读 BCD 时间、`RTC_CMD_SLEEP_MS` 用 **TSC deadline 自旋**（TSC 对 RTC 1 Hz 秒沿校准，分频无关；PIT 自 pit_ramp 后无人再编程）。userlib `user_rtc_time/user_rtc_sleep_ms`；demo `timer_delay_ms` 走 SLEEP_MS RPC，失败 fallback yield 循环（rtc 起不来时没有其它定时手段——demo 进程自身已无 PIT/CMOS 端口能力）。
- ✅ `hello.elf`：已移出启动集（build target 保留）；要回归需把它重新加入 grub.cfg + init.c，并接受它启动时 2 s 的 rtc 睡眠会独占单线程 rtc server。
- ⚠️ kb 未处理 E0 扩展码（方向键留给 demo 需要时再加）。

### 2.6 内核侧（调度 / VMM / 异常 / 日志）

- 调度：单核，PIT IRQ0 tick（`schedule_isr` → `schedule_if_needed`）；进程/线程用全局链表 + wait queue。
  启动时由 pit_ramp 把 tick 拉到 **1000 Hz**（~1 ms），`schedule_isr` 每 **5 个 tick（= 5 ms）** 才做一次
  抢占式调度（`schedule_lock` trylock：锁忙就跳过本 tick，避免与持锁的 ring-3 线程死锁）。上下文切换为
  **lazy CR3**：切换点只 `request_switch_context()` 登记 {vcb, next ctx}，真正提交在门出口
  `arch_task_context_switch()`（见 GOTCHA 11 补记）。
- 日志：`arch/i386/klog.c`（ring-0 直写 COM1）+ `arch/i386/kterm.c`（VGA 文本启动横幅）。
  用户态 LOG：namespace "log" → log_server2.elf（ring-3 服务写 COM1）；客户端用
  `userlib` 的 `user_log_str/user_log_write`（2026-09 接线，hello.elf 验证通过）。
- 异常：`exception_handler` dump 全部寄存器 + 错误码 + CR2/PF 类型 + 栈回溯，然后 halt。

### 2.7 已知问题与坑（GOTCHA）

1. ~~低 16MB 内核区 user-accessible~~（2026-09 已修）：前 16MB 恒等映射已改 `PTE_KERNEL`，ring3 无法再读写
   内核 text/堆/页表；`copy_*_user` 的页表 walk 因此自动拒绝内核页（原「无下界」问题随之关闭）。VGA 访问由
   `SYSCALL_MMIO` 高 VA 映射承担（见 §2.8）。
   **残留（独立于此修复，2026-09-13 已消除）**：用户线程栈曾可能落 16MB~kernel_end 段；现由
   `USER_ANON_BASE=0xD0000000` 统一锚定高区，`arch_unmap_4kb` 对恒等带只恢复不删除（见 GOTCHA 12）。
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
9. **【已修复 2026-09-14】mode-0x13 往返后字体不恢复**：VGA mode 0x13 是 chain4，线性写 0xA0000 会
   逐字节摊到 4 个 plane，**打烂 plane 2 的 8x16 字库**（BIOS/GRUB 只在启动时装载一次）；只切回 0x03
   寄存器组不会重载字库 → 图形 demo 结束后所有文本乱码（2026-09-14 截屏实锤）。
   历史：`435c05f` 曾实现启动时 `font_save()` + 回文本 `font_load()`（与 vgafont16 逐字节验证过），
   但 `04e452d` 因「启动期 user ELF 与内核 kterm 并发——快照改 VGA 窗口期间 0xB8000 的内核写被重定向
   进 VRAM、冲坏字体」**整体移除**，之后未补回安全版本。
   **修复（本次已落地）**：改为**惰性快照**——`font_save()` 只在**第一次** `gfx_enter_mode13()` 开头执行
   （此时 boot 已结束、无内核 kterm 写；terminal 自身的写入由 `g_vga_lock` 与按键线程互斥）；字库窗口经
   高 VA（0xE0010000）访问。`gfx_enter_text()` 在 `term_clear_screen()` 之前 `font_load()` 回载；上传
   期间 `sr[4]`=0x06、完毕恢复 0x02（QEMU odd/even 过滤丢半字形字节的坑，见 435c05f）。
   **验证**：菜单 [0] 图形 demo 连续 3 次往返后截屏，菜单字形全部正确（修复前整屏乱码）。
   （并发渲染修复仍在：`draw_menu` 用 `ESC[2J`+全部行拼成**一次 portal 调用**原子渲染，terminal
   `term_print` 支持行内 `ESC[2J/ESC[H`。）
10. **QEMU monitor `xp` 读 0xB8000 不可靠**：一旦有代码碰过 VGA GC/SEQ 寄存器，`xp` 读文本缓冲会
    返回缺字/行重叠/NUL 带等假象（看起来像缓冲损坏）。真实缓冲要用**内核侧读回打到 COM1** 验证
    （曾因此误判菜单被写坏数小时）；截屏（实际渲染输出）可靠。
11. **【已修复 2026-09】syscall gate 判 ring-3 恒假 → kbuf 拷贝路径从未生效 → 尾阻塞后 #PF（菜单 9/ipc_bench 必崩）**：
    `kernel/syscall.c` 的 `syscall_dispatch` 原本用 `arch_running_ring3()` 判断“调用者是 ring3 吗”，
    但该辅助函数读的是**当前 CS**——在 gate 内部恒为 CPL0，判断**永远为假**：kbuf 拷贝路径是**死代码**，
    所有 ring3 handler 一直在**直接解引用调用者原始指针**。一旦 handler 尾阻塞
    （`MAILBOX_CTRL_LISTEN_BLOCK`：`thread_block()` 内部已把 CR3 切到下一个线程/进程），handler 的
    C 尾部继续执行（`mailbox_exec` 的 `return config->ret`、`syscall_dispatch` 的
    `copy_to_user/kfree(kbuf)`），并在**错误 CR3** 下访问调用者栈上的 config（用户栈可能落在
    0x19xxxxx 内核带，仅本进程 CR3 可见）→ **#PF**（CR2 = config+偏移，如 `0x01981F94`）。
    复现（修复前 100%）：`process_test` 菜单 **9**（ipc_bench，源码现已并入 process_test.elf）第一次运行即崩。
    **修复**（本次已落地）：
    - `gate_caller_ring3()` 改读**保存帧的 CS**（`curr_task_ctx->regs->cs & 3`，`arch_task.h`）→ kbuf 路径真正生效；
    - 小配置的 kbuf 改为 kernel `.bss` 静态缓冲 `syscall_kbuf[512]`：低端内存在**任何 CR3** 下都映射，
      而新 kmalloc 的堆页在“进程创建时克隆过内核 PDE”的旧页目录里可能不存在；
    - handler 前后**若调用者被切走则跳过 `copy_to_user` 与 `kfree`**：原始判据是 `arch_get_cr3()`
      是否变化；**lazy CR3（2026-09-13）后改为 `thread_get_tid()` 前后比较**（切换在 `thread_block()`
      时就登记，地址空间要到门出口才真正换，CR3 比较保留作兜底）——不得在调用者已被切走/正在退出时
      触碰其地址空间或堆缓冲。
    验证：修复后菜单 9 完整跑通（mailbox ~1.3 ms/往返、portal ~105 µs、“portal ~13x faster”）。
    > **补记（2026-09-13，lazy CR3）**：上下文切换不再在 `thread_block()` 内立即换 CR3。调度器经
    > `request_switch_context()` 仅记录 {vcb, next ctx}，由门出口的 `arch_task_context_switch()`
    > 统一提交（syscall 出口的 EAX 交付 2026-09-14 改为「结果停放保存帧 + 全量恢复」——旧的
    > push/pop + KEEP_EAX 方案在切换线程时会把调用方返回值带给被恢复的线程，见 GOTCHA 13；
    > `proc_exit_internal` 自退在销毁 vcb 前显式提交）。因此 handler 的 C 尾部始终运行在**调用者自己的
    > CR3** 上，上文“尾阻塞后 config 在别的 CR3 下不可访问”的推理对当前内核已不成立；kbuf 用 `.bss`、
    > 尾部只用缓存局部量等做法保留为跨设计/跨时序都成立的保守选择。
12. **【已修复 2026-09-13】线程风暴 + 键盘活动 → `thread_create` 内 `pmm_alloc_pages` 清零页时写 #PF**：
    现象（修复前 3/3 复现，≤90s）：`process_test` 菜单 **1**（thread_api_test 的线程 create/block/unblock/exit
    风暴）叠加键盘输入时，内核在 `thread_create_ex_internal → arch_task_context_init → vmm_alloc_pages →
    pmm_alloc_pages` 的 **`memset`（清零新分配页）** 处写 fault（CR2 ≈ `0x01983000`，Error = `0x2`
    写/non-present/kernel-mode）。
    **根因（QEMU monitor 现场取证：dump 出错 CR3 的 PDE/PTE）**：
    - `vmm_alloc_pages` 空树回退 `va = pa` 把**用户线程栈放进了内核恒等带**（首分配≈PMM 首个空闲页，
      后续 gap 顺延仍在 `0x19xxxxx`）；gap 分配下 VA 也常与 PA 恰同值；
    - **内核带里 VA 的 PTE 同时就是恒等映射**：线程退出 `vmm_free_pages` → `arch_unmap_4kb` 清掉该 PTE，
      等价于在本进程 PD 里**删除了该物理页的恒等映射**（现场：PT idx384/385 活栈 user PTE、idx386/387
      被清 0、idx388+ 为拆分时铺的 supervisor 恒等项）；
    - 该物理页被 `pmm_alloc_pages` 回收再分发，其**清零 memset 走恒等地址**（`memset(pa)`）——当前 CR3
      （调用者自己的 PD）里该恒等 PTE 已被删 → **non-present 写 #PF**。概率性 = PMM 回收顺序 ×
      线程风暴/键盘时序的耦合。
    **修复（本次已落地）**：
    - `arch/i386/paging.h` 新增 `USER_ANON_BASE = 0xD0000000`（user ELF 之上、mmio 之下）；
      `vmm_alloc_pages` 空树回退由 `va=pa` 改锚定此基址——**用户 VA 永不进入内核恒等带**；
    - `arch_unmap_4kb` 防御：`va < USER_HEAP_BASE` 时不清理、**恢复为 supervisor 恒等 PTE**
      （内核带的 PTE 就是恒等映射，任何清理都是打洞）；
    - 回归修复：栈移到高 VA 后，`arch_task_context_init` 写初始 regs 帧不能再走 VA——boot 时内核线程
      创建 ELF 进程首线程，**当前 CR3 不是新进程的 PD**；改走**物理别名**写入（与 `elf_load` 拷贝段内容
      同一规则），新增 `vmm_vcb_va_to_pa()`。
    **验证**：风暴+键盘 90s ×3 轮全绿（修复前 ≤90s 必崩）；12 线程启动、菜单渲染、portal 测试 PASS、
    选项 1（风暴）与选项 9（ipc-bench：mailbox 58µs / portal 103µs）完整跑通；复现脚本
    `tools/gp_repro.py`。
    （历史同族 #GP：`arch_syscall_entry` iret 保存帧被毁——疑似同一“恒等带被洞穿”族类，修复后可再复测。
    **2026-09-14 复测结论：该族 #GP 仍在发生（≈7% 的启动命中，见 GOTCHA 13）；取证显示与恒等带无关，
    而是用户线程的保存帧/返回值在「中断-恢复」边界上被错传。**）
13. **【已修复 2026-09-14】启动期 #GP 竞态：ring3 上执行了裸 `in/out` → #GP(0) → 整机停机（修复前 ≈7%）**：
    现象（修复前）：无头 QEMU 启动采样（82 次，以每组并行 12 个 × 40 s 为主），**6 次（≈7%）在启动后
    10–40 s 内打印 `Exception: 13  #GP`、Error Code `00000000`，EIP 落在某个 server ELF 的
    `iowrite8`/`ioread8` 裸 I/O 指令上（CS=0x07、IOPL=0，CPU 行为正确），随后 `System halted.`**。
    命中过的进程与指令：rtc_server 启动 TSC 校准期读 CMOS（`out 0x70` / `in 0x71`）、pit_ramp 写 PIT
    命令口（`out 0x43`）——都来自 `user/server/user_service.c` 的「`!arch_running_ring3()` → 裸 in/out」
    分支（该分支本为已废除的「ring0 用户 handler」设计留的后路，理论上永远不该走到）。
    取证（临时插桩，已回退）：裸分支入口往共享 user-heap 写诊断块（活 CS、当场重查
    `arch_running_ring3()`、参数、返回地址、栈顶 16 字），异常 handler 打印。结果：裸分支确实在
    `cs=7` 时进入；「最后一次 ring3 检查」记录为 **result=1（却仍走裸分支）×2**、
    **result=0（同一包装里刚读到 cs=7）×1** ——检查的返回值在调用-中断-抢占-恢复边界上被错传。
    **根因（已确诊）**：`arch_syscall_entry` 出口原用 `RESTORE_REGS_KEEP_EAX` 把本 syscall 的返回值
    留在 EAX 里跨过出口恢复；但同一出口**切换到别的线程**时会弹出**目标线程**的保存帧——跳过了目标的
    EAX 槽，于是**被恢复的线程拿到"上一个线程的 syscall 返回值"当自己的 EAX**。若它恰在
    `arch_running_ring3()` 检查附近被 PIT 抢占、又经由某个阻塞系统调用的出口恢复，检查/分支就会用错误
    的 EAX 求值 → 走到环 3 的裸 `in/out` → #GP。此即 GOTCHA 11 尾注「历史同族 #GP（iret 保存帧被毁）」
    的真相。
    **修复（本次已落地）**：
    - `arch/i386/irq.S`：syscall 出口把 dispatch 结果**写回调用者保存帧的 eax 槽**（`44(%esi)`，
      offsetof(regs,eax)），出口恢复统一改用**全量 `popal` 恢复**——不切换：恢复的就是本帧，EAX=结果；
      切换：恢复目标线程自己的完整帧（含它被抢占时的 EAX）。阻塞（tail-block/yield）系统调用的返回值
      也因此被正确"停放"、在恢复时交付（两段式 wrapper 本就不依赖它，语义更干净）。
      `proc_exit_internal` 自退会在 handler 内提前提交切换，出口写入前用「entry ctx == curr_task_ctx」
      守卫跳过（否则会写坏下一个线程的帧）。`RESTORE_REGS_KEEP_EAX` 宏删除。
    - `user/server/user_service.c`：删掉裸 in/out 分支，**所有用户 ELF 的端口 I/O 无条件走 `SYSCALL_IO`
      门**（无任何合法使用者），彻底消除崩点。
    **验证（QEMU 无头）**：修复后 **72 次启动采样 0 崩溃**（修复前同条件 ≈7%）；菜单 [9]（mailbox 41µs /
    portal 77µs）、菜单 [0] 图形往返 ×3、菜单 [1] 线程风暴交互全部通过。
    复现配方（历史，多跑几组可命中修复前的概率）：`for i in $(seq 1 12); do timeout 40 qemu-system-i386
    -cdrom output/myos.iso -display none -serial file:/tmp/r$i.log & done; wait; grep -l "KERNEL EXCEPTION"
    /tmp/r*.log`。

### 2.8 地址空间访问范围审计（2026-09 · 更新于低 16MB 隔离后）

> 现状快照：**低 16MB 恒等映射 = `PTE_KERNEL`（2026-09 已隔离）**。内核镜像/堆/页表池/位图全部只对 ring0
> 可见；ring3 能访问的只有 per-process `PTE_USER` 映射。用户高区：共享 user-heap `[0xC0000000,0xC1000000)`，
> 用户 ELF 链接基址 `0xC1000000`（`user/user.ld`），线程栈锚点 `USER_ANON_BASE = 0xD0000000`，
> mmio 固定 VA `0xE0000000/0xE0010000`，`USER_SPACE_TOP = 0xF0000000`。

**Ring0（内核）访问的地址范围**（低地址恒等映射；用户 CR3 下仍以 supervisor 访问）

| 范围 | 内容 | 说明 |
|------|------|------|
| `0x00100000 ~ 0x00115420` | 内核 text/rodata/data | 1MB 起，恒等 |
| `0x00115420 ~ 0x01115420` | 内核堆池 16MB（kmalloc）| `heap.c` 静态 `krn_heap`，supervisor-only |
| `0x01122000 ~ 0x01922000` | 页表/页目录池 8MB | supervisor-only |
| `0x01922000 ~ 0x01942000` | PMM 位图 128KB | supervisor-only |
| `0x01942000`（`__kernel_end`）之后 | GRUB 模块区 | supervisor-only，`pmm_mark_used` 保留 |
| `0x00000000 ~ 64MB` | 全部物理（恒等）| ring0 直接可达 |
| `0x000B8000` / `0x000A0000` | VGA 文本 / mode-13 fb | supervisor（ring3 经 mmio 高 VA 别名访问）|

**Ring3（用户）访问的地址范围**

| 范围 | 内容 | 谁 |
|------|------|------|
| `0xC1000000 ~ +size` | ELF 代码/数据（per-process `PTE_USER`）| 所有 user server/demo |
| `0xD0000000 ~ 0xE0000000` | **线程栈**（`vmm_alloc_pages`，`USER_ANON_BASE` 起向上，含主线程）| 各进程 |
| `0xC0000000 ~ 0xC1000000` | **共享 user-heap** 16MB（所有进程同一物理页；`SYSCALL_HEAP` malloc、mail 对象）| 所有进程 |
| `0xE0000000` / `0xE0010000` | VGA 文本 / mode-13 fb 的 MMIO 高 VA 别名（`SYSCALL_MMIO` + `CAP_MAP_MEM`）| terminal_server |
| `0x00000000 ~ 0x00FFFFFF` | **不可达**（supervisor）| —— |
| 端口 `0x3C0-0x3DF`/`0x3F8`/`0x60-0x64`/`0x70-0x71`/`0x40-0x43` | io syscall（`CAP_ACCESS_IO`）| terminal/kb/log/rtc/pit_ramp（`0x40-0x43` 仅 pit_ramp，rtc 已收回） |

**要点**：
- **用户 VA 永不放进内核恒等带** `[0, 64MB)`：内核靠恒等映射访问物理页（pmm 清零、syscall kbuf、
  `elf_load` 拷贝段内容），用户映射一旦落在带内，其 unmap 会删掉同物理页的恒等 PTE（第 12 条）；
  `arch_unmap_4kb` 对 `< USER_HEAP_BASE` 的清理请求只恢复 supervisor 恒等 PTE，绝不删除。
- mail 对象在共享 user-heap（ring3 可读写 payload），`mailmeta`/`mailbox`/全部内核簿记在 supervisor 内核堆；
  ring3 已无任何「指向内核对象的指针」可解引用（syscall 句柄全为整数 / tid / `mb==NULL`）。
- 原「对方案①（P3）最要紧的三条 ring3 低地址访问路径」已全部解除：VGA 走 mmio 高 VA；mailbox 视图已迁
  用户堆（mail/meta 拆分）；主线程栈高区化亦已完成（见下「剩余」第 1 条，2026-09-13）。

**剩余（低 16MB 隔离之外，独立存在）**：
1. **（已消除 2026-09-13）用户栈落低物理页**：`USER_ANON_BASE=0xD0000000` 后所有用户线程栈都锚定在
   高区；`split_4mb_pde` 亦已修（兄弟 PTE 保留 supervisor、仅目标页 user），用户 VA 不再触碰内核恒等带。
2. **共享 user-heap 无进程隔离**：`[0xC0000000,0xC1000000)` 所有进程共享同一物理页（mail 传递依赖它），
   ring3 进程间可互读/互写堆对象——若做 per-process 隔离需改 mailbox 数据通路。
3. （已不需要）低 1MB 保留 VGA 用户窗口 / mailbox 内核堆视图——已由 mmio 高 VA + mail/meta 拆分替代。

---

## 三、向微内核演进的路线图

> 原则不变：**只有需要 CPU 特权级的代码留在内核**；把调度时钟留内核；共享内存优先；逐步迁移、每步可运行。

### P0 — 事件推送（mailbox 的「另一半」）⭐ 最高优先

**目标**：让 server 能把类型化事件推给 N 个订阅者（键盘 → 游戏）。

**已完成（2026-09）——事件推送全链路（除游戏 consumer）；阻塞 listen 已落地**：
- 内核（`kernel/ipc/mailbox.c`）：订阅注册表 + 广播按 magic 过滤；`magic == 0` 永不匹配；引用计数精确统计；
  SUBSCRIBE/UNSUBSCRIBE 支持 `mb == NULL`（自己的 mailbox）
- ✅ **阻塞 listen（2026-09）**：mailbox 增加共享 `mb->sp_lock` 的 `waiters`；新命令 `MAILBOX_CTRL_LISTEN_BLOCK`
  （=10）在 `mailbox_exec` 尾阻塞（`wait_queue_sleep_locked`）；`send_mail` 入队 / 广播 clone 后唤醒投递
  （普通 `wake_one`，及持 `schedule_lock` 时的 `wake_one_locked` + 新增 `thread_unblock_locked` 无锁变体）。
  benchmark mailbox 往返 ~178ms → ~0.9ms →（2026-09-14 实测）~49–58 µs/往返（见 §五.2）
- userlib：`user_mail_alloc/send/subscribe/unsubscribe/listen/release` + `user_mail` 视图 + `USER_MAIL_ANY_TID`；
  `user_mail_listen`/`user_irq_wait` 两段式（`LISTEN_BLOCK` → `LISTEN`，两段之间必须把回写的 `cfg.mb` 清零——
  ring3 gate 拒非空 mb）
- 事件源（`user/server/input/kb_server.c`）：IRQ1 scancode → `MSG_KEY_EVENT` 广播，可打印键兼写 COM1
- consumer 样板（`user/server/display/terminal_server.c`）：开用户线程 ns_lookup("kb") 拿 magic →
  `user_mail_subscribe` → `user_mail_listen` 过滤回显到屏幕；共享头 `user/server/server_msgs.h`（`MSG_KEY_EVENT` + `key_event`）

**剩余**：

| 子项 | 内容 | 位置 |
|------|------|------|
| 1. 游戏 consumer | 游戏 `user_mail_subscribe(MSG_KEY_EVENT)` + `user_mail_listen`（阻塞收信，随 P1 demo 一起做） | `user/demo/*` |
| 2.（可选）扩展键 | kb 处理 E0 前缀方向键 / shift 状态 | `user/server/input/kb_server.c` |

> 不做：`mailbox_call`（portal 已覆盖同步 RPC）。

### P0.5 — 在案缺陷修复（2026-09-14 新增 · ✅ 当日全部完成）

| 子项 | 内容 | 位置 |
|------|------|------|
| 1. ✅ 启动期 #GP 竞态（已完成） | 根因确诊并修复：syscall 出口旧 `RESTORE_REGS_KEEP_EAX` 在切换线程时把调用方返回值带给了被恢复的线程（后者拿到错误的 EAX）；改为「结果停放保存帧的 eax 槽 + 出口全量 `popal` 恢复」，并删除 `user_service.c` 裸 I/O 分支。验证：0/72 启动崩溃 | `arch/i386/irq.S`、`user/server/user_service.c` |
| 2. ✅ mode-0x13 字体恢复（已完成） | 惰性字体快照：首次 `gfx_enter_mode13()` 开头保存 plane 2（boot 已结束、由 g_vga_lock 互斥），`gfx_enter_text()` 在清屏前回载（`sr[4]`=0x06 → 0x02）。验证：连续 3 次往返截屏字形完好 | `user/server/display/terminal_server.c` |

> 两项合计 ~1 天完成（含取证与 72 次启动回归采样），按计划先于 P1 的游戏移植处理完毕。

### P1 — Demo 独立运行 ⭐

**目标**：`user/demo/` 从「引用不存在头文件的死代码」变成真正跑起来的独立进程。

| 子项 | 内容 | 位置 |
|------|------|------|
| 1. ✅ 图形服务（已落地，terminal server 兼任） | terminal_server 解析 `ESC 'G'` 控制帧：`GFX_CTRL_SET_MODE` 用 VGA 寄存器组切 mode 0x13/0x03（0x13 寄存器表同标准 VGA，端口都在 `{0x3C0,32}` grant 内）；`GFX_CTRL_BLIT` 把客户端帧缓冲拷到 0xA0000。⚠️ 回文本后字体不恢复（GOTCHA 9） | `user/server/display/terminal_server.c` |
| 2. ✅ 帧缓冲传送（已落地） | userlib `gfx_set_mode/gfx_blit_shared`（shm_share 静态 header+fb 一次调用，非每次画点 IPC）；demo_common 提供 `gfx_fb` + `gfx_clear_screen/put_pixel/fill_rect/flush` | `user/userlib.c`、`user/demo/demo_common.c` |
| 3. ✅ 命名服务（已落地，用户态版） | `ns_register` / `ns_lookup` → {portal_id, mailbox_tid, mail_magic}（`user/server/ns/namespace_server.c` + `user/ns_proto.h`，固定 `PORTAL_ID_NAMESPACE`） | 用户态 server，非内核 syscall |
| 4. ✅ timer / sleep（已落地，用户态版） | rtc server `RTC_CMD_SLEEP_MS`：**TSC deadline 自旋**（TSC 对 RTC 1 Hz 秒沿校准，分频无关）；未加内核 `sys_sleep_ms` | `user/server/clock/rtc_server.c` |
| 5. demo 重写 | 去掉 `drivers/*.h`，改用 `userlib` + portal（console）+ mailbox（按键事件） | 🟡 部分：process_test.elf 已跑 4 套件（thread/process/rbtree/sched_mix）+ 图形 demo + ipc_bench（菜单 9）；mailbox/shm 套件仍引用 kernel-only API（菜单里注明 not yet ported）；airplane/snake/breakout + games_entry 仍是死代码（引用已删的 `drivers/*.h`，makefile 未构建） |
| 6. 构建 | makefile 加 demo ELF target + grub.cfg 加载 | ✅ process_test.elf target + grub.cfg 已加 |

### P2 — 服务补齐

| 子项 | 内容 | 位置 |
|------|------|------|
| 1. ✅ RTC server（已落地） | `rtc_server`：`CAP_ACCESS_IO(0x70-0x71)`（仅 CMOS；PIT 已收回），portal 服务注册 "rtc"（GET_TIME / SLEEP_MS，同 log/terminal 模式） | `user/server/clock/` |
| 2. ✅ 清理死代码（已落地） | `include/kernel/driver.h` / `device.h` 已删（2026-09）；`user/demo/` 旧源文件在新版跑通后替换 | — |
| 3. 用户态驱动 API | 若 demo 需要 `gfx_*` / `kb_poll` / `timer_*`，在 `user/server/server_msgs.h` 旁建一份用户侧 API 头，替代已删的 `drivers/*.h` | `user/` |

### P3 — 地址空间隔离（第 0 步遗留 · 主体已落地 2026-09）

**已完成（2026-09）**：
1. 低 16MB 恒等映射改 `PTE_KERNEL`（`arch/i386/paging.c`）——ring3 不再能读写内核 text/堆/页表；
   `copy_*_user` 页表 walk 自动拒绝内核地址（原「无下界」问题关闭）。
2. 三个前置阻塞逐项解除：mail/mailmeta 拆分（mail 迁共享 user-heap、内核 inflight 注册表反查）；
   `SYSCALL_MMIO` 高 VA 映射承担 VGA（fixed + `own_phys=0`，`vmm_map_fixed` 加 own_phys 参数并新增
   `vmm_unmap_fixed`）；mailbox gate 拒 ring3 非空 `mb`。

**剩余工作**：

| 子项 | 内容 | 位置 |
|------|------|------|
| 1.（已消除 2026-09-13）用户栈高区化 | `USER_ANON_BASE=0xD0000000` 锚定所有用户线程栈；`split_4mb_pde` 亦已修（只放开目标 PTE、兄弟保持 supervisor）→ 用户 VA 不再触碰内核恒等带 | `kernel/mm/vmm.c`、`arch/i386/task.c`、`arch/i386/paging.c` |
| 2. per-process user heap（可选）| `[0xC0000000,0xC1000000)` 现为所有进程共享同一物理页（mail 传递依赖）；若需进程间堆隔离，需改 mailbox 数据通路 | `kernel/mm/heap.c`、`kernel/ipc/mailbox.c` |
| 3.（已不需要）低 1MB VGA 用户窗口 / mailbox 内核堆视图 | 已由 mmio 高 VA + mail/meta 拆分替代 | — |

---

## 四、工作量估算

| 阶段 | 内容 | 预计 | 难度 | 主要文件 |
|------|------|------|------|---------|
| P0（除游戏 consumer 已完成） | 游戏 consumer（随 P1 demo） | 半天 | ⭐⭐ | `user/demo/*` |
| P0.5（✅ 已完成） | 修 #GP 竞态（根因确诊 + 修复）+ mode-0x13 字体惰性快照 | 已完成（~1 天） | ⭐⭐⭐ | `arch/i386/irq.S`、`user/server/user_service.c`、`user/server/display/terminal_server.c` |
| P1 | 游戏 demo 上共享帧缓冲（graphics/timer/命名服务已完成） | 2-4 天 | ⭐⭐⭐ | `user/demo/*`、`user/server/display/` |
| P2 | 清理（RTC + log server 已接线） | ~半天 | ⭐⭐ | `user/server/`、`makefile` |
| P3（主体已落地） | 剩余（可选）：per-process user heap | ~1 天 | ⭐⭐⭐ | `kernel/mm/heap.c`、`kernel/ipc/mailbox.c` |
| **总计** | | **2-3 周** | | |

---

## 五、重要提醒

### 1. 微内核的本质是用 IPC 开销换隔离性
- **不要追求纯微内核**：调度时钟（IRQ0）留内核；高频路径（按键轮询）别走 portal。
- **共享内存优先**：帧缓冲一次 `shm_share`，不要每次画点发 IPC。
- **批量处理**：能合并成一次 IPC 的操作合并。

### 2. 先 benchmark，再优化（2026-09 已做；2026-09-14 复测更新）

`user/ipc_bench.c` 用 ring-3 `rdtsc`（CR4.TSD 未置，可读）计时，先经 rtc server 的 `SLEEP_MS`
校准 TSC 频率，再测两种往返。它已并入 process_test.elf，从**菜单 [9]**触发（不再是独立 ELF）。

**2026-09-14 实测**（HEAD `c0905e4`，PIT 1000 Hz，QEMU 无头；TSC ≈ 2.4 GHz）：

| 路径 | 平均往返 | 说明 |
|------|---------|------|
| **mailbox**（同进程双线程定向 mail + 阻塞 listen）| ~49–58 µs | 8 轮平均；投递直接唤醒，不等 tick |
| **portal**（跨进程 rtc GET_TIME RPC）| ~91–103 µs | 40 轮平均；含 shm_share + 信号量往返 |

**含义**：两条路径都在 0.1 ms 量级，且 **mailbox 已反超 portal**（此前“portal ~13x faster”的对比已过时）。
历史：mailbox 原先 ~178ms 的瓶颈 = 用户侧 `LISTEN + user_yield()` 忙等轮询（唤醒要等下个 PIT tick），
不是内核 mailbox 本身；内核 tail-block 的 `MAILBOX_CTRL_LISTEN_BLOCK`（mailbox 加 `waiters`，投递直接
唤醒）落地后先降到 ~0.9ms，再随 tick/调度改进到现在的 ~50 µs 量级。

⚠️ 旧注意事项已作废：以前测 bench 需先在 `init.c` 禁用 hello.elf（它的 2 s rtc 睡眠会独占单线程
rtc server——**hello.elf 已移出启动集**，不再有该干扰）。ipc_bench 末尾的比率行 2026-09-14 已改为
**双向显示**（谁快报谁，修复此前 portal 更慢时整数除法显示 `~0x` 的小瑕疵）。

**改动调度/唤醒/门路径后应重跑菜单 [9]，以实测数字为准。**

### 3. 逐步迁移，保持可运行
每一步完成都应有一个可启动、可演示的版本：
- ✅ 现在：内核 + terminal/kb/portal 三个 server 可启动，console 可打印，键盘有 COM1 诊断
- ✅ 2026-09：mailbox 订阅/广播 + kb_server 广播 + terminal 按键回显（P0 主线完成）
- ✅ 2026-09：namespace server —— 唯一固定 portal `PORTAL_ID_NAMESPACE`；console/log 改动态 id 并注册，客户端 `ns_lookup` 解析（console_putstr / user_log_write / 按键订阅）
- ✅ 2026-09：process_test.elf（demo 测试菜单）纳入 ISO 构建；demo_common 等完成 namespace 适配
- ✅ 2026-09：修复 mailbox 广播 —— `MAIL_ANY_TID`/`USER_MAIL_ANY_TID` 不一致导致按键广播被当点对点丢弃
- ✅ 2026-09：rtc_server.elf —— 用户态 RTC/sleep portal 服务（"rtc"）；demo `timer_delay_ms` 走 SLEEP_MS RPC，demo 进程的 PIT/PPI/CMOS 端口能力已收回
- ✅ 2026-09：terminal_server 图形模式 —— `ESC 'G'` 控制帧切 mode 0x13 + BLIT；process_test 菜单 `[0]` 弹跳方块 demo（250 帧 @20ms，rtc sleep 驱动帧率，结束回文本）；另修复 **PMM 未保留 GRUB 模块区** 导致 ~1MB 模块被启动期分配清零、elf 校验失败的问题（`pmm_mark_used`）。⚠️ 但 `04e452d` 因启动并发把字体快照/恢复整体移除后未补回——**mode 0x13 回文本后字形乱码**（2026-09-14 复测确认，见 GOTCHA 9 / P0.5）
- ✅ 2026-09：mailbox mail/mailmeta 拆分 —— mail 迁共享 user-heap（ring3 不再持有内核簿记指针）、内核 inflight 注册表按 payload 反查、gate 拒 ring3 非空 `mb`
- ✅ 2026-09：`SYSCALL_HEAP`（共享 user-heap malloc/free）+ `SYSCALL_MMIO`（`kernel/mmio.c`）—— terminal VGA 经 mmio 映射到高 VA（0xE0000000/0xE0010000），低地址回退移除
- ✅ 2026-09：低 16MB 改 `PTE_KERNEL` —— ring3 不再可达内核 text/堆/页表（P3 主体落地，`copy_*_user` 语义干净）
- ✅ 2026-09：清理死代码 —— 删除 `include/kernel/driver.h` / `device.h`（platform_bus 残留）
- ✅ 2026-09：IPC ping-pong benchmark（TSC 计时，日志可读化：千位分隔/单位/对比；现为 process_test 菜单 9）——
  portal RPC ~144µs 达标；mailbox 往返 ~178ms → 阻塞 listen 后 ~0.9ms →（2026-09-14）~49µs（见 §五.2）
- ✅ 2026-09：mailbox **阻塞 listen**（`MAILBOX_CTRL_LISTEN_BLOCK` + 共享 sp_lock 的 waiters + 投递唤醒 +
  `wake_one_locked`/`thread_unblock_locked`）—— mailbox 往返 ~178ms → ~0.9ms → ~49µs
- ✅ 2026-09-12：`pit_ramp.elf` 首进程 + PIT 爬坡到 1000 Hz；rtc_server 改 **TSC deadline 睡眠**（不再持有
  PIT）；hello.elf 移出启动集；ipc_bench 并入 process_test 菜单 9（`3cbbc74`）
- ✅ 2026-09-13：lazy CR3 切换（切换点只登记、门出口统一提交）；删除内核线程化 IRQ（ring-3 可达的
  CPL0 回调风险）；线程风暴 #PF 修复（`USER_ANON_BASE=0xD0000000` 锚定用户栈 + `arch_unmap_4kb`
  恒等带防御）（`c0905e4`）
- ✅ 2026-09-14：修复启动期 #GP 竞态（根因：syscall 出口的 EAX 交付在切换线程时把调用方的返回值带给
  了被恢复的线程；改为「结果停放保存帧 + 全量恢复」，并删除 `user_service.c` 的裸 I/O 分支）——
  修复前 82 次启动 ≈7% 崩溃，**修复后 72 次启动 0 崩溃**（见 GOTCHA 13）
- ✅ 2026-09-14：修复 mode-0x13 字体回归（惰性快照 plane 2 + 回文本重载 + `sr[4]`=0x06，见 GOTCHA 9）——
  图形 demo 连续 3 次往返后截屏字形全部正确
- ✅ 2026-09-14：ipc_bench 比率行改为双向显示（mailbox/portal 谁快报谁）
- ✅ 2026-09-13（P3 剩余）：用户栈高区化完成（`USER_ANON_BASE` 锚定）；剩余（可选）：per-process user heap
- P0 后：键盘事件能广播、游戏能收到（游戏 consumer 随 P1 demo）
- P1 后：airplane/snake 作为独立进程在共享帧缓冲上运行
- P2 后：死代码清理完毕（RTC 已可查时间，LOG 已走用户态 log server）
- ✅ 2026-09（P3 主体）：用户地址空间与内核低端隔离（低 16MB = `PTE_KERNEL`），`copy_*_user` 语义干净；剩余（可选）：per-process user heap

**永远不要一次性改完所有东西再测试。**
