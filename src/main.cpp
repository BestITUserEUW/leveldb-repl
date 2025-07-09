#include <cstdint>
#include <cstdlib>
#include <csignal>
#include <iostream>
#include <memory>
#include <string>
#include <print>

#include <leveldb/db.h>

#include <oryx/enchantum.hpp>
#include <oryx/string_split.hpp>
#include <utility>
#include <vector>

using std::print;
using std::println;

#define EXPECT_DB_OPENED(db)                         \
    do {                                             \
        if (!db) {                                   \
            println("Expected database to be open"); \
            return;                                  \
        }                                            \
    } while (0)

#define EXPECT_ARGS_SIZE(args, len)                 \
    do {                                            \
        if (args.size() != len) {                   \
            println("Expected {} arguments!", len); \
            return;                                 \
        }                                           \
    } while (0)

std::unique_ptr<leveldb::DB> db;

enum class Instruction : uint8_t { help, exit, open, close, read, write, dump };

struct InstructionInfo {
    std::string_view description;
    std::string_view args;
};

using ArgsVector = std::vector<std::string_view>;

constexpr auto ViewToSlice(std::string_view view) { return leveldb::Slice(view.data(), view.size()); }

auto GetInstructionInfo(Instruction inst) -> InstructionInfo {
    switch (inst) {
        using enum Instruction;
        case help:
            return {.description = "Print this help message", .args = ""};
        case exit:
            return {.description = "Exit the repl", .args = ""};
        case open:
            return {.description = "Open or create a database", .args = "path"};
        case close:
            return {.description = "Close database", .args = "path"};
        case read:
            return {.description = "Read value from database", .args = "key"};
        case write:
            return {.description = "Write value to database", .args = "key value"};
        case dump:
            return {.description = "Dump whole database", .args = ""};
        default:
            std::unreachable();
    }
}

auto ParseInput(std::string_view input) -> ArgsVector {
    static auto slice = [](std::string_view input, size_t pos, size_t idx, bool erease_quotes) {
        auto length = idx - pos;
        if (erease_quotes) {
            pos++;
            length -= 2;
        }
        return input.substr(pos, length);
    };

    ArgsVector result;
    size_t pos{};
    struct Flags {
        bool skipping : 1;
        bool erease_quotes : 1;
    } flags{};

    char current{};

    for (size_t i = 0; i < input.size(); i++) {
        // Catch single and double quotes
        if (input[i] == 0x22 || input[i] == 0x27) {
            if (flags.skipping) {
                // Don't end skipping we allow double quotes in single quotes and vice versa
                if (current != input[i]) continue;
                flags.erease_quotes = true;
            } else {
                current = input[i];
            }

            flags.skipping = !flags.skipping;
            continue;
        }

        // Skip if we are skipping or not a space
        if (input[i] != 0x20 || flags.skipping) {
            continue;
        }

        result.push_back(slice(input, pos, i, flags.erease_quotes));
        pos = i + 1;
        flags.erease_quotes = false;
    }

    result.push_back(slice(input, pos, input.size(), flags.erease_quotes));
    return result;
}

[[noreturn]] void Exit() {
    if (db) {
        db.reset();
    }
    std::exit(EXIT_SUCCESS);
}

void PrintHelp() {
    constexpr auto kPrintFmt = "{:<15}{:<20}{:<20}";

    println("Help\n");
    println("Input format is: <instruction> <args>");
    println("Example: open ./database.ldb\n");
    println(kPrintFmt, "Instruction", "Arguments", "Description");
    enchantum::for_each<Instruction>([=](auto c) {
        const auto info = GetInstructionInfo(c.value);
        println(kPrintFmt, enchantum::to_string(c.value), info.args, info.description);
    });
}

void Open(const ArgsVector& args) {
    EXPECT_ARGS_SIZE(args, 1);

    leveldb::Options opts{};
    opts.create_if_missing = true;
    opts.reuse_logs = true;
    auto path = std::string(args[0]);

    const auto status = leveldb::DB::Open(opts, path, std::out_ptr(db));
    if (!status.ok()) {
        println("open db: {} failure: {}!", path, status.ToString());
    }
    println("OK");
}

void Close() {
    EXPECT_DB_OPENED(db);
    db.reset();
    println("Closed database");
}

void Dump() {
    EXPECT_DB_OPENED(db);

    auto it = std::unique_ptr<leveldb::Iterator>(db->NewIterator(leveldb::ReadOptions()));
    for (it->SeekToFirst(); it->Valid(); it->Next()) {
        println("{}: {}", it->key().ToString(), it->value().ToString());
    }
}

void Write(const ArgsVector& args) {
    EXPECT_ARGS_SIZE(args, 2);
    EXPECT_DB_OPENED(db);

    auto key = args[0];
    auto value = args[1];

    leveldb::WriteOptions opts{};
    opts.sync = true;
    const auto status = db->Put(opts, ViewToSlice(key), ViewToSlice(value));
    if (!status.ok()) {
        println("write key: {} value: {} failure: {}!", key, value, status.ToString());
        return;
    }
    println("OK");
}

void Read(const ArgsVector& args) {
    EXPECT_ARGS_SIZE(args, 1);
    EXPECT_DB_OPENED(db);

    auto key = args[0];
    leveldb::ReadOptions opts{};
    std::string result;
    const auto status = db->Get(opts, ViewToSlice(key), &result);
    if (!status.ok()) {
        println("read key: {} failure: {}!", key, status.ToString());
        return;
    }
    println("{}", result);
}

void HandleInstruction(Instruction inst, const ArgsVector& args) {
    switch (inst) {
        using enum Instruction;
        case help:
            PrintHelp();
            return;
        case exit:
            Exit();
        case open:
            Open(args);
            return;
        case close:
            Close();
            return;
        case read:
            Read(args);
            return;
        case write:
            Write(args);
            return;
        case dump:
            Dump();
            return;
        default:
            std::unreachable();
    }
}

void MainLoop() {
    std::string user_input;
    user_input.reserve(64);

    for (;;) {
        user_input.clear();
        print(">>> ");
        std::getline(std::cin, user_input);
        if (user_input.empty()) {
            continue;
        }

        auto args = ParseInput(user_input);
        if (args.size() < 1) {
            println("Cannot understand this syntax!");
            continue;
        }

        const auto instruction = enchantum::cast<Instruction>(args[0]);
        if (!instruction) {
            println("Unknown instruction {}!", args[0]);
            continue;
        }
        args.erase(args.begin());  // Remove instruction from args
        HandleInstruction(instruction.value(), args);
    }
}

auto main() -> int {
    signal(SIGINT, [](int) {
        println("\nUser Interrupt");
        if (db) {
            db.reset();
        }
        std::exit(EXIT_SUCCESS);
    });

    println("LevelDB R.E.P.L.");
    println("Type 'help' for more information.");
    MainLoop();
    return 0;
}