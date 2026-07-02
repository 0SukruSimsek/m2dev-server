#!/usr/bin/env python3
"""
NPCH (NPC Location Helper) patch script.
Applies NPCH changes to uzak (FreeBSD) server-src files.
Run on the remote server: python3 npch_patch.py
"""
import re
import sys
import os

BASE = "/usr/home/game/m2dev-server-src"

def read_file(path):
    with open(path, 'r', encoding='utf-8', errors='replace') as f:
        return f.read()

def write_file(path, content):
    with open(path, 'w', encoding='utf-8') as f:
        f.write(content)
    print(f"  PATCHED: {path}")

def patch_service_h():
    path = os.path.join(BASE, "src/common/service.h")
    content = read_file(path)
    if "ENABLE_NPC_LOCATION_HELPER" in content:
        print(f"  SKIP (already patched): {path}")
        return
    old = "#endif"
    new = "// NPC Location Helper (Faz 0 -- 2026-06-01)\n#define ENABLE_NPC_LOCATION_HELPER\n\n#endif"
    content = content.replace(old, new, 1)
    write_file(path, content)

def patch_item_length_h():
    path = os.path.join(BASE, "src/common/item_length.h")
    content = read_file(path)
    if "ENABLE_NPC_LOCATION_HELPER" in content:
        print(f"  SKIP (already patched): {path}")
        return
    # Find USE_PUT_INTO_RING_SOCKET line and insert after it
    idx = content.find("USE_PUT_INTO_RING_SOCKET,")
    if idx == -1:
        print(f"  ERROR: could not find USE_PUT_INTO_RING_SOCKET in {path}")
        return
    eol = content.find('\n', idx)
    content = (content[:eol+1] +
               "#ifdef ENABLE_NPC_LOCATION_HELPER\n"
               "\tUSE_MAP,\t\t\t\t\t\t// 31 NPC Location Helper map unlock item\n"
               "#endif\n" +
               content[eol+1:])
    write_file(path, content)

def patch_packet_headers_h():
    path = os.path.join(BASE, "src/common/packet_headers.h")
    content = read_file(path)
    if "NPC_LOCATION_HELPER" in content:
        print(f"  SKIP (already patched): {path}")
        return

    # 1. CG namespace: after HACK = 0x0B03
    old_cg = "    constexpr uint16_t HACK               = 0x0B03;\n\n    // Guild Marks"
    new_cg = ("    constexpr uint16_t HACK               = 0x0B03;\n"
              "#ifdef ENABLE_NPC_LOCATION_HELPER\n"
              "    constexpr uint16_t NPC_LOCATION_HELPER = 0x0D01;\n"
              "#endif\n\n    // Guild Marks")
    if old_cg not in content:
        print(f"  ERROR: CG HACK anchor not found in {path}")
        return
    content = content.replace(old_cg, new_cg, 1)

    # 2. GC namespace: after OBSERVER_MOVE = 0x0B22
    old_gc = "    constexpr uint16_t OBSERVER_MOVE      = 0x0B22;\n\n    // Guild Marks"
    new_gc = ("    constexpr uint16_t OBSERVER_MOVE      = 0x0B22;\n"
              "#ifdef ENABLE_NPC_LOCATION_HELPER\n"
              "    constexpr uint16_t NPC_LOCATION_HELPER = 0x0D01;\n"
              "#endif\n\n    // Guild Marks")
    if old_gc not in content:
        print(f"  ERROR: GC OBSERVER_MOVE anchor not found in {path}")
        return
    content = content.replace(old_gc, new_gc, 1)

    # 3. NPCLocationHelperSub namespace at end of file (before last line)
    npch_sub = (
        "\n\n#ifdef ENABLE_NPC_LOCATION_HELPER\n"
        "namespace NPCLocationHelperSub {\n"
        "    namespace CG { enum : uint8_t {\n"
        "        REQUEST_STATUS,\n"
        "        WARP_TO_NPC,\n"
        "        USE_TICKET,\n"
        "        REQUEST_GUILD_LAND,\n"
        "    }; }\n"
        "    namespace GC { enum : uint8_t {\n"
        "        STATUS,\n"
        "        WARP_RESULT,\n"
        "        GUILD_LAND,\n"
        "    }; }\n"
        "}\n"
        "#endif // ENABLE_NPC_LOCATION_HELPER\n"
    )
    # Append before end - find last closing brace of DragonSoulSub
    if "REFINE_SUCCEED" in content:
        # append after REFINE_SUCCEED }; }
        old_end = "    REFINE_SUCCEED,\n}; }"
        new_end = "    REFINE_SUCCEED,\n}; }" + npch_sub
        if old_end in content:
            content = content.replace(old_end, new_end, 1)
        else:
            content = content.rstrip() + npch_sub
    else:
        content = content.rstrip() + npch_sub

    write_file(path, content)

def patch_packet_structs_h():
    path = os.path.join(BASE, "src/game/packet_structs.h")
    content = read_file(path)
    if "TPacketCGNPCLocationHelper" in content:
        print(f"  SKIP (already patched): {path}")
        return
    npch_structs = (
        "\n#ifdef ENABLE_NPC_LOCATION_HELPER\n"
        "// CG packet (client -> server): NPC Location Helper request\n"
        "typedef struct SPacketCGNPCLocationHelper\n"
        "{\n"
        "\tuint16_t header;\n"
        "\tuint16_t length;\n"
        "\tuint8_t  subheader;\n"
        "\tuint8_t  pad[3];\n"
        "\tint32_t  mapIndex;\n"
        "\tuint32_t vnum;\n"
        "\tint32_t  x;\n"
        "\tint32_t  y;\n"
        "} TPacketCGNPCLocationHelper;\n"
        "\n"
        "// GC packet (server -> client): NPC Location Helper response\n"
        "typedef struct SPacketGCNPCLocationHelper\n"
        "{\n"
        "\tuint16_t header;\n"
        "\tuint16_t length;\n"
        "\tuint8_t  subheader;\n"
        "\tuint8_t  result;\n"
        "\tuint8_t  active;\n"
        "\tuint8_t  pad;\n"
        "\tuint32_t cooldownRemain;\n"
        "\tint32_t  mapIndex;\n"
        "\tuint32_t vnum;\n"
        "\tint32_t  x;\n"
        "\tint32_t  y;\n"
        "} TPacketGCNPCLocationHelper;\n"
        "#endif // ENABLE_NPC_LOCATION_HELPER\n"
    )
    # Insert before #pragma pack()
    old_end = "\n#pragma pack()"
    if old_end in content:
        content = content.replace(old_end, npch_structs + old_end, 1)
    else:
        content = content.rstrip() + npch_structs
    write_file(path, content)

def patch_input_h():
    path = os.path.join(BASE, "src/game/input.h")
    content = read_file(path)
    if "HandleNPCLocationHelper" in content:
        print(f"  SKIP (already patched): {path}")
        return
    # Insert before closing }; of CInputMain (after Refine declaration)
    anchor = "\t\tvoid\t\tRefine(LPCHARACTER ch, const char* c_pData);\n};"
    new_decl = ("\t\tvoid\t\tRefine(LPCHARACTER ch, const char* c_pData);\n"
                "#ifdef ENABLE_NPC_LOCATION_HELPER\n"
                "\t\tint\t\t\tHandleNPCLocationHelper(LPDESC d, const char* p);\n"
                "#endif\n"
                "};")
    if anchor in content:
        content = content.replace(anchor, new_decl, 1)
        write_file(path, content)
        return
    # Fallback: find HandleAntiFarm declaration
    anchor2 = "\t\tint\t\t\tHandleAntiFarm(LPDESC d, const char* p);"
    if anchor2 in content:
        content = content.replace(anchor2,
            anchor2 + "\n#ifdef ENABLE_NPC_LOCATION_HELPER\n\t\tint\t\t\tHandleNPCLocationHelper(LPDESC d, const char* p);\n#endif", 1)
        write_file(path, content)
        return
    # Fallback: HandleSwitchbot
    anchor3 = "\t\tint\t\t\tHandleSwitchbot(LPDESC d, const char* p);"
    if anchor3 in content:
        content = content.replace(anchor3,
            anchor3 + "\n#ifdef ENABLE_NPC_LOCATION_HELPER\n\t\tint\t\t\tHandleNPCLocationHelper(LPDESC d, const char* p);\n#endif", 1)
        write_file(path, content)
        return
    print(f"  ERROR: no insertion point found in {path}")

def patch_input_main_cpp():
    path = os.path.join(BASE, "src/game/input_main.cpp")
    content = read_file(path)
    if "HandleNPCLocationHelper" in content:
        print(f"  SKIP (already patched): {path}")
        return

    # 1. Include block: after DragonSoul.h include
    incl_anchor = '#include "DragonSoul.h"'
    npch_include = ('\n#ifdef ENABLE_NPC_LOCATION_HELPER\n'
                    '#include "npc_location_helper.h"\n'
                    '#endif')
    if incl_anchor in content:
        content = content.replace(incl_anchor, incl_anchor + npch_include, 1)
    else:
        print(f"  WARNING: DragonSoul.h include not found in {path}, trying alternate anchor")
        # Try locale_service
        incl_anchor2 = '#include "locale_service.h"'
        if incl_anchor2 in content:
            content = content.replace(incl_anchor2, incl_anchor2 + npch_include, 1)
        else:
            print(f"  ERROR: no include anchor found in {path}")
            return

    # 2. RegisterHandlers: insert reg() before closing }
    # Anchor: last reg() call before RegisterHandlers closes
    reg_anchor = ('reg(CG::QUEST_CONFIRM,      &CInputMain::SimpleHandlerV<&CInputMain::QuestConfirm>);\n'
                  '}')
    reg_new = ('reg(CG::QUEST_CONFIRM,      &CInputMain::SimpleHandlerV<&CInputMain::QuestConfirm>);\n'
               '#ifdef ENABLE_NPC_LOCATION_HELPER\n'
               '\treg(CG::NPC_LOCATION_HELPER, &CInputMain::HandleNPCLocationHelper);\n'
               '#endif\n'
               '}')
    if reg_anchor in content:
        content = content.replace(reg_anchor, reg_new, 1)
    else:
        print(f"  ERROR: RegisterHandlers QUEST_CONFIRM anchor not found in {path}")
        return

    # 3. Append handler implementation at end of file
    handler_impl = (
        "\n// ---------------------------------------------------------------------------\n"
        "// NPC Location Helper handler\n"
        "// ---------------------------------------------------------------------------\n"
        "#ifdef ENABLE_NPC_LOCATION_HELPER\n"
        "int CInputMain::HandleNPCLocationHelper(LPDESC d, const char* p)\n"
        "{\n"
        "\tLPCHARACTER ch = d->GetCharacter();\n"
        "\tif (!ch) return 0;\n"
        "\n"
        "\tconst TPacketCGNPCLocationHelper* pkt = reinterpret_cast<const TPacketCGNPCLocationHelper*>(p);\n"
        "\tif (!pkt) return 0;\n"
        "\n"
        "\tCNpcLocationHelperManager::instance().HandlePacket(ch, *pkt);\n"
        "\treturn 0;\n"
        "}\n"
        "#endif // ENABLE_NPC_LOCATION_HELPER\n"
    )
    content = content.rstrip() + "\n" + handler_impl
    write_file(path, content)

def patch_packet_info_cpp():
    path = os.path.join(BASE, "src/game/packet_info.cpp")
    content = read_file(path)
    if "NPC_LOCATION_HELPER" in content:
        print(f"  SKIP (already patched): {path}")
        return
    # Insert after STATE_CHECKER Set()
    anchor = 'Set(CG::STATE_CHECKER, sizeof(TPacketCGStateCheck), "ServerStateCheck");'
    new_line = (anchor + "\n"
                "#ifdef ENABLE_NPC_LOCATION_HELPER\n"
                '\tSet(CG::NPC_LOCATION_HELPER, sizeof(TPacketCGNPCLocationHelper), "NPCLocationHelper");\n'
                "#endif")
    if anchor in content:
        content = content.replace(anchor, new_line, 1)
    else:
        print(f"  ERROR: STATE_CHECKER anchor not found in {path}")
        return
    write_file(path, content)

def patch_main_cpp():
    path = os.path.join(BASE, "src/game/main.cpp")
    content = read_file(path)
    if "npc_location_helper" in content:
        print(f"  SKIP (already patched): {path}")
        return

    # 1. Include: after DragonSoul.h
    incl_anchor = '#include "DragonSoul.h"'
    npch_include = ('\n#ifdef ENABLE_NPC_LOCATION_HELPER\n'
                    '#include "npc_location_helper.h"\n'
                    '#endif')
    if incl_anchor in content:
        content = content.replace(incl_anchor, incl_anchor + npch_include, 1)
    else:
        print(f"  ERROR: DragonSoul.h include not found in {path}")
        return

    # 2. Initialize call: after OXEvent_manager.Initialize();
    init_anchor = "OXEvent_manager.Initialize();"
    npch_init = ("\n#ifdef ENABLE_NPC_LOCATION_HELPER\n"
                 "\tCNpcLocationHelperManager::instance().Initialize();\n"
                 "#endif")
    if init_anchor in content:
        content = content.replace(init_anchor, init_anchor + npch_init, 1)
    else:
        # Try alternate
        init_anchor2 = "fishing::Initialize();"
        if init_anchor2 in content:
            content = content.replace(init_anchor2, init_anchor2 + npch_init, 1)
        else:
            print(f"  ERROR: Initialize anchor not found in {path}")
            return
    write_file(path, content)

def patch_char_item_cpp():
    path = os.path.join(BASE, "src/game/char_item.cpp")
    content = read_file(path)
    if "npc_location_helper" in content:
        print(f"  SKIP (already patched): {path}")
        return

    # 1. Include: after belt_inventory_helper.h
    incl_anchor = '#include "belt_inventory_helper.h"'
    npch_include = ('\n#ifdef ENABLE_NPC_LOCATION_HELPER\n'
                    '#include "npc_location_helper.h"\n'
                    '#endif')
    if incl_anchor in content:
        content = content.replace(incl_anchor, incl_anchor + npch_include, 1)
    else:
        print(f"  ERROR: belt_inventory_helper.h include not found in {path}")
        return

    # 2. Activator check: insert before `if (item->GetVnum() > 50800`
    activate_anchor = "\t\t\tif (item->GetVnum() > 50800 && item->GetVnum() <= 50820)"
    npch_activate = (
        "#ifdef ENABLE_NPC_LOCATION_HELPER\n"
        "\t\t\t\tif (item->GetVnum() == 79667) // NPC_LOCATION_HELPER_ACTIVATOR\n"
        "\t\t\t\t{\n"
        "\t\t\t\t\tif (!CNpcLocationHelperManager::instance().IsActive(this))\n"
        "\t\t\t\t\t\titem->SetCount(item->GetCount() - 1);\n"
        "\t\t\t\t\tCNpcLocationHelperManager::instance().Activate(this);\n"
        "\t\t\t\t\treturn true;\n"
        "\t\t\t\t}\n"
        "#endif // ENABLE_NPC_LOCATION_HELPER\n"
    )
    if activate_anchor in content:
        content = content.replace(activate_anchor, npch_activate + activate_anchor, 1)
    else:
        print(f"  ERROR: activate anchor (50800) not found in {path}")
        return

    # 3. USE_MAP case: insert after AutoGiveItem break before closing }
    # Pattern: AutoGiveItem(item->GetValue(0));\n\t\t\t\t\t}\n\t\t\t\t\tbreak;\n\t\t\t\t}
    use_map_anchor = "AutoGiveItem(item->GetValue(0));\n\t\t\t\t\t}\n\t\t\t\t\tbreak;\n\t\t\t\t}"
    npch_use_map = (
        "AutoGiveItem(item->GetValue(0));\n"
        "\t\t\t\t\t}\n"
        "\t\t\t\t\tbreak;\n"
        "#ifdef ENABLE_NPC_LOCATION_HELPER\n"
        "\t\t\t\tcase USE_MAP:\n"
        "\t\t\t\t\t{\n"
        "\t\t\t\t\t\tif (item->GetVnum() >= 79800 && item->GetVnum() <= 79822)\n"
        "\t\t\t\t\t\t{\n"
        "\t\t\t\t\t\t\tif (CNpcLocationHelperManager::instance().UnlockMapByItem(this, item->GetVnum(), item->GetValue(0)))\n"
        "\t\t\t\t\t\t\t\titem->SetCount(item->GetCount() - 1);\n"
        "\t\t\t\t\t\t\treturn true;\n"
        "\t\t\t\t\t\t}\n"
        "\t\t\t\t\t}\n"
        "\t\t\t\t\tbreak;\n"
        "#endif // ENABLE_NPC_LOCATION_HELPER\n"
        "\t\t\t\t}"
    )
    if use_map_anchor in content:
        content = content.replace(use_map_anchor, npch_use_map, 1)
    else:
        print(f"  WARNING: USE_MAP anchor not found in {path}, skipping USE_MAP case")
    write_file(path, content)

def patch_proto_reader_cpp():
    path = os.path.join(BASE, "src/db/ProtoReader.cpp")
    content = read_file(path)
    if "USE_MAP" in content:
        print(f"  SKIP (already patched): {path}")
        return
    # Find last USE_PUT_INTO_RING_SOCKET"}; pattern
    target = '"USE_PUT_INTO_RING_SOCKET"};'
    idx = content.find(target)
    if idx != -1:
        new_text = '"USE_PUT_INTO_RING_SOCKET",\n#ifdef ENABLE_NPC_LOCATION_HELPER\n\t\t\t\t"USE_MAP"\n#endif\n\t\t\t\t};'
        content = content.replace(target, new_text, 1)
        write_file(path, content)
        return
    # Try variant without space differences
    idx = content.find('"USE_PUT_INTO_RING_SOCKET"}')
    if idx != -1:
        eol = content.find('\n', idx)
        line_end = content[idx:eol]
        print(f"  INFO: found ring socket at {idx}: {repr(line_end[:50])}")
    print(f"  ERROR: USE_PUT_INTO_RING_SOCKET anchor not found in {path}")
    return

def main():
    print("=== NPCH Patch Script ===")
    print(f"Target: {BASE}")
    print()

    steps = [
        ("service.h",       patch_service_h),
        ("item_length.h",   patch_item_length_h),
        ("packet_headers.h",patch_packet_headers_h),
        ("packet_structs.h",patch_packet_structs_h),
        ("input.h",         patch_input_h),
        ("input_main.cpp",  patch_input_main_cpp),
        ("packet_info.cpp", patch_packet_info_cpp),
        ("main.cpp",        patch_main_cpp),
        ("char_item.cpp",   patch_char_item_cpp),
        ("ProtoReader.cpp", patch_proto_reader_cpp),
    ]

    for name, fn in steps:
        print(f"[{name}]")
        try:
            fn()
        except Exception as e:
            print(f"  EXCEPTION: {e}")
            import traceback
            traceback.print_exc()
        print()

    print("=== Done ===")

if __name__ == "__main__":
    main()
