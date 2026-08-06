#include "map_size.h"

#include <cassert>

#include "fp_guard.h"

namespace ra2::rmg {
namespace {

// 四张 .rdata 表，各 7 项，下标 = NumPlayers - 2。宽高两组逐项相同。
// dump 见 memory/data/rmg/map_size_tables.txt。
constexpr int kWidthLo[7] = {70, 70, 70, 80, 90, 100, 100};   // 0x7ED634
constexpr int kWidthHi[7] = {80, 80, 80, 90, 100, 110, 120};  // 0x7ED650
constexpr int kHeightLo[7] = {70, 70, 70, 80, 90, 100, 100};  // 0x7ED66C
constexpr int kHeightHi[7] = {80, 80, 80, 90, 100, 110, 120}; // 0x7ED688

constexpr float kOneThird = 0.3333333432674408f;  // 0x7ED968，单精度常量
constexpr double kClampD = 1.2;                   // 0x7E5190，双精度常量

// 宽度那条夹取完要 `fstp dword` 落回单精度，RC=chop 下 1.2 截成这个值
// （而不是就近舍入的 1.2000000476837158f）。写死是为了不让编译器在
// 常量折叠时按就近舍入把它算错——那是编译期行为，fesetround 管不着。
constexpr float kClampF = 1.1999999284744263f;

}  // namespace

MapSize ComputeMapSize(int width_step, int height_step, int num_players,
                       int map_type) {
  assert(num_players >= 2 && num_players <= 8 && "四张插值表只有 7 项");
  const int idx = num_players - 2;

  // 0x599665：fild + fmul(PC=53) + fstp dword。整数乘单精度常量在 double 里是
  // 精确的，所以那次 53 位舍入是空操作，只有落回 float 这一次真的截断。
  float wt = static_cast<float>(static_cast<double>(width_step) *
                                static_cast<double>(kOneThird));
  double ht = static_cast<float>(static_cast<double>(height_step) *
                                 static_cast<double>(kOneThird));

  if (map_type != 3 && map_type != 4) {  // 0x59968D/0x599692：3/4 整段跳过
    if (static_cast<double>(wt) > kClampD) wt = kClampF;  // 夹完落回 float
    if (ht > kClampD) ht = kClampD;  // 夹完留在 x87 栈上，仍是 double
  }

  // 0x5996DB / 0x599706：两条线性插值，各自 _ftol(0x7C5F00) 截断成 int。
  // 逐步舍入不可省——70*(1-1.2)+80*1.2 在 double 里是 81.99999999999999956，
  // 精确算成 82 就错了。
  const double wtd = static_cast<double>(wt);
  const double w = kWidthLo[idx] * (1.0 - wtd) + kWidthHi[idx] * wtd;
  const double h = kHeightLo[idx] * (1.0 - ht) + kHeightHi[idx] * ht;

  MapSize out;
  out.width = static_cast<int>(w);
  out.height = static_cast<int>(h);
  return out;
}

}  // namespace ra2::rmg
