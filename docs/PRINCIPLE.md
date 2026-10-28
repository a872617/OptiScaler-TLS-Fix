# 技术细节：OptiScaler 启动崩溃的排查与通用修补

> 这份文档记录完整的排查过程，方便遇到类似问题的同学少走弯路。
> 作者：鱼加面汉化

---

## 1. 一开始的现象

游戏（燕云十六声，`yysls.exe` / `WWM.exe`）放入 OptiScaler 代理 DLL 后：

- 大多数情况：启动即崩，事件查看器 `Application Error 1000`
- 少数情况：窗口出来但未响应（`Application Hang 1002`）

崩溃签名（不同 OptiScaler 版本偏移不同）：

| 版本 | 模块 | 异常码 | 偏移 |
|---|---|---|---|
| v0.10 特殊构建 | WINMM.dll | 0xc0000005 | 0x2bbda |
| 0.9.4.0 | WINMM.dll | 0xc0000005 | 0x73341 |
| 0.9.2.1 | WINMM.dll | 0xc0000005 | 0x6b961 |
| 0.9.1.0 | WINMM.dll | 0xc0000005 | 0x6b961 附近 |

**偏移每次都不同，但都是 `WINMM.dll`、都是 `c0000005`。**

---

## 2. 读崩溃转储

用 `MiniDumpWriteDump` 抓全内存转储，解析后得到：

- 异常地址 = `winmm.dll + 0x2bbda`
- `ExceptionInformation` = 读地址 **0x78**
- 崩溃瞬间只有 ~88 个模块，**连 `d3d12.dll` 都没加载**
- 栈上是 `ntdll` 的加载器函数（`LdrpCallInitRoutine` 一类）

结论：崩溃发生在 **DLL 初始化阶段**，不是渲染中。

---

## 3. 反汇编：一条 TLS 访问

崩溃点前后的机器码（v0.10）：

```asm
2bbc6:  mov  ecx, dword ptr [rip+0x1926c28]   ; _tls_index
2bbcc:  mov  rax, qword ptr gs:[58h]          ; TEB->ThreadLocalStoragePointer
2bbd5:  mov  edx, 0x14
2bbda:  mov  rax, [rax+rcx*8]                 ; <-- 崩在这里
2bbde:  mov  ecx, [rdx+rax]                   ; 读 TLS_block + 0x14
```

这是 MSVC 访问 `__declspec(thread)` 变量的标准序列。崩溃时：

- `rax`（TLS 数组指针）= **0**
- `rcx`（`_tls_index`）= **15**
- `0 + 15*8 = 0x78` ← 和事件日志的"读地址 0x78"完全对上

---

## 4. 关键实验：不装插件也有这种线程

在**完全没有 OptiScaler**的情况下启动游戏（正常进到主界面），
用 `NtQueryInformationThread` + `ReadProcessMemory` 读每个线程的 TEB：

| 线程 | TEB | TLS 指针 |
|---|---|---|
| 12844 | 25D000 | 0x5FDC3CC0 ✅ |
| …（154 个） | | 正常 |
| 11640 | 25F000 | **0x0** ❌ |
| 2268 | 261000 | **0x0** ❌ |
| 5824 | 263000 | **0x0** ❌ |

这 3 个线程的起始地址全部是 `ntdll.dll+0x51A00`（`TppWorkerThread`，线程池 worker）。

**说明：这种"没有 TLS 的线程"是游戏/系统自己产生的，跟 OptiScaler 无关。**
OptiScaler 只是运气不好，在 DllMain 里访问了 thread_local。

---

## 5. 为什么别人能跑、换注入名结果不同

- 同一个 DLL，被**普通线程**加载 → TLS 正常 → 不崩
- 被**没有 TLS 的线程**加载 → 崩

而"被谁加载"取决于：

1. 代理文件名（`winmm.dll` 是 exe 的静态导入，进程初始化最早期就加载；
   `d3d12.dll` / `dxgi.dll` 加载时机不同）
2. 客户端版本（国服 / 国际服 exe 导入表不同）
3. Windows 版本与加载器时序
4. 机器上运行的其他驱动/组件

这解释了为什么"同一份文件在别人电脑、虚拟机、Sandboxie 里正常"。

---

## 6. 三种补丁方案（我们试过的）

| 方案 | 做法 | 结果 |
|---|---|---|
| 单点补丁 | 给崩溃那一处加判空 | v0.10 可用，换版本就失效 |
| 形态匹配 | 匹配 `mov edx,14h` / `mov r9d,14h` 等形态 | 覆盖一部分，0.9.1 全是第三种写法，全漏 |
| **通用补丁（本工具）** | 改 `mov rax, gs:[58h]` 本身 + 共享 helper | **任何版本都适用** |

### 通用补丁的 helper

```
pop  r11              ; 取返回地址
add  r11, 4           ; -> 原指令之后 (site+9)
push r11              ; 返回地址留在栈上
push rcx / push rdx   ; 保存会被用到的寄存器
mov  rax, gs:[58h]
test rax, rax
jnz  done
  ; TLS 为空：初始化一次"假数组"（64 项，每项指向数组自身）
  lea  r11,[rip+arr] / lea rdx,[rip+arr] / mov ecx,64 / loop { mov [rdx],r11; add rdx,8 }
  lea  rax,[rip+arr]
done:
pop rdx / pop rcx
ret
```

- TLS 非空：行为和原指令完全一致
- TLS 为空：后续 `[rax+rcx*8]` 得到 `&arr`，`[reg+rax]` 读到数组内的 0，不再崩

补丁把每个 `mov rax, gs:[58h]`（9 字节）改成 `call helper` + 4 个 `NOP`，
并在 PE 末尾新增 `.osc` 节存放 helper 与假数组。

---

## 7. 已知限制

- 只适用于"TLS 为空"这一类崩溃；其他原因（钩子冲突、驱动拦截）不适用
- 对已打补丁的文件重复运行会拒绝（找不到原始指令）
- 需要 PE 头有空间新增节（极少见失败）
- 这是**本地绕过**，不是官方修复；OptiScaler 更新后需重新打补丁

---

## 8. 可复现的验证方法

1. 用 `procdump` 或 `MiniDumpWriteDump` 抓崩溃转储
2. 用 `winedbg` / `cdb -z dump -c "!analyze -v"` 看 `WINMM.dll+偏移`
3. 用 PE 工具在该 RVA 反汇编，找 `gs:[58h]` 与 `[rax+rcx*8]`
4. 若确认读地址是 `0x78`（或 `index*8`），基本就是本文这一类
