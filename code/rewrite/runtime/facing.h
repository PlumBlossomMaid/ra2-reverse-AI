#pragma once

#include <cstdint>

namespace ra2::runtime {

// FacingStruct（朝向控制器）——0x4C91xx 家族的 0x18 字节布局，
// 偏移逐项由汇编确认（0x4C91E0 ctor / 0x4C9220 SetDesired / 0x4C93D0 GetFacing）。
// 16 位朝向：0x10000 = 一整圈；ROT 是（INI 转速值 × 256），clamp 到 127。
struct FacingStruct {
    std::uint16_t Value;     // +0x00 目标朝向（想转去哪）
    std::uint16_t unused_2;  // +0x02 从不被本家族读
    std::uint16_t Initial;   // +0x04 本次旋转的起点朝向
    std::uint16_t unused_6;  // +0x06 从不被本家族读
    std::uint32_t StartTime; // +0x08 Timer.StartTime，0xFFFFFFFF = 未启动
    std::uint32_t unused_C;  // +0x0C 从不被本家族读
    std::uint32_t TimeLeft;  // +0x10 Timer.TimeLeft（旋转总帧数）
    std::uint16_t ROT;       // +0x14 转速（每秒朝向增量，INI × 256）
    std::uint16_t unused_16; // +0x16 从不被本家族读
};

// 0x426630: TimerStruct::GetTimeLeft。未启动（StartTime==-1）原样返回 TimeLeft；
// 已启动则扣掉 CurrentFrame-StartTime 的流逝，无符号 >= 比较钳到 0。
std::uint32_t timer_get_time_left(std::uint32_t start_time, std::uint32_t time_left,
                                  std::uint32_t current_frame);

// 0x4C93D0: GetFacing——当前朝向插值。返回 dword（低 16 位是当前朝向，
// 高 16 位原样携带 +0x02）。公式（按机器码/OpenTS 的运算顺序）：
//   steps = |Value-Initial| / ROT；remaining = GetTimeLeft()
//   out = Value - (int16)((Value-Initial)/steps * remaining)
// 注意 YRpp 头文件写成 `steps_left*diff/num_steps`（先乘后除）与原汇编
// `diff/steps*remaining`（先除后乘）截断顺序不同——以机器码为准。
std::uint32_t facing_get_facing(const FacingStruct& f, std::uint32_t current_frame);

// 0x4C9480: IsRotating——ROT>0 且 GetTimeLeft()>0。
bool facing_is_rotating(const FacingStruct& f, std::uint32_t current_frame);

// 0x4C94B0 / 0x4C94F0: IsRotatingCW / IsRotatingCCW——旋转中且
// (Value-Initial)>0（顺时针正增量）/ <0。
bool facing_is_rotating_cw(const FacingStruct& f, std::uint32_t current_frame);
bool facing_is_rotating_ccw(const FacingStruct& f, std::uint32_t current_frame);

// 0x4C9220: SetDesired——起始转向。Value==target 返 false；否则
// Initial=current()、Value=target，ROT>0 时 StartTime=CurrentFrame、
// TimeLeft=|Value-Initial|/ROT。+0x0C 写入栈上未初始化 dword（永不读，模型置 0）。
bool facing_set_desired(FacingStruct& f, std::uint16_t target, std::uint32_t current_frame);

// 0x4C9530: Difference()——Value - current()（留在旋转外的剩余量，低 16 位）。
std::int16_t facing_difference(const FacingStruct& f, std::uint32_t current_frame);

// 0x4C95D0: Difference(DirType facing)——给定朝向 - current()。
std::int16_t facing_difference_from(const FacingStruct& f, std::uint16_t facing,
                                    std::uint32_t current_frame);

// 0x4C91E0: FacingStruct(rot) ctor——Value/Initial/TimeLeft=0、
// StartTime=CurrentFrame、ROT=clamp(rot,0..127)<<8。+0x02/+0x06/+0x0C 不写。
void facing_construct(FacingStruct* out, std::uint32_t rot, std::uint32_t current_frame);

}  // namespace ra2::runtime