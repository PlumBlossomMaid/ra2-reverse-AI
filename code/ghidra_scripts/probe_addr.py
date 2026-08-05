# -*- coding: utf-8 -*-
"""探针：检查几个 YRpp 已知地址在当前 gamemd.exe 里是否有函数
用于判断 YRpp 地址体系与本地程序版本是否匹配
"""
space = currentProgram.getAddressFactory().getDefaultAddressSpace()
fm = currentProgram.getFunctionManager()
listing = currentProgram.getListing()

addrs = [
    0x4C9C60,  # FactoryClass::HasProgressChanged (YRpp)
    0x4C9C70,  # FactoryClass::DemandProduction (YRpp)
    0x4CA5A0,  # FactoryClass::StartProduction (YRpp)
    0x50B1D0,  # HouseClass::AISupers (YRpp)
    0xB73550,  # Game::hWnd (YRpp)
    0x7CD80F,  # 入口 (已验证存在)
    0x401000,  # .text 起始
]

for a in addrs:
    addr = space.getAddress(a)
    fn = fm.getFunctionAt(addr)
    if fn:
        print("0x%x -> FUNC: %s @ %s" % (a, fn.getName(), fn.getEntryPoint()))
    else:
        # 看看有没有指令
        ins = listing.getInstructionAt(addr)
        data = listing.getDataAt(addr)
        print("0x%x -> NO_FUNC | ins=%s data=%s" % (a, ins is not None, data is not None))
