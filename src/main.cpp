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

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "runtime.h"
#include "runtime_arm.h"
#include "emerald_extended_view.h"

extern "C" void gf_ReadFlash1(void);
extern "C" void gf_ReadFlash_Core(void);

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

#if defined(GBARECOMP_VARIANT_EMERALD)
bool ram_matches_rom(uint32_t ram_pc, uint32_t rom_pc, uint32_t size) {
    for (uint32_t offset = 0; offset < size; ++offset) {
        if (bus_read_u8(ram_pc + offset) != bus_read_u8(rom_pc + offset)) {
            return false;
        }
    }
    return true;
}

// Emerald copies two position-independent flash routines from ROM to moving
// stack slots. Fixed RAM dispatch aliases would be unsafe because those slots
// are reused. Canonicalize only byte-for-byte matches against the hash-gated
// ROM routines; ReadFlash1 additionally has a stable live callback pointer.
int emerald_ram_dispatch(uint32_t pc, int thumb) {
    constexpr uint32_t kReadFlash1Rom = 0x082E1A6Cu;
    constexpr uint32_t kReadFlash1Callback = 0x03007844u;
    constexpr uint32_t kReadFlashCoreRom = 0x082E1AB0u;
    constexpr uint32_t kReadFlashCoreSize = 0x22u;

    if (!thumb) return 0;
    if (bus_read_u32(kReadFlash1Callback) == (pc | 1u) &&
        ram_matches_rom(pc, kReadFlash1Rom, 4u)) {
        gf_ReadFlash1();
        return 1;
    }
    if (ram_matches_rom(pc, kReadFlashCoreRom, kReadFlashCoreSize)) {
        gf_ReadFlash_Core();
        return 1;
    }
    return 0;
}

#endif

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

#if defined(GBARECOMP_VARIANT_EMERALD)
    g_runtime_ram_dispatch_hook = &emerald_ram_dispatch;
#endif

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
#if defined(GBARECOMP_VARIANT_EMERALD)
    opts.mod_game_id       = "pokemon-emerald-us";
    opts.mod_owns_adaptive_view = true;
    opts.max_view_width = emerald::kMaxViewWidth;
    opts.max_resize_view_width = emerald::kMaxViewWidth;
    opts.max_resize_view_height = emerald::kMaxViewHeight;
    opts.resize_driven_view = true;
    opts.freely_resizable_window = true;
    opts.extended_view_init = emerald::install_extended_view;
    opts.extended_view_frame = emerald::update_extended_view;
#endif
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
