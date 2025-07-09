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

std::unique_ptr<leveldb::DB> db;

enum class Instruction : uint8_t { help, exit, open, close, read, write, dump };

using ArgsVector = std::vector<std::string_view>;

auto ViewToSlice(std::string_view view) { return leveldb::Slice(view.data(), view.size()); }

[[noreturn]] void Exit() {
    if (db) {
        db.reset();
    }
    std::exit(EXIT_SUCCESS);
}

auto GetInstructionDescription(Instruction inst) -> std::string_view {
    switch (inst) {
        using enum Instruction;
        case help:
            return "Print this help message";
        case exit:
            return "Exit the repl";
        case open:
            return "Open or create a database";
        case close:
            return "Close database";
        case read:
            return "Read value from database";
        case write:
            return "Write value to database";
        case dump:
            return "Dump whole database";
        default:
            return "N.A.";
    }
}

auto GetInstructionArgs(Instruction inst) -> std::string_view {
    switch (inst) {
        using enum Instruction;
        case help:
            return "";
        case exit:
            return "";
        case open:
            return "path";
        case close:
            return "";
        case read:
            return "key";
        case write:
            return "key value";
        case dump:
            return "";
        default:
            return "N.A.";
    }
}

void PrintHelp() {
    constexpr auto kPrintFmt = "{:<15}{:<20}{:<20}";

    println("Help\n");
    println("Input format is: <instruction> <args>");
    println("Example: open ./database.ldb\n");
    println(kPrintFmt, "Instruction", "Arguments", "Description");
    enchantum::for_each<Instruction>([=](auto c) {
        println(kPrintFmt, enchantum::to_string(c.value), GetInstructionArgs(c.value),
                GetInstructionDescription(c.value));
    });
}

void OpenDb(const ArgsVector& args) {
    if (args.size() != 1) {
        println("open expects one argument!");
        return;
    }

    leveldb::Options opts{};
    opts.create_if_missing = true;
    opts.reuse_logs = true;
    auto path = std::string(args[0]);

    const auto status = leveldb::DB::Open(opts, path, std::out_ptr(db));
    if (!status.ok()) {
        println("open db: {} failure: {}!", path, status.ToString());
    }
    println("Opened database: {}", path);
}

void CloseDb() {
    if (!db) {
        println("No openend database present!");
        return;
    }

    db.reset();
    println("Closed database");
}

void DumpDb() {
    if (!db) {
        println("No opened database present!");
        return;
    }

    auto it = std::unique_ptr<leveldb::Iterator>(db->NewIterator(leveldb::ReadOptions()));
    for (it->SeekToFirst(); it->Valid(); it->Next()) {
        println("{}: {}", it->key().ToString(), it->value().ToString());
    }
}

void WriteDb(const ArgsVector& args) {
    if (args.size() != 2) {
        println("write expects two arguments!");
        return;
    }

    if (!db) {
        println("No opened database present!");
        return;
    }

    auto key = args[0];
    auto value = args[1];

    leveldb::WriteOptions opts{};
    opts.sync = true;
    const auto status = db->Put(opts, ViewToSlice(key), ViewToSlice(value));
    if (!status.ok()) {
        println("write key: {} value: {} failure: {}!", key, value, status.ToString());
        return;
    }
    println("write key: {} value: {}", key, value);
}

void ReadDb(const ArgsVector& args) {
    if (args.size() != 1) {
        println("read expects one argument!");
        return;
    }

    if (!db) {
        println("No opened database present!");
        return;
    }

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
            OpenDb(args);
            return;
        case close:
            CloseDb();
            return;
        case read:
            ReadDb(args);
            return;
        case write:
            WriteDb(args);
            return;
        case dump:
            DumpDb();
            return;
        default:
            std::unreachable();
    }
}

void MainLoop() {
    std::string user_input;

    for (;;) {
        user_input.clear();
        print(">>> ");
        std::getline(std::cin, user_input);
        if (user_input.empty()) {
            continue;
        }

        auto args = oryx::StringSplit(user_input, ' ');
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