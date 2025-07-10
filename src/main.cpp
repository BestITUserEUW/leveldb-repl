#include <cstdint>
#include <cstdlib>
#include <csignal>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <print>
#include <algorithm>
#include <array>
#include <vector>

#include <leveldb/db.h>
#include <oryx/enchantum.hpp>

using std::print;
using std::println;
using ArgsVector = std::vector<std::string_view>;

enum class Instruction : uint8_t { help, exit, open, close, read, write, dump };

struct InstructionInfo {
    using ImplFn = void (*)(const ArgsVector&);
    struct Arguments {
        std::string_view data;
        size_t size;
    };

    std::string_view description;
    Arguments args;
    ImplFn impl;
    bool require_db = false;
};

std::unique_ptr<leveldb::DB> database;

constexpr auto ViewToSlice(std::string_view view) -> leveldb::Slice;
constexpr auto GetInfo(Instruction inst) -> const InstructionInfo&;

struct ExitFunctor {
    void operator()() const {
        database.reset();
        std::exit(EXIT_SUCCESS);
    }
};

struct PrintHelpFunctor {
    static constexpr auto kPrintFmt = "{:<15}{:<20}{:<20}";

    void operator()() const {
        println("Help\n");
        println("Input format is: <instruction> <args>");
        println("Example: open ./database.ldb\n");
        println(kPrintFmt, "Instruction", "Arguments", "Description");
        enchantum::for_each<Instruction>([&](auto c) {
            const auto& info = GetInfo(c.value);
            println(kPrintFmt, enchantum::to_string(c.value), info.args.data, info.description);
        });
    }
};

struct CloseFunctor {
    void operator()() const {
        database.reset();
        println("OK");
    }
};

struct DumpFunctor {
    void operator()() const {
        auto it = std::unique_ptr<leveldb::Iterator>(database->NewIterator(leveldb::ReadOptions()));
        for (it->SeekToFirst(); it->Valid(); it->Next()) {
            println("{}: {}", it->key().ToString(), it->value().ToString());
        }
    }
};

struct OpenFunctor {
    void operator()(const ArgsVector& args) const {
        leveldb::Options opts{};
        opts.create_if_missing = true;
        opts.reuse_logs = true;
        auto path = std::string(args[0]);

        const auto status = leveldb::DB::Open(opts, path, std::out_ptr(database));
        if (!status.ok()) {
            println("error: open {} status='{}'", path, status.ToString());
        }
        println("OK");
    }
};

struct WriteFunctor {
    void operator()(const ArgsVector& args) const {
        auto key = args[0];
        auto value = args[1];

        leveldb::WriteOptions opts{};
        opts.sync = true;
        const auto status = database->Put(opts, ViewToSlice(key), ViewToSlice(value));
        if (!status.ok()) {
            println("error: write {} {} status='{}'", key, value, status.ToString());
            return;
        }
        println("OK");
    }
};

struct ReadFunctor {
    void operator()(const ArgsVector& args) const {
        auto key = args[0];
        leveldb::ReadOptions opts{};
        std::string result;
        const auto status = database->Get(opts, ViewToSlice(key), &result);
        if (!status.ok()) {
            println("error: read {} status='{}'", key, status.ToString());
            return;
        }
        println("{}", result);
    }
};

template <class T>
struct Equals {
    Equals(const T& rhs)
        : rhs(rhs) {}

    auto operator()(T lhs) const -> bool { return lhs == rhs; }

    const T& rhs;
};

template <typename F>
constexpr auto WrapNoArgs() {
    return +[](const ArgsVector&) { F()(); };
}

template <typename F>
constexpr auto Wrap() {
    return +[](const ArgsVector& args) { F()(args); };
}

constexpr auto ViewToSlice(std::string_view view) -> leveldb::Slice { return {view.data(), view.size()}; }

constexpr auto GetInfo(Instruction inst) -> const InstructionInfo& {
    using KeyValue = std::pair<Instruction, InstructionInfo>;
    static constexpr std::array<KeyValue, 7> infos{{
        {Instruction::help, InstructionInfo("Print this help message", {}, WrapNoArgs<PrintHelpFunctor>())},
        {Instruction::exit, InstructionInfo("Exit the repl", {}, WrapNoArgs<ExitFunctor>())},
        {Instruction::close, InstructionInfo("Close database", {}, WrapNoArgs<CloseFunctor>(), true)},
        {Instruction::open, InstructionInfo("Open database", {"path", 1}, Wrap<OpenFunctor>())},
        {Instruction::read, InstructionInfo("Read value from database", {"key", 1}, Wrap<ReadFunctor>(), true)},
        {Instruction::write, InstructionInfo("Write value to database", {"key value", 2}, Wrap<WriteFunctor>(), true)},
        {Instruction::dump, InstructionInfo("Dump whole database", {}, WrapNoArgs<DumpFunctor>(), true)},
    }};
    return std::ranges::find_if(infos, Equals<Instruction>{inst}, &KeyValue::first)->second;
}

void PrintInvalidStateError(Instruction inst, std::string_view requirement) {
    println("error: {} requires {}", enchantum::to_string(inst), requirement);
}

void PrintSizeMismatchError(Instruction inst, size_t expected, size_t actual) {
    println("error: {} expected {} arguments got {}", enchantum::to_string(inst), expected, actual);
}

void PrintSyntaxError(std::string_view input, size_t pos, std::string_view error_msg) {
    size_t length = pos + 1;
    println("error: {}\n{}\n{:>{}}{:~^{}}", error_msg, input, '^', length, "", input.size() - length);
}

auto SliceInput(std::string_view input, size_t start, size_t end, bool erease_quotes) {
    auto length = end - start;
    if (erease_quotes) {
        start++;
        length -= 2;
    }
    return input.substr(start, length);
}

auto ParseInput(std::string_view input) -> std::optional<ArgsVector> {
    ArgsVector result;
    size_t pos{};
    struct Flags {
        bool skipping : 1;
        bool erease_quotes : 1;
    } flags{};

    char current;
    size_t quote_start;

    for (size_t i = 0; i < input.size(); i++) {
        // Catch single and double quotes
        if (input[i] == 0x22 || input[i] == 0x27) {
            if (flags.skipping) {
                // Don't end skipping we allow double quotes in single quotes and vice versa
                if (current != input[i]) continue;
                flags.erease_quotes = true;
            } else {
                quote_start = i;
                current = input[i];
            }

            flags.skipping = !flags.skipping;
            continue;
        }

        // Skip if we are skipping or not a space
        if (input[i] != 0x20 || flags.skipping) {
            continue;
        }

        result.push_back(SliceInput(input, pos, i, flags.erease_quotes));
        pos = i + 1;
        flags.erease_quotes = false;
    }

    if (flags.skipping) {
        PrintSyntaxError(input, quote_start, "Expected quotes or double quotes to be closed");
        return std::nullopt;
    }

    result.push_back(SliceInput(input, pos, input.size(), flags.erease_quotes));
    return result;
}

auto main() -> int {
    signal(SIGINT, [](int) {
        println("\nUser Interrupt");
        ExitFunctor()();
    });

    std::string user_input{};
    user_input.reserve(32);

    println("LevelDB R.E.P.L.");
    println("Type 'help' for more information.");
    for (;;) {
        user_input.clear();
        print(">>> ");
        std::getline(std::cin, user_input);
        if (user_input.empty()) {
            continue;
        }

        auto parsed = ParseInput(user_input);
        if (!parsed) continue;

        auto& args = parsed.value();
        if (args.empty()) continue;

        const auto instruction = enchantum::cast<Instruction>(args[0]);
        if (!instruction) {
            println("Unknown instruction '{}' !", args[0]);
            continue;
        }
        args.erase(args.begin());  // Remove instruction from args

        auto& info = GetInfo(*instruction);
        if (info.require_db && database == nullptr) {
            PrintInvalidStateError(*instruction, "Opened Database");
            continue;
        }

        if (!info.args.data.empty() && args.size() != info.args.size) {
            PrintSizeMismatchError(*instruction, info.args.size, args.size());
            continue;
        }

        info.impl(args);
    }
    return 0;
}