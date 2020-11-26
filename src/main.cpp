#include <llvm/IR/Module.h>
#include <llvm/IR/PassManager.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/IR/NoFolder.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/ExecutionEngine/GenericValue.h>
#include <llvm/ExecutionEngine/Interpreter.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Linker/IRMover.h>

#include <iostream>
#include <vector>
#include <memory>
#include <fstream>
#include <unordered_map>
#include <iomanip>
#include <cstdio>
#include <filesystem>
#include <future>

#include "instructions.hpp"

using namespace llvm;

const uint16_t MASKS[] = { 0xffff, 0xf0ff, 0xf00f, 0xf000 };
static const std::unordered_map<uint16_t, instruction_t> INSTRUCTIONS =
{
    { 0x00e0, instruction::cls },
    { 0x00ee, instruction::ret },
    { 0xf065, instruction::ld_vx_i },
    { 0xf065, instruction::ld_b_vx },
    { 0xf01e, instruction::add_i_vx },
    { 0xf007, instruction::ld_vx_dt },
    { 0xf015, instruction::ld_dt_vx },
    { 0x0000, instruction::sys },
    { 0x1000, instruction::jp },
    { 0xb000, instruction::jp_rel },
    { 0xa000, instruction::ld_i },
    { 0x6000, instruction::ld_reg },
    { 0x3000, instruction::se },
    { 0x4000, instruction::sne },
    { 0xc000, instruction::rnd },
    { 0xd000, instruction::drw },
    { 0x2000, instruction::call },
    { 0x7000, instruction::add },
    { 0x8004, instruction::add_v_v },
};
 
void handle_instructions(std::vector<uint8_t>& data, size_t size, Module& program, IRBuilder<NoFolder>& builder)
{
    context_info context{ program, builder };

    for (size_t pc = 0; pc < size; pc += 2)
    {
        auto instruction = (data[pc] << 8) | data[pc + 1];

        std::unordered_map<uint16_t, instruction_t>::const_iterator handler;
        for (auto mask : MASKS)
        {
            handler = INSTRUCTIONS.find(instruction & mask);

            if (handler != INSTRUCTIONS.end()) break;
        }

        std::cout << std::setfill('0') << std::setw(4) << std::hex << (int)(0x200 + pc) << ": ";
        if (handler != INSTRUCTIONS.end())
        {
            instruction_info info{ instruction, pc };

            auto block = context.basic_blocks.find(pc);
            if (block != context.basic_blocks.end())
            {
                auto main = program.getFunction("main");
                auto dst_block = utils::find_block(main->getBasicBlockList(), fmt("%x", pc));
                
                if (!dst_block)
                    dst_block = BasicBlock::Create(program.getContext(), fmt("%x", pc), main);
                
                builder.SetInsertPoint(block->second);
                builder.CreateBr(dst_block);
                builder.SetInsertPoint(dst_block);

                context.basic_blocks.erase(block);
            }

            bool ignore_skippable = context.skippable == nullptr;
            handler->second(info, context);

            if (!ignore_skippable && context.skippable)
            {
                if(!context.skippable->getTerminator())
                    builder.CreateBr(context.skippable);
                builder.SetInsertPoint(context.skippable);
                context.skippable = nullptr;
            }
        }
        else
        {
            std::cout << "UNKNOWN " << (int)instruction << std::endl;
            //__debugbreak();
        }
    }
}

void add_externals(Module& program, IRBuilder<NoFolder>& builder)
{
    auto type = FunctionType::get(builder.getInt32Ty(), {}, false);
    program.getOrInsertFunction("rand", type);

    ArrayRef<Type*> args({ builder.getInt8Ty()->getPointerTo(), builder.getInt8Ty()->getPointerTo() });
    type = FunctionType::get(builder.getInt32Ty(), args, false);
    program.getOrInsertFunction("printf", type);

    args = { builder.getInt8Ty()->getPointerTo() };
    type = FunctionType::get(builder.getVoidTy(), args, false);
    program.getOrInsertFunction("draw", type);

    type = FunctionType::get(builder.getVoidTy(), {}, false);
    program.getOrInsertFunction("init", type);

    args = { builder.getInt8Ty()->getPointerTo() };
    type = FunctionType::get(builder.getVoidTy(), args, false);
    program.getOrInsertFunction("start_delay_timer", type);
}

void dump_to_file(Module& program, std::string& name)
{
    std::string ir;
    raw_string_ostream stream(ir);
    program.print(stream, nullptr);

    auto ir_path = name.append(".ll");
    std::ofstream output(ir_path);
    output << ir;
    output.close();
}

std::future<void> execute(Module& program, std::vector<uint8_t>& data)
{
    InitializeNativeTarget();
    InitializeNativeTargetAsmPrinter();
    InitializeNativeTargetAsmParser();

    std::unique_ptr<Module> ptr(&program);
    std::string error;
    auto vm = EngineBuilder(std::move(ptr))
        .setErrorStr(&error)
        .setEngineKind(EngineKind::Interpreter)
        .create();

    if (!error.empty())
    {
        printf("Execution error: %s\n", error.c_str());
        return std::async(std::launch::async, [] {});
    }

    /*SMDiagnostic err;
    auto lib = parseIRFile("..\\lib.ll", err, context);

    vm->addModule(std::move(lib));*/
    vm->finalizeObject();

    auto future = std::async(std::launch::async, [&]
        {
            auto main = program.getFunction("main");
            vm->runFunction(main, { });
        });

    std::this_thread::sleep_for(std::chrono::seconds(2));
    auto screen = (uint8_t*)vm->getAddressToGlobalIfAvailable("screen");
    auto memory = (uint8_t*)vm->getAddressToGlobalIfAvailable("memory");

    printf("ROM: ");
    for (int i = 0; i < data.size(); ++i)
    {
        printf("%02x ", memory[0x200 + i]);
    }
    printf("\n");

    for (int i = 0; i < 64 * 32; ++i)
    {
        printf("%02x ", screen[i]);
    }

    return future;
}

void fill_non_terminated_blocks(Function* func, IRBuilder<NoFolder>& builder)
{
    auto print = func->getParent()->getFunction("printf");
    auto fmt = builder.CreateGlobalStringPtr("NON TERMINATED BLOCK REACHED: %s\n", "fmt");

    for (auto& basic_block : func->getBasicBlockList())
    {
        if (!basic_block.getTerminator())
        {
            builder.SetInsertPoint(&basic_block);
            
            auto name = builder.CreateGlobalStringPtr(basic_block.getName(), "bbname");
            builder.CreateCall(print, { fmt, name });
            builder.CreateBr(&basic_block);
        }
    }
}

void remove_dead_blocks(Function* func)
{
    std::vector<BasicBlock*> dead_blocks;
    for (auto& basic_block : func->getBasicBlockList())
    {
        if (basic_block.getName() != "entrypoint" && (basic_block.empty() || basic_block.hasNPredecessors(0)))
        {
            dead_blocks.push_back(&basic_block);
        }
    }

    for (auto& basic_block : dead_blocks)
    {
        basic_block->eraseFromParent();
    }
}

int main(int argc, char* argv[])
{
    std::filesystem::path path{ argv[1] };
    auto data = utils::read_file(path);
    auto name = path.filename().string();
    auto size = data.size();

    if (argv[2])
    {
        size = atoi(argv[2]);
    }

    LLVMContext context;
    IRBuilder<NoFolder> builder(context);
    Module program(name, context);

    auto type = FunctionType::get(builder.getVoidTy(), false);
    auto func = Function::Create(type, Function::ExternalLinkage, "main", program);

    auto entry = BasicBlock::Create(context, "entrypoint", func);
    builder.SetInsertPoint(entry);

    add_externals(program, builder);

    auto create_global = [&program](const std::string& name, Type* type, uint64_t value = 0)
    {
        program.getOrInsertGlobal(name, type);

        auto global = program.getNamedGlobal(name);
        global->setLinkage(GlobalValue::InternalLinkage);

        if (type->isAggregateType())
            global->setInitializer(ConstantAggregateZero::get(type));
        else
            global->setInitializer(Constant::getIntegerValue(type, APInt(type->getPrimitiveSizeInBits(), value)));

        return global;
    };

    /* set up registers V0-Vf, I, DT and ST */
    create_global("I", builder.getInt16Ty());
    auto dt = create_global("DT", builder.getInt8Ty());
    for (int i = 0; i < 16; ++i)
    {
        create_global(utils::fmt("V%x", i), builder.getInt8Ty());
    }

    /* set up 4kb memory page */
    auto memory = create_global("memory", ArrayType::get(builder.getInt8Ty(), 4096));

    int i = 0x200;
    for (auto byte : data)
    {
        auto dest = builder.CreateInBoundsGEP(memory, { utils::GetIntConstant(program, 0), utils::GetIntConstant(program, i) });
        builder.CreateStore(builder.getInt8(byte), dest);
        i++;
    }

    /* set up 64*32 screen buffer */
    create_global("screen", ArrayType::get(builder.getInt8Ty(), 64*32));

    /* set up stack */
    create_global("stack", ArrayType::get(builder.getInt16Ty(), 16));

    /* set up graphics */
    builder.CreateCall(program.getFunction("init"));

    /* start timers */
    auto dt_func = program.getFunction("start_delay_timer");
    builder.CreateCall(dt_func, { dt });

    handle_instructions(data, size, program, builder);

    builder.CreateRetVoid();

    //remove_dead_blocks(func);
    fill_non_terminated_blocks(func, builder);

    printf("\n== Verification ==\n");
    printf("Module: %d\n", !verifyModule(program, &outs()));
    printf("Main: %d\n", !verifyFunction(*func, &outs()));

    printf("\n== Dump ==\n");
    program.dump();

    dump_to_file(program, name);

    printf("\n");

    auto task = execute(program, data);
    task.wait();
    
    return 0;
}