# 手写汇编取证——怪汇编可能是人写的，不是 C++ bug

## 一句话结论

逆向时看到"怪汇编"，先问一句：**这是 C++ 编译产物，还是人直接写的汇编？**
RA1/TD 源码（CnCRemastered，EA 2020 开源，GPL v3）给出源码级证据：
**Westwood 系统性使用手写汇编**——不是偶尔为之，是"能写汇编就写汇编"的团队文化。

## 源码级证据（`D:\CnCRemastered\SOURCECODE`）

### 独立 .asm 文件：27 个

覆盖范围（REDALERT 与 TIBERIANDAWN 各一套，Win32 移植还有 `WIN32LIB\` 专用版）：

| 领域 | 文件 | 说明 |
|---|---|---|
| 图形 | `COORDA.ASM` / `TOBUFF.ASM` / `REMAP.ASM` / `MMX.ASM` | 坐标变换、blit、调色板重映射、MMX 优化 |
| 压缩 | `LCWCOMP.ASM` / `LCWUNCMP.ASM` | **LCW 压缩/解压**（Westwood 自有格式，`.mix`/`.map` 解码相关） |
| 网络 | `IPXPROT.ASM` / `IPXREAL.ASM` | IPX 网络协议（联机底层） |
| 系统 | `CPUID.ASM` / `PAGFAULT.ASM` | CPU 检测、页面错误 |
| 文本 | `TXTPRNT.ASM` / `2TXTPRNT.ASM` | 文本打印 |
| 工具 | `SUPPORT.ASM` / `2SUPPORT.ASM` / `KEYFBUFF.ASM` | 通用支持例程、键盘缓冲 |

具体实例：

- `SUPPORT.ASM`（1993-09-23，Joe L. Bostic）：连 `strtrim`（字符串去尾空格）都手写汇编——
  这不是"性能热点才用汇编"，是**"能写汇编就写汇编"**；
- `COORDA.ASM`（1995-02-17，Barry W. Green）：`Cardinal_To_Fixed` 定点数转换（1/65536 定点），
  **除零默认返回 `0xFFFFFFFF`**——人写的"顺手防御"，编译器不会生成这种代码。

### 内联汇编：`REDALERT\MiscAsm.cpp` 41 处 `__asm`

典型 `Distance_Coord`（计算等距网格距离）：

```cpp
int Distance_Coord(COORDINATE coord1, COORDINATE coord2)
{
	__asm {
		mov	eax,[coord1]
		mov	ebx,[coord2]
		mov	dx,ax
		sub	dx,bx
		jg	okx
		neg	dx
		okx:
		shr	eax,16
		...
	}
}
```

**这个函数没有 return 语句**——返回值靠 EAX 隐式传递。坐标 32 位打包
（低 16 位 X、高 16 位 Y），所以全是 `shr 16` 拆分位操作。
这是"手写汇编在 C++ 里的真实样貌"。

## 手写汇编的怪癖特征（逆向识别清单）

1. **无 return 的伪函数**——Ghidra 反编译出"没有返回值、结尾怪怪的"C 函数，第一嫌疑；
2. **位操作当算术用**——坐标打包（高 16 位 Y / 低 16 位 X）、定点数（1/65536）、`shr` 代替 `div`；
3. **特殊的边界/默认值**——如除零返回 `0xFFFFFFFF`，人写的顺手防御，编译器不会生成；
4. **自定义调用约定痕迹**——`PROC Cardinal_To_Fixed C near` + `USES ebx, edx`
   （Borland/TASM 风格），寄存器保存规则人工管理；
5. **指令选择"有人味"**——人记得什么用什么，编译器不会这么选。

## 对 YR 逆向的意义

- **RA2 哪些层最可能继承手写汇编传统**：LCW 压缩解压（`.mix`/`.map` 解码相关！）、
  坐标/朝向变换（等距渲染）、IPX 网络协议（联机底层）——遇到这些领域的怪汇编，优先怀疑手写；
- **反编译伪代码会失真**：手写汇编反编译回 C 必然出现无 return、位操作、怪默认值。
  若假设"C++ 源码写成这样"，会误判为"原版 bug"——实际根本没有对应的 C++ 源码，它是直接写的 asm；
- **怪代码不需要解释得通，只需要对得上**——这是对"反编译伪代码不可信"的又一注脚。

## 本仓库方法论的免疫性

`emu_oracle` 对拍**只信机器码行为，从不假设伪代码的合理性**——不管怪汇编是 C++ bug
还是手写汇编，以机器码行为为准。三重验证（反编译 / 汇编 / PE 字节）的意义正在于此：
判断一段怪代码的来源，不如直接验证它的行为。

## 相关位置

- 取证源码：`D:\CnCRemastered\SOURCECODE\REDALERT\`（`COORDA.ASM` / `SUPPORT.ASM` / `MiscAsm.cpp`）
- 资源登记：`docs/references/cncremastered-source.md`（RA1↔YR 对照实现）
