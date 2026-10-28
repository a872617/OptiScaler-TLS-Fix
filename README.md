# OptiScaler TLS Crash Fix

> 针对 **OptiScaler 0.9.x** 代理 DLL 启动崩溃的通用本地补丁。  
> A universal local patch for the **OptiScaler 0.9.x** startup crash.

> 📺 视频讲解（B 站）：https://www.bilibili.com/video/BV1bQsQ6dEPM/

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

---

## 症状 / Symptom

在游戏目录放入 OptiScaler 的代理 DLL（`winmm.dll` / `dxgi.dll` / `d3d12.dll` …）后，
游戏启动即崩溃，事件查看器里固定是：

```
出错模块：WINMM.dll
异常代码：0xc0000005
错误偏移：0x2bbda        (不同版本不同：0x6b961 / 0x73341 …)
读地址  ：0x78
```

部分版本不崩溃而是"窗口出来但未响应"。

---

## 根因 / Root cause

把崩溃点反汇编出来，是一条 `__declspec(thread)` 访问：

```asm
mov ecx, [_tls_index]
mov rax, gs:[58h]        ; TEB->ThreadLocalStoragePointer
mov rax, [rax+rcx*8]     ; <-- crash here
mov ecx, [rdx+rax]       ; read at TLS_block + 0x14
```

崩溃说明：**加载该 DLL 的线程，`ThreadLocalStoragePointer` 是 0**，
于是 `0 + 0x14*... = 0x78`，读取空指针直接访问违例。

实测（`yysls.exe`，未安装任何模组）：一个正常运行的进程里也存在
**3 个 TLS 指针为 0 的线程**，其起始地址全部是 `ntdll!TppWorkerThread`
（线程池 worker）。也就是说：

- 游戏没坏
- 插件没坏
- 是 **DLL 被加载的时机/线程** 与"TLS 尚未初始化"撞上了

这也解释了为什么同一份 DLL 在别人机器、虚拟机、沙盒里能跑，在这台机器上必崩。

---

## 为什么这个补丁与版本无关 / Why it is version-agnostic

常见做法是按指令形态打补丁（`mov edx,14h`、`mov r9d,14h` …），
但不同版本编译器写法不同，换个版本就失效（0.9.1 就是第三种写法）。

本补丁换了个思路：**直接改"读取 TLS 指针"的那一条指令本身**：

```asm
原：  65 48 8B 04 25 58 00 00 00     mov rax, gs:[58h]
新：  E8 xx xx xx xx  90 90 90 90     call helper + 4 x NOP
```

`helper` 的行为：

| 情况 | 行为 |
|---|---|
| TLS 指针有效 | 原样返回 `gs:[58h]`，后续代码行为完全不变 |
| TLS 指针为 0 | 返回一个"假 TLS 数组"（每项指向数组自身），后面的 `[rax+rcx*8]`、`[reg+rax]` 读出来都是 0，不再崩 |

因为**所有版本读 TLS 都是这一条指令**，所以与后续指令形态无关。

实现上会在 PE 文件末尾新增一个 4 KB 的节 `.osc`（可读可写可执行），
里面放 helper 代码、一个标志字节和 512 字节假数组；**原有代码段/数据段一律不动**。

---

## 使用方法 / Usage

1. 把 OptiScaler Client 生成的那个 DLL 放进本目录  
   （`dxgi.dll` / `winmm.dll` / `d3d12.dll` / `dbghelp.dll` / `version.dll` /
   `wininet.dll` / `winhttp.dll` / `OptiScaler.dll` 都支持）
2. 双击 `patch.bat`
3. 完成后文件名不变，原文件自动备份为 `<原名>.bak`
4. 把打好补丁的文件放回游戏目录

命令行也可以：

```bat
patcher.exe "winmm.dll" "winmm_patched.dll"
```

---

## 从源码构建 / Build

只需要 [TinyCC](https://bellard.org/tcc/)（约 1 MB，免安装）：

```bat
tcc -o patcher.exe src/patcher.c
```

源码只有一个文件 `src/patcher.c`，约 200 行，可直接审阅。

---

## 验证是否生效 / Verify

- 打补丁后文件应比原来大约 4 KB（新增的 `.osc` 节）
- 运行 `patcher.exe` 时有输出 `sites patched: N`
- 对已打过的文件再次运行会提示 `no mov rax,gs:[58h] found (already patched?)`

## 回滚 / Rollback

删掉游戏目录里那个代理 DLL 即可（游戏会用系统原版），
或用备份的 `<原名>.bak` 覆盖回去。

---

## 常见问题 / FAQ

**Q：会修改 OptiScaler 源文件吗？**  
A：不会。补丁只作用于你放进来的那份副本，原文件备份为 `.bak`。

**Q：为什么有的版本补完还是崩？**  
A：如果崩溃偏移落在 `.osc`（文件末尾新增那 4 KB）里，说明是 helper 的问题；
落在原代码段里，说明是别的机制（不是 TLS），需要单独分析。
把"错误偏移"贴到 issue 即可。

**Q：能用于联机游戏吗？**  
A：本工具只做本地二进制修补，不涉及内存注入或反作弊绕过。
是否使用、风险如何，请自行判断。

---

## 免责声明 / Disclaimer

- 本项目与 OptiScaler、游戏厂商**没有任何关系**，仅供学习与个人使用。
- 补丁会修改你本地的 DLL 文件，**请务必先备份**。
- 因使用本工具造成的任何问题（游戏崩溃、存档异常、账号风险等），作者不承担责任。
- 本项目**不包含** OptiScaler 本体或任何游戏文件，请自行从官方渠道获取。

---

## 作者 / Author

**鱼加面汉化** (YuJiaMian)

如果这个工具帮到了你，给个 Star 就是最大的支持。
