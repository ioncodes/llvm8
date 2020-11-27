#pragma once

#include <llvm/IR/Module.h>
#include <llvm/IR/PassManager.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/NoFolder.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/ExecutionEngine/GenericValue.h>
#include <llvm/ExecutionEngine/Interpreter.h>
#include <llvm/IR/InlineAsm.h>

#include "utils.hpp"

using namespace llvm;
using namespace utils;

struct instruction_info
{
    uint16_t instruction;
    uint16_t address;

    template<size_t N>
    auto nibble() { return get_nibble(instruction, N); }
    template<size_t N>
    auto byte() { return get_byte(instruction, N); }

    auto addr() { return get_addr(instruction); }
};

struct context_info
{
    Module& program;
    IRBuilder<NoFolder>& builder;
    std::unordered_map<uint16_t, BasicBlock*> basic_blocks;
    std::unordered_map<uint16_t, Instruction*> instructions;
    BasicBlock* skippable = nullptr;

    auto ctx() { return std::tie(program, builder); }
};

using instruction_t = void(*)(instruction_info&, context_info&);

struct instruction
{
    static void jp(instruction_info& info, context_info& context)
    {
        auto [program, builder] = context.ctx();
        auto instr = log(program, builder, fmt("jp 0x%x", info.addr()));
        context.instructions[info.address] = instr;

        auto main = program.getFunction("main");
        
        auto phys_addr = info.addr() - 0x200;
        auto dst_block = utils::find_block(main->getBasicBlockList(), fmt("%x", phys_addr));
        
        if (!dst_block && phys_addr > info.address)
        {
            context.basic_blocks[phys_addr] = &main->back();
        }
        else if (!dst_block && phys_addr < info.address)
        {
            auto instr = context.instructions[phys_addr];
            instr->getParent()->splitBasicBlock(instr, fmt("%x", phys_addr));
            dst_block = utils::find_block(main->getBasicBlockList(), fmt("%x", phys_addr));
            builder.CreateBr(dst_block);
        }
        else if (!dst_block && phys_addr == info.address)
        {
            auto self = BasicBlock::Create(program.getContext(), fmt("%x", phys_addr), main);
            builder.CreateBr(self);
            builder.SetInsertPoint(self);
            builder.CreateBr(self);
        }
        else if (dst_block)
        {
            builder.CreateBr(dst_block);
        }

        auto next_block = BasicBlock::Create(program.getContext(), fmt("%x", info.address + 2), main);
        builder.SetInsertPoint(next_block);
    }

    static void jp_rel(instruction_info& info, context_info& context)
    {
        auto [program, builder] = context.ctx();
        auto instr = log<false>(program, builder, fmt("jp V0, 0x%x", info.addr()));;
        context.instructions[info.address] = instr;
    }

    static void ld_i(instruction_info& info, context_info& context)
    {
        auto [program, builder] = context.ctx();
        auto instr = log(program, builder, fmt("ld I, 0x%x", info.addr()));
        context.instructions[info.address] = instr;

        auto i = program.getNamedGlobal("I");
        auto value = builder.getInt16(info.addr());
        builder.CreateStore(value, i);
    }

    static void ld_reg(instruction_info& info, context_info& context)
    {
        auto reg = info.nibble<1>();
        auto byte = info.byte<0>();

        auto [program, builder] = context.ctx();
        auto instr = log(program, builder, fmt("ld V%x, 0x%x", reg, byte));
        context.instructions[info.address] = instr;

        auto v_reg = program.getNamedGlobal(fmt("V%x", reg));
        builder.CreateStore(builder.getInt8(byte), v_reg);
    }

    static void se(instruction_info& info, context_info& context)
    {
        auto reg = info.nibble<1>();
        auto byte = info.byte<0>();

        auto [program, builder] = context.ctx();
        auto instr = log(program, builder, fmt("se V%x, 0x%x", reg, byte));
        context.instructions[info.address] = instr;

        auto main = program.getFunction("main");
        auto dst_f = BasicBlock::Create(program.getContext(), fmt("%x", info.address + 2), main);
        auto dst_t = BasicBlock::Create(program.getContext(), fmt("%x", info.address + 4), main);

        auto v_reg = program.getNamedGlobal(fmt("V%x", reg));
        auto deref = builder.CreateLoad(v_reg);
        auto cond = builder.CreateICmp(CmpInst::Predicate::ICMP_EQ, deref, builder.getInt8(byte));
        builder.CreateCondBr(cond, dst_t, dst_f);

        builder.SetInsertPoint(dst_f);
        context.skippable = dst_t;
    }

    static void sne(instruction_info& info, context_info& context)
    {        
        auto reg = info.nibble<1>();
        auto byte = info.byte<0>();

        auto [program, builder] = context.ctx();
        auto instr = log(program, builder, fmt("sne V%x, 0x%x", reg, byte));
        context.instructions[info.address] = instr;

        auto main = program.getFunction("main");
        auto dst_f = BasicBlock::Create(program.getContext(), fmt("%x", info.address + 2), main);
        auto dst_t = BasicBlock::Create(program.getContext(), fmt("%x", info.address + 4), main);
        
        auto v_reg = program.getNamedGlobal(fmt("V%x", reg));
        auto deref = builder.CreateLoad(v_reg);
        auto cond = builder.CreateICmp(CmpInst::Predicate::ICMP_NE, deref, builder.getInt8(byte));
        builder.CreateCondBr(cond, dst_t, dst_f);

        builder.SetInsertPoint(dst_f);
        context.skippable = dst_t;
    }

    static void rnd(instruction_info& info, context_info& context)
    {
        auto reg = info.nibble<1>();
        auto byte = info.byte<0>();

        auto [program, builder] = context.ctx();
        auto instr = log(program, builder, fmt("rnd V%x, 0x%x", reg, byte));
        context.instructions[info.address] = instr;

        auto rand = program.getFunction("rand");
        auto v_reg = program.getNamedGlobal(fmt("V%x", reg));
        auto rand_value = builder.CreateCall(rand, {});
        auto trunc = builder.CreateTrunc(rand_value, builder.getInt8Ty());
        auto and_v = builder.CreateAnd(trunc, builder.getInt8(byte));
        builder.CreateStore(and_v, v_reg);
    }

    static void drw(instruction_info& info, context_info& context)
    {
        auto xnib = info.nibble<1>();
        auto ynib = info.nibble<2>();
        auto size = info.nibble<3>();

        auto [program, builder] = context.ctx();
        auto instr = log(program, builder, fmt("drw V%x, V%x, 0x%x", xnib, ynib, size));
        context.instructions[info.address] = instr;

        auto ireg = program.getNamedGlobal("I");
        auto xreg = program.getNamedGlobal(fmt("V%x", xnib));
        auto yreg = program.getNamedGlobal(fmt("V%x", ynib));
        auto memory = program.getNamedGlobal("memory");
        auto screen = program.getNamedGlobal("screen");

        auto i = builder.CreateLoad(ireg);
        auto x = builder.CreateLoad(xreg);
        auto y = builder.CreateLoad(yreg);

        auto i_64 = builder.CreateIntCast(i, builder.getInt64Ty(), true);
        auto y_64 = builder.CreateIntCast(y, builder.getInt64Ty(), true);

        for (auto n = 0; n < size; ++n)
        {
            auto x_64 = builder.CreateIntCast(x, builder.getInt64Ty(), true);

            // load byte from sprite
            auto sprt = builder.CreateInBoundsGEP(memory, { GetIntConstant(program, 0), i_64 });
            auto byte = builder.CreateLoad(sprt);

            // copy each bit (pixel) to its own byte in the screen buffer
            for (auto bit = 7; bit >= 0; bit--)
            {
                // extract bit
                auto shift = builder.CreateAShr(byte, bit);
                auto value = builder.CreateAnd(shift, builder.getInt8(1));

                // calculate 1D offset for byte
                auto tmp0 = builder.CreateMul(builder.getInt64(64), y_64);
                auto tmp1 = builder.CreateAdd(tmp0, x_64);
                auto dest = builder.CreateInBoundsGEP(screen, { GetIntConstant(program, 0), tmp1 });

                x_64 = builder.CreateAdd(x_64, builder.getInt64(1));

                // TODO: set VF on collission
                // xor byte into screen
                auto orig = builder.CreateLoad(dest);
                value = builder.CreateXor(orig, value);
                builder.CreateStore(value, dest);
            }

            i_64 = builder.CreateAdd(i_64, builder.getInt64(1));
            y_64 = builder.CreateAdd(y_64, builder.getInt64(1));
        }

        auto draw = program.getFunction("draw");
        auto buff = builder.CreateGEP(screen, { GetIntConstant(program, 0), GetIntConstant(program, 0) });
        builder.CreateCall(draw, { buff });
    }

    static void call(instruction_info& info, context_info& context)
    {
        auto [program, builder] = context.ctx();
        auto instr = log<false>(program, builder, fmt("call 0x%x", info.addr()));
        context.instructions[info.address] = instr;
    }

    static void add(instruction_info& info, context_info& context)
    {
        auto reg = info.nibble<1>();
        auto byte = info.byte<0>();

        auto [program, builder] = context.ctx();
        auto instr = log(program, builder, fmt("add V%x, 0x%x", reg, byte));
        context.instructions[info.address] = instr;

        auto v_reg = program.getNamedGlobal(fmt("V%x", reg));
        auto deref = builder.CreateLoad(v_reg);
        auto value = builder.CreateAdd(deref, builder.getInt8(byte));
        builder.CreateStore(value, v_reg);
    }

    static void add_v_v(instruction_info& info, context_info& context)
    {
        auto xnib = info.nibble<1>();
        auto ynib = info.nibble<2>();

        auto [program, builder] = context.ctx();
        auto instr = log(program, builder, fmt("add V%x, V%x", xnib, ynib));
        context.instructions[info.address] = instr;

        auto xreg = program.getNamedGlobal(fmt("V%x", xnib));
        auto yreg = program.getNamedGlobal(fmt("V%x", ynib));
        auto xreg_deref = builder.CreateLoad(xreg);
        auto yreg_deref = builder.CreateLoad(yreg);
        auto value = builder.CreateAdd(xreg_deref, yreg_deref);
        builder.CreateStore(value, xreg);
    }

    static void add_i_vx(instruction_info& info, context_info& context)
    {
        auto reg = info.nibble<1>();

        auto [program, builder] = context.ctx();
        auto instr = log(program, builder, fmt("add I, V%x", reg));
        context.instructions[info.address] = instr;

        auto vreg = program.getNamedGlobal(fmt("V%x", reg));
        auto ireg = program.getNamedGlobal("I");
        auto vreg_deref = builder.CreateLoad(vreg);
        auto vreg_deref_64 = builder.CreateIntCast(vreg_deref, builder.getInt16Ty(), true);
        auto ireg_deref = builder.CreateLoad(ireg);
        auto value = builder.CreateAdd(ireg_deref, vreg_deref_64);
        builder.CreateStore(value, ireg);
    }

    static void cls(instruction_info& info, context_info& context)
    {
        auto [program, builder] = context.ctx();
        auto instr = log<false>(program, builder, "cls");
        context.instructions[info.address] = instr;
    }

    static void ret(instruction_info& info, context_info& context)
    {
        auto [program, builder] = context.ctx();
        auto instr = log<false>(program, builder, "ret");
        context.instructions[info.address] = instr;
    }

    static void sys(instruction_info& info, context_info& context)
    {
        auto [program, builder] = context.ctx();
        auto instr = log(program, builder, "sys");
        //context.instructions[info.address] = instr;

        jp(info, context);
    }

    // TODO: this is wrong
    static void ld_vx_i(instruction_info& info, context_info& context)
    {
        auto reg = info.nibble<1>();

        auto [program, builder] = context.ctx();
        auto instr = log(program, builder, fmt("ld V%x, [I]", reg));
        context.instructions[info.address] = instr;

        auto i_reg = program.getNamedGlobal("I");
        auto v_reg = program.getNamedGlobal(fmt("V%x", reg));
        auto deref = builder.CreateLoad(i_reg);
        auto value = builder.CreateTrunc(deref, builder.getInt8Ty());
        builder.CreateStore(value, v_reg);
    }

    static void ld_vx_dt(instruction_info& info, context_info& context)
    {
        auto reg = info.nibble<1>();

        auto [program, builder] = context.ctx();
        auto instr = log(program, builder, fmt("ld V%x, DT", reg));
        context.instructions[info.address] = instr;

        auto d_reg = program.getNamedGlobal("DT");
        auto v_reg = program.getNamedGlobal(fmt("V%x", reg));

        auto value = builder.CreateLoad(d_reg);
        builder.CreateStore(value, v_reg);
    }

    static void ld_dt_vx(instruction_info& info, context_info& context)
    {
        auto reg = info.nibble<1>();

        auto [program, builder] = context.ctx();
        auto instr = log(program, builder, fmt("ld DT, V%x", reg));
        context.instructions[info.address] = instr;

        auto d_reg = program.getNamedGlobal("DT");
        auto v_reg = program.getNamedGlobal(fmt("V%x", reg));

        auto value = builder.CreateLoad(v_reg);
        builder.CreateStore(value, d_reg);
    }
    
    static void ld_b_vx(instruction_info& info, context_info& context)
    {
        auto reg = info.nibble<1>();

        auto [program, builder] = context.ctx();
        auto instr = log(program, builder, fmt("ld B, V%x", info.nibble<1>()));
        context.instructions[info.address] = instr;

        auto v_reg = program.getNamedGlobal(fmt("V%x", reg));
        auto i_reg = program.getNamedGlobal("I");
        auto memory = program.getNamedGlobal("memory");
        auto value = builder.CreateLoad(v_reg);
        auto dest = builder.CreateLoad(i_reg);
        
        auto v0 = builder.CreateSDiv(value, builder.getInt8(100));
        auto v1 = builder.CreateSDiv(value, builder.getInt8(10));
        v1 = builder.CreateSRem(v1, builder.getInt8(10));
        auto v2 = builder.CreateSRem(value, builder.getInt8(100));
        v2 = builder.CreateSRem(v2, builder.getInt8(10));

        auto dest_64 = builder.CreateIntCast(dest, builder.getInt64Ty(), true);

        Value* values[3] = { v0, v1, v2 };
        for (int i = 0; i < 3; ++i)
        {
            auto dest = builder.CreateInBoundsGEP(memory, { GetIntConstant(program, 0), dest_64 });
            builder.CreateStore(values[i], dest);
            dest_64 = builder.CreateAdd(dest_64, builder.getInt64(1));
        }
    }

    static void ld_i_vx(instruction_info& info, context_info& context)
    {
        auto reg = info.nibble<1>();

        auto [program, builder] = context.ctx();
        auto instr = log(program, builder, fmt("ld [I], V%x", reg));
        context.instructions[info.address] = instr;

        auto memory = program.getNamedGlobal("memory");
        auto i_reg = program.getNamedGlobal("I");
        auto v_reg = program.getNamedGlobal(fmt("V%x", reg));
        auto value = builder.CreateLoad(v_reg);
        auto deref = builder.CreateLoad(i_reg);
        auto deref_64 = builder.CreateIntCast(deref, builder.getInt64Ty(), true);

        auto dest = builder.CreateInBoundsGEP(memory, { GetIntConstant(program, 0), deref_64 });
        builder.CreateStore(value, dest);
    }

    static void shr(instruction_info& info, context_info& context)
    {
        auto reg = info.nibble<1>();

        auto [program, builder] = context.ctx();
        auto instr = log<false>(program, builder, fmt("shr V%x", reg));
        context.instructions[info.address] = instr;
    }

    static void shl(instruction_info& info, context_info& context)
    {
        auto reg = info.nibble<1>();

        auto [program, builder] = context.ctx();
        auto instr = log<false>(program, builder, fmt("shl V%x", reg));
        context.instructions[info.address] = instr;
    }

    static void sub(instruction_info& info, context_info& context)
    {
        auto xreg = info.nibble<1>();
        auto yreg = info.nibble<2>();

        auto [program, builder] = context.ctx();
        auto instr = log<false>(program, builder, fmt("sub V%x, V%x", xreg, yreg));
        context.instructions[info.address] = instr;
    }

    static void xor_v_v(instruction_info& info, context_info& context)
    {
        auto xreg = info.nibble<1>();
        auto yreg = info.nibble<2>();

        auto [program, builder] = context.ctx();
        auto instr = log<false>(program, builder, fmt("xor V%x, V%x", xreg, yreg));
        context.instructions[info.address] = instr;
    }

    static void and_v_v(instruction_info& info, context_info& context)
    {
        auto xreg = info.nibble<1>();
        auto yreg = info.nibble<2>();

        auto [program, builder] = context.ctx();
        auto instr = log<false>(program, builder, fmt("and V%x, V%x", xreg, yreg));
        context.instructions[info.address] = instr;
    }

    static void or_v_v(instruction_info& info, context_info& context)
    {
        auto xreg = info.nibble<1>();
        auto yreg = info.nibble<2>();

        auto [program, builder] = context.ctx();
        auto instr = log<false>(program, builder, fmt("or V%x, V%x", xreg, yreg));
        context.instructions[info.address] = instr;
    }

    static void ld_v_v(instruction_info& info, context_info& context)
    {
        auto xreg = info.nibble<1>();
        auto yreg = info.nibble<2>();

        auto [program, builder] = context.ctx();
        auto instr = log<false>(program, builder, fmt("ld V%x, V%x", xreg, yreg));
        context.instructions[info.address] = instr;
    }

    static void se_v_v(instruction_info& info, context_info& context)
    {
        auto xreg = info.nibble<1>();
        auto yreg = info.nibble<2>();

        auto [program, builder] = context.ctx();
        auto instr = log<false>(program, builder, fmt("se V%x, V%x", xreg, yreg));
        context.instructions[info.address] = instr;
    }

    static void sne_v_v(instruction_info& info, context_info& context)
    {
        auto xreg = info.nibble<1>();
        auto yreg = info.nibble<2>();

        auto [program, builder] = context.ctx();
        auto instr = log<false>(program, builder, fmt("sne V%x, V%x", xreg, yreg));
        context.instructions[info.address] = instr;
    }
};