// main.cpp — FRLG multi-variant entry point (FireRed / LeafGreen).
//
// One source file backs every variant; the build picks the game via
// compile-defs set in CMakeLists.txt (add_gba_variant):
//
//   GBARECOMP_BUILTIN_NAME      e.g. "Pokemon FireRed (USA)"
//   GBARECOMP_BUILTIN_SHA1      expected ROM sha1 (hash gate)
//   GBARECOMP_DEFAULT_GAME_CONFIG  variants/<name>/game.toml
//   GBARECOMP_DEFAULT_DEBUG_PORT / GBARECOMP_WINDOW_TITLE  (read by runtime)
//
// Every gbarecomp game binary takes BOTH a BIOS and a ROM at launch
// (see ../gbarecomp/PRINCIPLES.md "BIOS is sacred"). The CLI accepts:
//
//   <Variant>Recomp [--bios <path>] [--rom <path>] [game.toml]
//
// All three are optional on the command line; missing values are pulled
// from game.toml. Hashes are verified before any code runs.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "runtime.h"
#include "runtime_arm.h"
#include "emerald_extended_view.h"

#ifndef GBARECOMP_BUILTIN_NAME
#define GBARECOMP_BUILTIN_NAME "GBA cartridge"
#endif
#ifndef GBARECOMP_BUILTIN_SHA1
#define GBARECOMP_BUILTIN_SHA1 ""
#endif
#ifndef GBARECOMP_WINDOW_TITLE
#define GBARECOMP_WINDOW_TITLE "gbarecomp"
#endif
#ifndef GBARECOMP_BUILTIN_CRC32
#define GBARECOMP_BUILTIN_CRC32 0
#endif
#ifndef GBARECOMP_BUILTIN_REGION
#define GBARECOMP_BUILTIN_REGION ""
#endif
#ifndef GBARECOMP_BOXART
#define GBARECOMP_BOXART ""
#endif

#if defined(GBAGAME_RECOMP_UI)
#include "game_launcher_boot.h"
#endif

namespace {

// MOTHER 3's FLASH512_V131 library runs its flash-reading routines from a
// buffer on the stack (code that reads flash may not run from the cartridge
// bus), copying them there on every call:
//   ReadFlash1       0x080911D4, 4 bytes (ldrb r0,[r0]; bx lr), copied by
//                    0x080911D8 and called through the pointer at 0x02015E14
//                    by the write/erase status polls, thousands of times per save
//   ReadFlash_Core   0x08091218, 0x24 bytes, copied by 0x0809123C
//   VerifyFlash_Core 0x080912D8, 0x30 bytes, copied by 0x0809130A, 0x080913A2
// The slot moves with the call depth and is reused for other data, so fixed
// RAM entries would be unsafe, and every call ran on the interpreter bridge,
// as did every IRQ return into a loop (the library times flash operations
// with a timer IRQ): ~20 PCs at 0x03006C1C..0x03006D80 and about a million
// interpreted instructions per save. The routines are position independent,
// so a RAM PC inside a byte-exact copy runs the static translation of the ROM
// routine at the same offset (static_resume_all gives every instruction an
// entry). ReadFlash1 is too short to identify by its bytes alone and must
// also be the copy the library's pointer names. Anything else falls through
// to normal dispatch. Same idea as EmeraldRecomp's ReadFlash1/ReadFlash_Core
// hook, plus mid-routine resumes.
struct StackRoutine {
    uint32_t rom;
    uint32_t size;
    uint32_t live_pointer;  // holds (copy | 1) while the copy is live; 0 = none
};
constexpr StackRoutine kFlashStackRoutines[] = {
    {0x080911D4u, 0x04u, 0x02015E14u},  // ReadFlash1
    {0x08091218u, 0x24u, 0u},           // ReadFlash_Core
    {0x080912D8u, 0x30u, 0u},           // VerifyFlash_Core
};

bool ram_matches_rom(uint32_t ram, uint32_t rom, uint32_t size) {
    for (uint32_t i = 0; i < size; i += 2) {
        if (bus_read_u16(ram + i) != bus_read_u16(rom + i)) return false;
    }
    return true;
}

int mother3_ram_dispatch(uint32_t pc, int thumb) {
    pc &= ~1u;
    if (!thumb || pc < 0x03000000u || pc >= 0x03008000u) return 0;
    const uint16_t opcode = bus_read_u16(pc);
    for (const StackRoutine& routine : kFlashStackRoutines) {
        for (uint32_t offset = 0; offset < routine.size; offset += 2) {
            const uint32_t copy = pc - offset;
            if (opcode != bus_read_u16(routine.rom + offset) ||
                (routine.live_pointer &&
                 bus_read_u32(routine.live_pointer) != (copy | 1u)) ||
                !ram_matches_rom(copy, routine.rom, routine.size))
                continue;
            runtime_dispatch(routine.rom + offset);
            return 1;
        }
    }
    return 0;
}

void print_usage() {
    std::printf(
        "%s [--bios <path>] [--rom <path>] [game.toml]\n"
        "\n"
        "Both BIOS and ROM are required (either via flags or via the\n"
        "[bios] / [rom] sections of game.toml). The runtime refuses\n"
        "to start unless both hash-verify.\n"
        "\n"
        "Default BIOS path: ../gbarecomp/bios/gba_bios.bin\n"
        "Default game config: " GBARECOMP_DEFAULT_GAME_CONFIG " (relative to CWD)\n",
        GBARECOMP_WINDOW_TITLE);
}

}  // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--help") == 0 ||
            std::strcmp(argv[i], "-h") == 0) {
            print_usage();
            return 0;
        }
    }

    g_runtime_ram_dispatch_hook = &mother3_ram_dispatch;

    // Built-in defaults so a standalone <Variant>Recomp.exe ships without
    // a sibling game.toml. The asset picker still validates against these
    // values; CLI / TOML can override.
    gbarecomp::RunOptions opts;
    opts.builtin_game_name = GBARECOMP_BUILTIN_NAME;
    opts.builtin_rom_sha1  = (sizeof(GBARECOMP_BUILTIN_SHA1) > 1)
                                 ? GBARECOMP_BUILTIN_SHA1
                                 : nullptr;
    // CRC32 of the pinned ROM (same dump the SHA-1 gates on); the
    // launcher's GAME card uses it for its "ROM verified" check.
    opts.builtin_rom_crc32 = GBARECOMP_BUILTIN_CRC32;
    opts.mod_game_id       = "pokemon-emerald-us";
    opts.mod_owns_adaptive_view = true;
    opts.max_view_width = emerald::kMaxViewWidth;
    opts.max_resize_view_width = emerald::kMaxViewWidth;
    opts.max_resize_view_height = emerald::kMaxViewHeight;
    opts.resize_driven_view = true;
    opts.freely_resizable_window = true;
    opts.extended_view_init = emerald::install_extended_view;
    opts.extended_view_frame = emerald::update_extended_view;
    opts.launcher_expose_widescreen = false;
    opts.launcher_expose_adaptive_view = false;
    opts.launcher_region   = (sizeof(GBARECOMP_BUILTIN_REGION) > 1)
                                 ? GBARECOMP_BUILTIN_REGION
                                 : nullptr;
    opts.launcher_boxart = (sizeof(GBARECOMP_BOXART) > 1)
                               ? GBARECOMP_BOXART
                               : nullptr;
    opts.launcher_game_config = GBARECOMP_DEFAULT_GAME_CONFIG;  // prefill ROM/BIOS

#if defined(GBAGAME_RECOMP_UI)
    std::vector<std::string> args(argv, argv + argc);
    if (game_launcher_preboot(args, opts)) return 0;   // user quit the launcher
    std::vector<char*> av;
    av.reserve(args.size());
    for (auto& s : args) av.push_back(s.data());
    return gbarecomp::run_game(static_cast<int>(av.size()), av.data(), opts);
#else
    return gbarecomp::run_game(argc, argv, opts);
#endif
}
