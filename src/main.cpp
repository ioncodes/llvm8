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
#include "argparse.hpp"

using namespace llvm;

const uint16_t MASKS[] = { 0xffff, 0xf0ff, 0xf00f, 0xf000 };
static const std::unordered_map<uint16_t, instruction_t> INSTRUCTIONS =
{
    { 0x00e0, instruction::cls },
    { 0x00ee, instruction::ret },
    { 0xf055, instruction::ld_i_vx },
    { 0xf065, instruction::ld_vx_i },
    { 0xf033, instruction::ld_b_vx },
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
    { 0x5000, instruction::se_v_v },
    { 0x9000, instruction::sne_v_v },
    { 0xc000, instruction::rnd },
    { 0xd000, instruction::drw },
    { 0x2000, instruction::call },
    { 0x7000, instruction::add },
    { 0x8000, instruction::ld_v_v },
    { 0x8001, instruction::or_v_v },
    { 0x8002, instruction::and_v_v },
    { 0x8003, instruction::xor_v_v },
    { 0x8004, instruction::add_v_v },
    { 0x8005, instruction::sub },
    { 0x8006, instruction::shr },
    { 0x800e, instruction::shl }
};
 
void handle_instructions(const std::vector<uint8_t>& data, const std::vector<std::pair<size_t, size_t>>& code_blocks, Module& program, IRBuilder<NoFolder>& builder)
{
    context_info context{ program, builder };

    for (size_t pc = 0; pc < data.size(); pc += 2)
    {
        bool is_code = false;
        for (auto& [start, end] : code_blocks)
        {
            if (pc >= start && pc <= end)
            {
                is_code = true;
                break;
            }
        }

        if (!is_code) continue;

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
            instruction_info info(instruction, pc);

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
                if (!context.skippable->getTerminator())
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

    ArrayRef<Type*> args({ builder.getInt32Ty() });
    type = FunctionType::get(builder.getVoidTy(), args, false);
    program.getOrInsertFunction("srand", type);

    args = { builder.getInt32Ty() };
    type = FunctionType::get(builder.getInt32Ty(), args, false);
    program.getOrInsertFunction("time", type);

    args = { builder.getInt8Ty()->getPointerTo(), builder.getInt8Ty()->getPointerTo() };
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

auto parse_args(int argc, char* argv[])
{
    /*
        ./llvm8 --rom ./boot.ch8 --code 0-90
    */

    auto split = [](std::string value, const std::string& delimiter)
    {
        std::vector<std::string> splitted;
        size_t pos = 0;
        while ((pos = value.find(delimiter)) != std::string::npos)
        {
            splitted.push_back(value.substr(0, pos));
            value.erase(0, pos + delimiter.size());
        }

        if (!value.empty())
            splitted.push_back(value);

        if (splitted.empty())
            splitted.push_back(value);

        return splitted;
    };

    auto extract_code_blocks = [&split](const std::string& value)
    {
        std::vector<std::pair<size_t, size_t>> code_blocks;
        for (auto& range : split(value, ","))
        {
            auto ranges = split(range, "-");
            code_blocks.push_back(std::make_pair(atoi(ranges[0].c_str()), atoi(ranges[1].c_str())));
        }

        return code_blocks;
    };

    argparse::ArgumentParser program("llvm8");

    program.add_argument("--rom")
        .help("path to the rom file")
        .required();
    program.add_argument("--code")
        .help("list of code blocks")
        .required();

    try
    {
        program.parse_args(argc, argv);
    }
    catch (const std::runtime_error& err)
    {
        std::cout << err.what() << std::endl;
        std::cout << program;
        exit(0);
    }

    auto rom = program.get("--rom");
    auto code_blocks = extract_code_blocks(program.get("--code"));

    return std::make_pair(rom, code_blocks);
}

int main(int argc, char* argv[])
{
    auto [rom, code_blocks] = parse_args(argc, argv);

    std::filesystem::path path{ rom };
    auto data = utils::read_file(path);
    auto name = path.filename().string();

    LLVMContext context;
    IRBuilder<NoFolder> builder(context);
    Module program(name, context);

    auto type = FunctionType::get(builder.getVoidTy(), false);
    auto func = Function::Create(type, Function::ExternalLinkage, "main", program);

    auto entry = BasicBlock::Create(context, "entrypoint", func);
    builder.SetInsertPoint(entry);

    add_externals(program, builder);

    /* set up registers V0-Vf, I, ST and DT */
    utils::create_global(program, "I", builder.getInt16Ty());
    utils::create_global(program, "ST", builder.getInt8Ty());
    auto dt = utils::create_global(program, "DT", builder.getInt8Ty());
    for (int i = 0; i < 16; ++i)
    {
        utils::create_global(program, utils::fmt("V%x", i), builder.getInt8Ty());
    }

    /* set up 4kb memory page */
    auto memory = utils::create_global(program, "memory", ArrayType::get(builder.getInt8Ty(), 4096), data, 0x200);

    /* set up 64*32 screen buffer */
    utils::create_global(program, "screen", ArrayType::get(builder.getInt8Ty(), 64*32));

    /* set up stack */
    utils::create_global(program, "stack", ArrayType::get(builder.getInt16Ty(), 16));

    /* set up graphics */
    builder.CreateCall(program.getFunction("init"));

    /* start timers */
    auto dt_func = program.getFunction("start_delay_timer");
    builder.CreateCall(dt_func, { dt });

    /* create RNG */
    auto srand_func = program.getFunction("srand");
    auto time_func = program.getFunction("time");
    auto seed = builder.CreateCall(time_func, { builder.getInt32(0) });
    builder.CreateCall(srand_func, { seed });

    /* lift instructions */
    handle_instructions(data, code_blocks, program, builder);

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