#pragma once

#include <llvm/IR/Module.h>

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
        printf("%s\n", instruction.c_str());

        builder.CreateAdd(builder.getInt32(1337), builder.getInt32(1337)); // NOP sentinel

        if (!T)
        {
            auto main = program.getFunction("main");
            auto curr = &main->back();
            auto inst = &curr->back();
            auto node = MDNode::get(builder.getContext(), MDString::get(builder.getContext(), instruction));
            inst->setMetadata("UNKNOWN", node);
        }
        
        return &builder.GetInsertBlock()->back();
    }

    template<typename _Type = Type, typename _Value = uint64_t> requires (std::is_same_v<_Type, ArrayType> || std::is_same_v<_Type, IntegerType>)
    GlobalVariable* create_global(Module& program, const std::string& name, _Type* type, std::vector<_Value> value = { 0 }, size_t offset = 0)
    {
        program.getOrInsertGlobal(name, type);

        auto global = program.getNamedGlobal(name);
        global->setLinkage(GlobalValue::InternalLinkage);
        
        if constexpr (std::is_same_v<_Type, ArrayType>)
        {
            auto elem_type = type->getArrayElementType();
            auto bit_size = elem_type->getPrimitiveSizeInBits();
            auto capacity = type->getArrayNumElements();

            auto make_constant = [&elem_type, &bit_size](_Value value)
            {
                return Constant::getIntegerValue(elem_type, APInt(bit_size, value));
            };

            std::vector<Constant*> constants(capacity, make_constant(0));
            std::transform(value.begin(), value.end(), constants.begin() + offset, make_constant);

            global->setInitializer(ConstantArray::get(type, makeArrayRef<Constant*>(constants)));
        }
        else if constexpr (std::is_same_v<_Type, IntegerType>)
        {
            global->setInitializer(Constant::getIntegerValue(type, APInt(type->getPrimitiveSizeInBits(), value[0])));
        }

        return global;
    };

    Constant* GetIntConstant(Module& program, uint64_t Val, IntegerType* Ty = nullptr)
    {
        if (Ty == nullptr)
            Ty = Type::getInt64Ty(program.getContext());
        return Constant::getIntegerValue(Ty, APInt(Ty->getPrimitiveSizeInBits(), Val));
    }

    BasicBlock* find_block(Function::BasicBlockListType& basic_blocks, std::string name)
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