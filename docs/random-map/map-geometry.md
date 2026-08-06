# 等距地图的几何：哪些格子存在、按什么顺序扫

> 状态：**格子集合、遍历顺序、总格数三件事已实锤并对拍**。
> 遗留 1 条未确认：格子指针数组 `MapClass +0x13C` 由谁分配。

之前每个阶段文档都在写"扫全图"、"对每个格子"，但从没定义过**全图到底是哪些格子**、
**扫的顺序是什么**。这两件事直接决定复刻成败：同一个随机数落到不同格子上，
从那一步起地图就散了。本文把它补上。

## 一、坐标系：等距坐标，打包成一个 dword

原版所有涉及格子的代码用的都是**等距坐标** `(X, Y)`，打包成 32 位：

```c
packed = (Y << 16) | X;      // 低 16 位 X，高 16 位 Y
```

取法在两处独立出现，都是 `movsx` 读两个 int16：

```asm
0057a3be  movsx ecx, ax                     ; X
0057a3c1  movsx eax, word ptr [esp + 0x22]  ; Y
...
0057b24c  movsx edi, si                     ; X
0057b24f  movsx esi, word ptr [esp + 0x16]  ; Y
```

工作记录 `+0x00` 存的也是这个打包值（阶段 1 `work[Y*side+X].packedXY = (Y<<16)|X`）。

与真实坐标 `(rx, ry)` 的关系（由 `0x59BAB0` 的逆变换给出）：

```
X = rx + ry + 1          2·rx = X - Y + W - 1
Y = ry - rx + W          2·ry = X + Y - W - 1
```

注意逆变换给的是 `2rx` / `2ry`——**等距格子有两种奇偶**，
半数格子的 `(rx, ry)` 落在半整数上。这正是"真实 `w×h` 的矩形在等距网格里占 `2wh` 格"
那句话的来历。

## 二、格子集合：四条不等式

菱形边界存在两个全局里，写入点 `0x5662FB`：

```asm
005662fb  mov eax, dword ptr [0x87f8dc]   ; W
00566300  mov edx, dword ptr [0x87f8e0]   ; H
00566306  mov dword ptr [0xabed04], eax   ; = W
0056630f  lea eax, [eax + edx*2]
00566317  mov dword ptr [0xabed08], eax   ; = W + 2H
```

判定在 `0x57A3C6` 与 `0x57B254` **两处一模一样**地出现：

```asm
lea  edi, [eax + ecx]          ; X + Y
cmp  edi, [0xabed04] / jle 出界   ; 需要 X + Y >  W
mov  ebp, ecx / sub ebp, eax
cmp  ebp, [0xabed04] / jge 出界   ; 需要 X - Y <  W
sub  eax, ecx
cmp  eax, [0xabed04] / jge 出界   ; 需要 Y - X <  W
cmp  edi, [0xabed08] / jg  出界   ; 需要 X + Y <= W + 2H
```

即：

```c
bool InMap(int X, int Y) {
    return (X + Y) >  W
        && (X - Y) <  W
        && (Y - X) <  W
        && (X + Y) <= W + 2*H;
}
```

> ⚠️ 上下界的**开闭不对称**：`X+Y` 下界是**严格大于** `W`、上界是**小于等于** `W+2H`。
> 照抄，不要"对称化"。

代进 `(rx, ry)` 正好是 `0 <= rx < W` 且 `0 <= ry < H`——所以 `W`/`H` 确实是
真实坐标下的宽高，`.sed` 档位换算出来的那两个数（[stage-init.md](stage-init.md) §三之二）。

### 总格数 = `H × (2W − 1)`

对固定的 `s = X+Y`（共 `2H` 个取值），`d = X−Y` 要与 `s` 同奇偶且 `|d| <= W−1`；
两种奇偶合起来恰好 `2W−1` 个，`s` 的奇偶各占一半，于是

```
N = H × (2W − 1)
```

2 人默认图（`W = H = 70`）是 **9730** 格。

> 它**不等于** `2WH`（差一个 `H`），也不等于阶段 1 那个
> `UsableCellCount() = (H+4)·W·2`（[stage-init.md](stage-init.md) §六）。
> 后者是原版故意放宽的覆盖率分母，**不要**拿本节的精确值去替换它。

## 三、遍历顺序：`Reset 0x578350` / `Next 0x578290`

迭代器状态是 `MapClass` 的四个字段：`+0x10C` = X、`+0x110` = Y、
`+0x114` = 本行剩余步数、`+0x118` = 当前格子指针。
（岸线平滑"每遍前重设四个全局 `0x87F8F4/F8/FC/900`"说的就是它们。）

```asm
; Reset：起点 (1, W)，本行还剩 W-1 步
00578350  mov eax, [ecx + 0xf4]              ; +0xF4 = 地图宽 W
00578356  mov [ecx + 0x10c], 1               ; X = 1
00578360  mov [ecx + 0x110], eax             ; Y = W
00578369  mov [ecx + 0x114], eax-1           ; count = W - 1
00578378  lea eax, [eax + edx + 4]           ; ptr = base + (Y*512 + 1)*4
```

```asm
; Next：先返回当前格，再前进
005782ae  inc esi / dec edx / dec eax        ; X+1, Y-1, count-1
005782bd  lea eax, [ebp - 0x7fc]             ; ptr += (+1 - 512)*4
; count == 0 时换行（0x5782D5 起）
005782e8  mov [ecx + 0x10c], edx             ; X' = Y
005782f2  mov [ecx + 0x110], eax             ; Y' = X
005782f8  lea edx, [edx + eax - 1]           ; t = (Y - W) + X - 1
005782fc  test dl, 1
  奇：  Y' = X + 1 ;               count = W - 1
  偶：  X' = Y + 1 ;               count = W - 2
```

即：

```c
X = 1; Y = W; count = W - 1;
while (cell(X, Y) != NULL) {
    visit(X, Y);
    if (count != 0) { ++X; --Y; --count; }
    else {
        int ox = X, oy = Y;
        if (((oy - W) + ox - 1) & 1) { X = oy;     Y = ox + 1; count = W - 1; }
        else                         { X = oy + 1; Y = ox;     count = W - 2; }
    }
}
```

**行是等距的斜线**（`X+1, Y-1` 即真实坐标里 `rx+1`），每行长度在 `W` 与 `W-1` 之间交替。
`W = 4` 的前 21 步：

```
(1,4) (2,3) (3,2) (4,1)
(2,4) (3,3) (4,2)
(2,5) (3,4) (4,3) (5,2)
(3,5) (4,4) (5,3)
(3,6) (4,5) (5,4) (6,3)
(4,6) (5,5) (6,4)
```

`512` 是格子指针数组的行跨度（`shl eax, 9` / `-0x7FC = (1 - 512) * 4`），
即数组是 `512 × 512` 的 `CellClass*`，`index = Y*512 + X`。

### 停机条件与交叉验证

原版靠"格子指针为 `NULL`"停机——出了菱形的位置没有分配格子。
把上面的走位和 §二的四条不等式各自算出集合来比：

> **遍历到的集合 ≡ 菱形判定的集合，且不重不漏**（`W ∈ [3,40] × H ∈ [1,40]` 全验）。

这两条是**独立**的证据链（一条来自迭代器算术，一条来自边界比较指令），
相等本身就是最强的交叉验证。因此可以直接用 `InMap` 当停机条件，
不必先把格子指针数组建出来。

走位本身另做了模拟执行对拍：Unicorn 直接跑 `0x578350`/`0x578290`，
喂一个 `+0xF4 = W`、`+0x13C` 指向全非 NULL 数组的假对象，
取 `W ∈ {3,4,5,6,7,8,70,120}` 各 300 步，**与重写逐点一致**。
存档 `memory/data/rmg/map_iterator_golden.txt`。

## 四、复刻状态

`code/rewrite/rmg/map_grid.h`（`MapGrid::InMap` / `ForEachCell` / `CellCount` / `side`），
回归 `test/test_map_geometry.cpp`。

`side = W + H + 1` 与工作数组下标 `Y*side + X` 见
[stage-init.md](stage-init.md) §三之二。

## 五、未确认

1. **格子指针数组 `MapClass +0x13C` 由谁分配、菱形外是否真是 `NULL`**——
   `+0x13C` 与 `+0xF4` 全镜像都没有绝对寻址的访问（只经 `this`），没跟到写入点。
   但集合等价性（§三）已经让这一条不影响复刻。
2. `0x87F8D4` 那个矩形（`{x, y, W, H}`）**谁写的**：全镜像只有 `0x68554F` 一处
   绝对写，而那是一次性清零；真正赋值应当经指针，未跟到。
   数值等于 `MapSeedClass +0x180/+0x184` 由全部下游公式的自洽性佐证。
3. `mov ecx, 0x87F978` 是迭代器最常见的 `this`（62 处），
   但 `0x87F8DC` 这组又是绝对寻址访问的——**两个对象的关系未确认**，
   不影响复刻（我们只需要 `W`/`H` 与走位规则）。
