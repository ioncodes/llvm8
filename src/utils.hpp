#pragma once

#include <llvm/IR/Module.h>
#include <llvm/IR/PassManager.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/NoFolder.h>

#include <iostream>
#include <cstdio>
#include <vector>
#include <filesystem>

using namespace llvm;

namespace utils
{
    template<typename... Tx>
    static std::string fmt(const char* fmt, Tx&&... args)
    {
        std::string buffer;
        buffer.resize(snprintf(nullptr, 0, fmt, args...));
        snprintf(buffer.data(), buffer.size() + 1, fmt, std::forward<Tx>(args)...);
        return buffer;
    }

    template<typename T = uint8_t>
    std::vector<T> read_file(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);

        file.seekg(0, std::ios_base::end);
        std::streampos length = file.tellg();
        file.seekg(0, std::ios_base::beg);

        std::vector<T> buffer(length / sizeof(T));
        file.read((char*)buffer.data(), length);

        return buffer;
    }

    template<bool T = true>
    Instruction* log(Module& program, IRBuilder<NoFolder>& builder, const std::string& instruction)
    {
        printf("%s\n", instruction);

        builder.CreateAdd(builder.getInt32(1337), builder.getInt32(1337)); // NOP sentinel

        if (!T)
        {
            auto main = program.getFunction("main");
            auto curr = &main->back();
            auto inst = &curr->back();
            auto node = MDNode::get(builder.getContext(), MDString::get(builder.getContext(), instruction));
            inst->setMetadata("UNKNOWN", node);
        }
        
        // GetInsertBlock returns shit?
        return &builder.GetInsertBlock()->back();
    }

    Constant* GetIntConstant(Module& program, uint64_t Val, IntegerType* Ty = nullptr)
    {
        if (Ty == nullptr)
            Ty = Type::getInt64Ty(program.getContext());
        return Constant::getIntegerValue(Ty, APInt(Ty->getPrimitiveSizeInBits(), Val));
    }

    BasicBlock* find_block(Function::BasicBlockListType& basic_blocks, std::string& name)
    {
        for (auto& basic_block : basic_blocks)
        {
            if (basic_block.getName() == name)
            {
                return &basic_block;
            }
        }

        return nullptr;
    }

    uint8_t get_nibble(uint16_t value, size_t n)
    {
        return (value >> (4 * (3 - n))) & 0x0f;
    }

    uint16_t get_addr(uint16_t value)
    {
        return value & 0x0fff;
    }

    // TODO: this is reversed
    uint8_t get_byte(uint16_t value, size_t n)
    {
        return (value >> (8 * n)) & 0xff;
    }
}