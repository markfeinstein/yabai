#include "../../scripts/macho_parser.h"
#include "../../scripts/scan_sa_core.h"

#include <assert.h>
#include <limits.h>
#include <mach-o/fixup-chains.h>
#include <mach-o/loader.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IMAGE_SIZE 1024

static struct mach_header_64 *header(uint8_t *image) { return (void *) image; }
static struct segment_command_64 *segment(uint8_t *image) { return (void *) (image + sizeof(struct mach_header_64)); }
static struct section_64 *section(uint8_t *image) { return (void *) (image + sizeof(struct mach_header_64) + sizeof(struct segment_command_64)); }

static void make_image(uint8_t image[IMAGE_SIZE])
{
    memset(image, 0, IMAGE_SIZE);
    struct mach_header_64 h = { MH_MAGIC_64, CPU_TYPE_ARM64, CPU_SUBTYPE_ARM64_ALL, MH_EXECUTE,
        1, sizeof(struct segment_command_64) + sizeof(struct section_64), 0, 0 };
    struct segment_command_64 seg = { .cmd = LC_SEGMENT_64,
        .cmdsize = sizeof(struct segment_command_64) + sizeof(struct section_64),
        .vmaddr = 0x100000, .vmsize = IMAGE_SIZE, .fileoff = 0, .filesize = IMAGE_SIZE, .nsects = 1 };
    struct section_64 sec = { .addr = 0x100200, .size = 32, .offset = 512 };
    memcpy(seg.segname, "__DATA", 7); memcpy(sec.segname, "__DATA", 7);
    memcpy(sec.sectname, "__objc_selrefs", 15);
    memcpy(image, &h, sizeof(h)); memcpy(image + sizeof(h), &seg, sizeof(seg));
    memcpy(image + sizeof(h) + sizeof(seg), &sec, sizeof(sec));
}

static void make_single_command(uint8_t image[IMAGE_SIZE], const void *command, uint32_t size)
{
    memset(image, 0, IMAGE_SIZE);
    struct mach_header_64 h = { MH_MAGIC_64, CPU_TYPE_ARM64, CPU_SUBTYPE_ARM64_ALL, MH_EXECUTE, 1, size, 0, 0 };
    memcpy(image, &h, sizeof(h)); memcpy(image + sizeof(h), command, size);
}

static void test_headers_policy_and_commands(void)
{
    uint8_t image[IMAGE_SIZE]; make_image(image);
    assert(macho_validate_arm64(image, sizeof(image)) && macho_validate_scanner_image(image, sizeof(image)));
    assert(!macho_validate_arm64(image, sizeof(struct mach_header_64) - 1));
    make_image(image); header(image)->magic = MH_MAGIC; assert(!macho_validate_arm64(image, sizeof(image)));
    make_image(image); header(image)->cputype = CPU_TYPE_X86_64; assert(!macho_validate_arm64(image, sizeof(image)));
    make_image(image); header(image)->filetype = MH_DYLIB;
    assert(macho_validate_arm64(image, sizeof(image)) && !macho_validate_scanner_image(image, sizeof(image)));
    make_image(image); header(image)->cpusubtype = CPU_SUBTYPE_ARM64E | CPU_SUBTYPE_LIB64;
    assert(macho_validate_scanner_image(image, sizeof(image)));
    make_image(image); header(image)->cpusubtype = 7; assert(!macho_validate_scanner_image(image, sizeof(image)));
    make_image(image); header(image)->ncmds = 0; assert(!macho_validate_arm64(image, sizeof(image)));
    make_image(image); header(image)->ncmds = 2; assert(!macho_validate_arm64(image, sizeof(image)));
    make_image(image); segment(image)->cmdsize = 0; assert(!macho_validate_arm64(image, sizeof(image)));
    make_image(image); segment(image)->cmdsize = 9; assert(!macho_validate_arm64(image, sizeof(image)));
    make_image(image); segment(image)->cmdsize = UINT32_MAX; assert(!macho_validate_arm64(image, sizeof(image)));

    struct { struct build_version_command command; struct build_tool_version tool; } build = {
        { LC_BUILD_VERSION, sizeof(build), PLATFORM_MACOS, 0, 0, 1 }, { TOOL_CLANG, 1 } };
    make_single_command(image, &build, sizeof(build)); assert(macho_validate_arm64(image, sizeof(image)));
    build.command.cmdsize = sizeof(build.command); make_single_command(image, &build, sizeof(build.command));
    assert(!macho_validate_arm64(image, sizeof(image)));

    struct note_command note = { LC_NOTE, sizeof(note), "test", 900, 124 };
    make_single_command(image, &note, sizeof(note)); assert(macho_validate_arm64(image, sizeof(image)));
    note.cmdsize = 8; make_single_command(image, &note, 8); assert(!macho_validate_arm64(image, sizeof(image)));
    note.cmdsize = sizeof(note); note.size = 125; make_single_command(image, &note, sizeof(note));
    assert(!macho_validate_arm64(image, sizeof(image)));

    struct { struct dylib_command command; char name[8]; } dylib = {0};
    dylib.command.cmd = LC_LOAD_DYLIB; dylib.command.cmdsize = sizeof(dylib);
    dylib.command.dylib.name.offset = sizeof(struct dylib_command); memcpy(dylib.name, "lib.dyl", 8);
    make_single_command(image, &dylib, sizeof(dylib)); assert(macho_validate_arm64(image, sizeof(image)));
    dylib.command.dylib.name.offset = sizeof(dylib); make_single_command(image, &dylib, sizeof(dylib));
    assert(!macho_validate_arm64(image, sizeof(image)));

    struct { struct rpath_command command; char path[20]; } rpath = {0};
    rpath.command.cmd=LC_RPATH; rpath.command.cmdsize=sizeof(rpath);
    rpath.command.path.offset=sizeof(rpath.command); memcpy(rpath.path,"@loader_path",13);
    make_single_command(image,&rpath,sizeof(rpath)); assert(macho_validate_arm64(image,sizeof(image)));
    memset(rpath.path,'x',sizeof(rpath.path)); make_single_command(image,&rpath,sizeof(rpath));
    assert(!macho_validate_arm64(image,sizeof(image)));

    struct { struct dylinker_command command; char name[20]; } dylinker = {0};
    dylinker.command.cmd=LC_LOAD_DYLINKER; dylinker.command.cmdsize=sizeof(dylinker);
    dylinker.command.name.offset=sizeof(dylinker.command); memcpy(dylinker.name,"/usr/lib/dyld",14);
    make_single_command(image,&dylinker,sizeof(dylinker)); assert(macho_validate_arm64(image,sizeof(image)));
    dylinker.command.name.offset=sizeof(dylinker); make_single_command(image,&dylinker,sizeof(dylinker));
    assert(!macho_validate_arm64(image,sizeof(image)));

    struct { struct fileset_entry_command command; char id[8]; } fileset = {0};
    fileset.command.cmd=LC_FILESET_ENTRY; fileset.command.cmdsize=sizeof(fileset);
    fileset.command.fileoff=512; fileset.command.entry_id.offset=sizeof(fileset.command);
    memcpy(fileset.id,"entry",6); make_single_command(image,&fileset,sizeof(fileset));
    assert(macho_validate_arm64(image,sizeof(image)));
    fileset.command.entry_id.offset=sizeof(fileset); make_single_command(image,&fileset,sizeof(fileset));
    assert(!macho_validate_arm64(image,sizeof(image)));

    struct { struct linker_option_command command; char strings[12]; } options = {
        { LC_LINKER_OPTION, sizeof(options), 2 }, "-lfoo\0-lbar" };
    make_single_command(image, &options, sizeof(options)); assert(macho_validate_arm64(image, sizeof(image)));
    options.command.count = 3; make_single_command(image, &options, sizeof(options)); assert(!macho_validate_arm64(image, sizeof(image)));

    struct { struct thread_command command; uint32_t flavor, count, state[2]; } thread = {
        { LC_THREAD, sizeof(thread) }, 1, 2, { 3, 4 } };
    make_single_command(image, &thread, sizeof(thread)); assert(macho_validate_arm64(image, sizeof(image)));
    thread.count = 20; make_single_command(image, &thread, sizeof(thread)); assert(!macho_validate_arm64(image, sizeof(image)));

    struct load_command unknown = { 0x777777, sizeof(unknown) };
    make_single_command(image, &unknown, sizeof(unknown)); assert(macho_validate_arm64(image, sizeof(image)));
}

static void test_object_and_file_ranges(void)
{
    uint8_t image[IMAGE_SIZE]; uint64_t address;
    memset(image,0,sizeof(image));
    struct mach_header_64 h={MH_MAGIC_64,CPU_TYPE_ARM64,CPU_SUBTYPE_ARM64_ALL,MH_OBJECT,1,
        sizeof(struct segment_command_64)+2*sizeof(struct section_64),0,0};
    struct segment_command_64 seg={.cmd=LC_SEGMENT_64,.cmdsize=h.sizeofcmds,.vmaddr=0x1000,
        .vmsize=0x100,.fileoff=400,.filesize=300,.nsects=2};
    struct section_64 sections[2]={{.addr=0x1000,.size=32,.offset=512,.reloff=700,.nreloc=1},
                                   {.addr=0x1080,.size=24,.offset=600}};
    memcpy(sections[0].segname,"__TEXT",7); memcpy(sections[0].sectname,"__text",7);
    memcpy(sections[1].segname,"__DATA",7); memcpy(sections[1].sectname,"__data",7);
    memcpy(image,&h,sizeof(h)); memcpy(image+sizeof(h),&seg,sizeof(seg));
    memcpy(image+sizeof(h)+sizeof(seg),sections,sizeof(sections));
    assert(macho_validate_arm64(image,sizeof(image)));
    assert(!macho_file_offset_to_vm_address(image,sizeof(image),512,&address));
    struct section_64 *object_sections=(void *)(image+sizeof(h)+sizeof(seg));
    object_sections[1].offset=690;
    assert(!macho_validate_arm64(image,sizeof(image)));
    memcpy(object_sections,sections,sizeof(sections)); object_sections[1].addr=0x10f8;
    assert(!macho_validate_arm64(image,sizeof(image)));
    memcpy(object_sections,sections,sizeof(sections));
    ((struct segment_command_64 *)(image+sizeof(h)))->vmaddr=UINT64_MAX-8;
    assert(!macho_validate_arm64(image,sizeof(image)));
    memcpy(image+sizeof(h),&seg,sizeof(seg)); memcpy(object_sections,sections,sizeof(sections));
    object_sections[0].reloff=IMAGE_SIZE-4;
    assert(!macho_validate_arm64(image,sizeof(image)));

    struct segment_command oldseg={.cmd=LC_SEGMENT,.cmdsize=sizeof(oldseg)};
    make_single_command(image,&oldseg,sizeof(oldseg)); assert(!macho_validate_arm64(image,sizeof(image)));
    struct symtab_command sym={LC_SYMTAB,sizeof(sym),500,1,600,8};
    make_single_command(image,&sym,sizeof(sym)); assert(macho_validate_arm64(image,sizeof(image)));
    sym.symoff=IMAGE_SIZE-4; make_single_command(image,&sym,sizeof(sym)); assert(!macho_validate_arm64(image,sizeof(image)));
    sym.symoff=500; sym.stroff=IMAGE_SIZE-4; sym.strsize=8; make_single_command(image,&sym,sizeof(sym));
    assert(!macho_validate_arm64(image,sizeof(image)));

    struct dysymtab_command dys={0}; dys.cmd=LC_DYSYMTAB; dys.cmdsize=sizeof(dys);
#define BAD_DYS(off,count) do { memset(&dys,0,sizeof(dys)); dys.cmd=LC_DYSYMTAB; dys.cmdsize=sizeof(dys); \
    dys.off=IMAGE_SIZE-2; dys.count=1; make_single_command(image,&dys,sizeof(dys)); \
    assert(!macho_validate_arm64(image,sizeof(image))); } while (0)
    BAD_DYS(tocoff,ntoc); BAD_DYS(modtaboff,nmodtab); BAD_DYS(extrefsymoff,nextrefsyms);
    BAD_DYS(indirectsymoff,nindirectsyms); BAD_DYS(extreloff,nextrel); BAD_DYS(locreloff,nlocrel);
#undef BAD_DYS
    struct dyld_info_command info={0}; info.cmd=LC_DYLD_INFO_ONLY; info.cmdsize=sizeof(info);
#define BAD_INFO(off,sz) do { memset(&info,0,sizeof(info)); info.cmd=LC_DYLD_INFO_ONLY; info.cmdsize=sizeof(info); \
    info.off=IMAGE_SIZE-4; info.sz=8; make_single_command(image,&info,sizeof(info)); \
    assert(!macho_validate_arm64(image,sizeof(image))); } while (0)
    BAD_INFO(rebase_off,rebase_size); BAD_INFO(bind_off,bind_size); BAD_INFO(weak_bind_off,weak_bind_size);
    BAD_INFO(lazy_bind_off,lazy_bind_size); BAD_INFO(export_off,export_size);
#undef BAD_INFO
    struct symseg_command ss={LC_SYMSEG,sizeof(ss),IMAGE_SIZE-4,8};
    make_single_command(image,&ss,sizeof(ss)); assert(!macho_validate_arm64(image,sizeof(image)));
    struct twolevel_hints_command hints={LC_TWOLEVEL_HINTS,sizeof(hints),IMAGE_SIZE-2,1};
    make_single_command(image,&hints,sizeof(hints)); assert(!macho_validate_arm64(image,sizeof(image)));
    struct encryption_info_command_64 enc={LC_ENCRYPTION_INFO_64,sizeof(enc),IMAGE_SIZE-4,8,0,0};
    make_single_command(image,&enc,sizeof(enc)); assert(!macho_validate_arm64(image,sizeof(image)));
    struct entry_point_command maincmd={LC_MAIN,sizeof(maincmd),IMAGE_SIZE,0};
    make_single_command(image,&maincmd,sizeof(maincmd)); assert(!macho_validate_arm64(image,sizeof(image)));
}

static void make_sym_dys(uint8_t image[IMAGE_SIZE], const struct symtab_command *sym,
                         const struct dysymtab_command *dys, unsigned symtabs)
{
    memset(image,0,IMAGE_SIZE);
    struct mach_header_64 h={MH_MAGIC_64,CPU_TYPE_ARM64,CPU_SUBTYPE_ARM64_ALL,MH_EXECUTE,
        symtabs+1,(uint32_t)(symtabs*sizeof(*sym)+sizeof(*dys)),0,0};
    memcpy(image,&h,sizeof(h)); size_t cursor=sizeof(h);
    for (unsigned index=0;index<symtabs;++index) { memcpy(image+cursor,sym,sizeof(*sym)); cursor+=sizeof(*sym); }
    memcpy(image+cursor,dys,sizeof(*dys));
}

static void make_main_image(uint8_t image[IMAGE_SIZE], uint64_t entryoff,
                            unsigned text_segments, bool named_text)
{
    memset(image,0,IMAGE_SIZE); size_t commands=text_segments*sizeof(struct segment_command_64)+sizeof(struct entry_point_command);
    struct mach_header_64 h={MH_MAGIC_64,CPU_TYPE_ARM64,CPU_SUBTYPE_ARM64_ALL,MH_EXECUTE,
        text_segments+1,(uint32_t)commands,0,0}; memcpy(image,&h,sizeof(h)); size_t cursor=sizeof(h);
    for (unsigned index=0;index<text_segments;++index) {
        struct segment_command_64 segment={.cmd=LC_SEGMENT_64,.cmdsize=sizeof(segment),
            .vmaddr=0x100000+index*0x1000,.vmsize=200,.fileoff=400+index*200,.filesize=200};
        memcpy(segment.segname,named_text?"__TEXT":"__DATA",7); memcpy(image+cursor,&segment,sizeof(segment)); cursor+=sizeof(segment);
    }
    struct entry_point_command main_command={LC_MAIN,sizeof(main_command),entryoff,0}; memcpy(image+cursor,&main_command,sizeof(main_command));
}

static void test_cross_command_consistency(void)
{
    uint8_t image[IMAGE_SIZE];
    struct symtab_command sym={LC_SYMTAB,sizeof(sym),500,10,700,16};
    struct dysymtab_command dys={0}; dys.cmd=LC_DYSYMTAB; dys.cmdsize=sizeof(dys);
    dys.nlocalsym=2; dys.iextdefsym=2; dys.nextdefsym=3; dys.iundefsym=5; dys.nundefsym=5;
    make_sym_dys(image,&sym,&dys,1); assert(macho_validate_arm64(image,sizeof(image)));

    memset(&dys.ilocalsym,0,6*sizeof(uint32_t));
    dys.iextdefsym=0; dys.nextdefsym=3; dys.iundefsym=3; dys.nundefsym=7;
    make_sym_dys(image,&sym,&dys,1); assert(macho_validate_arm64(image,sizeof(image)));
    memset(&dys.ilocalsym,0,6*sizeof(uint32_t));
    dys.nlocalsym=2; dys.iextdefsym=2; dys.iundefsym=2; dys.nundefsym=8;
    make_sym_dys(image,&sym,&dys,1); assert(macho_validate_arm64(image,sizeof(image)));
    memset(&dys.ilocalsym,0,6*sizeof(uint32_t));
    dys.nlocalsym=2; dys.iextdefsym=2; dys.nextdefsym=8; dys.iundefsym=10;
    make_sym_dys(image,&sym,&dys,1); assert(macho_validate_arm64(image,sizeof(image)));
    sym.nsyms=0; memset(&dys.ilocalsym,0,6*sizeof(uint32_t));
    make_sym_dys(image,&sym,&dys,1); assert(macho_validate_arm64(image,sizeof(image)));
    sym.nsyms=10;

    dys.nlocalsym=4; dys.iextdefsym=3; dys.nextdefsym=2; dys.iundefsym=5; dys.nundefsym=5;
    make_sym_dys(image,&sym,&dys,1); assert(!macho_validate_arm64(image,sizeof(image)));
    dys.nlocalsym=2; dys.iextdefsym=3; dys.nextdefsym=2; dys.iundefsym=5; dys.nundefsym=5;
    make_sym_dys(image,&sym,&dys,1); assert(!macho_validate_arm64(image,sizeof(image)));
    dys.nlocalsym=4; dys.iextdefsym=2; dys.nextdefsym=3; dys.iundefsym=5; dys.nundefsym=5;
    make_sym_dys(image,&sym,&dys,1); assert(!macho_validate_arm64(image,sizeof(image)));
    dys.nlocalsym=2; dys.iextdefsym=2; dys.nextdefsym=3; dys.iundefsym=5; dys.nundefsym=4;
    make_sym_dys(image,&sym,&dys,1); assert(!macho_validate_arm64(image,sizeof(image)));
    dys.nlocalsym=2; dys.iextdefsym=2; dys.nextdefsym=3; dys.iundefsym=UINT32_MAX; dys.nundefsym=2;
    make_sym_dys(image,&sym,&dys,1); assert(!macho_validate_arm64(image,sizeof(image)));

    dys.iundefsym=5; dys.nundefsym=5;
    make_sym_dys(image,&sym,&dys,0); assert(!macho_validate_arm64(image,sizeof(image)));
    make_sym_dys(image,&sym,&dys,2); assert(!macho_validate_arm64(image,sizeof(image)));
    make_sym_dys(image,&sym,&dys,1); header(image)->ncmds=3; header(image)->sizeofcmds+=sizeof(dys);
    memcpy(image+sizeof(struct mach_header_64)+sizeof(sym)+sizeof(dys),&dys,sizeof(dys));
    assert(!macho_validate_arm64(image,sizeof(image)));

    make_main_image(image,199,1,true); assert(macho_validate_arm64(image,sizeof(image)));
    make_main_image(image,200,1,true); assert(!macho_validate_arm64(image,sizeof(image)));
    make_main_image(image,300,1,true); assert(!macho_validate_arm64(image,sizeof(image)));
    make_main_image(image,UINT64_MAX,1,true); assert(!macho_validate_arm64(image,sizeof(image)));
    make_main_image(image,1,1,false); assert(!macho_validate_arm64(image,sizeof(image)));
    make_main_image(image,1,2,true); assert(!macho_validate_arm64(image,sizeof(image)));

    struct linkedit_data_command data={LC_DATA_IN_CODE,sizeof(data),500,3};
    make_single_command(image,&data,sizeof(data)); assert(!macho_validate_arm64(image,sizeof(image)));
    data.datasize=sizeof(struct data_in_code_entry); make_single_command(image,&data,sizeof(data));
    assert(macho_validate_arm64(image,sizeof(image)));
}

static void test_sections_and_addresses(void)
{
    uint8_t image[IMAGE_SIZE]; uint64_t offset, size, address;
    make_image(image); assert(macho_find_section(image, sizeof(image), "__DATA", "__objc_selrefs", &offset, &size));
    assert(offset == 512 && size == 32);
    make_image(image); section(image)->addr = UINT64_MAX - 4; section(image)->size = 8;
    assert(!macho_validate_arm64(image, sizeof(image)));
    make_image(image);
    struct section_64 second = *section(image); memcpy(second.sectname,"__unrequested",14);
    second.addr=UINT64_MAX-4; second.size=8;
    segment(image)->nsects=2; segment(image)->cmdsize+=sizeof(second); header(image)->sizeofcmds+=sizeof(second);
    memcpy(image+sizeof(struct mach_header_64)+sizeof(struct segment_command_64)+sizeof(struct section_64),&second,sizeof(second));
    assert(!macho_validate_arm64(image,sizeof(image)));
    make_image(image); section(image)->offset = 513;
    assert(!macho_validate_arm64(image, sizeof(image)));
    make_image(image); section(image)->offset = IMAGE_SIZE - 4; section(image)->addr = 0x100000 + IMAGE_SIZE - 4; section(image)->size = 8;
    assert(!macho_validate_arm64(image, sizeof(image)));
    make_image(image); segment(image)->filesize = IMAGE_SIZE; segment(image)->vmsize = IMAGE_SIZE - 1;
    assert(!macho_validate_arm64(image, sizeof(image)) && !macho_file_offset_to_vm_address(image, sizeof(image), 900, &address));

    make_image(image); segment(image)->vmsize = 4096; section(image)->flags = S_ZEROFILL;
    section(image)->addr = 0x100200; section(image)->size = 2048; section(image)->offset = UINT32_MAX;
    assert(macho_validate_arm64(image, sizeof(image)));
    assert(!macho_find_section(image, sizeof(image), "__DATA", "__objc_selrefs", &offset, &size));

    make_image(image); section(image)->reloff = IMAGE_SIZE - 4; section(image)->nreloc = 1;
    assert(!macho_validate_arm64(image, sizeof(image)));
    make_image(image); section(image)->reloff = UINT32_MAX; section(image)->nreloc = UINT32_MAX;
    assert(!macho_validate_arm64(image, sizeof(image)));

    make_image(image); assert(macho_file_offset_to_vm_address(image, sizeof(image), 512, &address));
    assert(address == 0x100200);
    memset(segment(image)->segname, 'A', 16); memset(section(image)->segname, 'A', 16);
    memset(section(image)->sectname, 'B', 16);
    assert(macho_find_section(image, sizeof(image), "AAAAAAAAAAAAAAAA", "BBBBBBBBBBBBBBBB", &offset, &size));
    assert(!macho_find_section(image, sizeof(image), "AAAAAAAAAAAAAAA", "BBBBBBBBBBBBBBBB", &offset, &size));
}

static void test_patterns_and_allocations(void)
{
    struct macho_byte_pattern pattern; uint64_t first; size_t count;
    const uint8_t bytes[] = { 0xaa, 0x10, 0xcc, 0xaa, 0x20, 0xcc, 0xaa, 0x30, 0xcc };
    assert(!macho_parse_pattern("", &pattern)); assert(!macho_parse_pattern("AA G0", &pattern));
    assert(!macho_parse_pattern("AA A", &pattern)); assert(macho_parse_pattern("AA ?8 CC", &pattern));
    assert(macho_pattern_matches(bytes, sizeof(bytes), &pattern)); macho_free_pattern(&pattern);
    assert(macho_test_outstanding_allocations() == 0);
    macho_test_set_allocation_failure(0); assert(!macho_parse_pattern("AA BB", &pattern));
    assert(macho_test_outstanding_allocations() == 0);
    macho_test_set_allocation_failure(1); assert(!macho_parse_pattern("AA BB", &pattern));
    assert(macho_test_outstanding_allocations() == 0);
    macho_test_set_allocation_failure(SIZE_MAX);
    assert(scan_sa_pattern(bytes, sizeof(bytes), 0, 4, "AA ?? CC", &first, &count));
    assert(first == 0 && count == 2); /* starts 0 and 3; start 4+ is excluded */
    assert(scan_sa_pattern(bytes, sizeof(bytes), 3, 1, "AA ?? CC", &first, &count) && count == 1);
    assert(scan_sa_pattern(bytes, sizeof(bytes), 3, 0, "AA ?? CC", &first, &count) && count == 0);
    const uint8_t boundary[] = { 0, 0, 0, 0x7f, 0x7f };
    assert(scan_sa_pattern(boundary, sizeof(boundary), 0, 4, "7F", &first, &count));
    assert(first == 3 && count == 1);
    assert(scan_sa_pattern(boundary, sizeof(boundary), 4, 1, "7F", &first, &count) && first == 4 && count == 1);
    assert(scan_sa_pattern(boundary, sizeof(boundary), 0, sizeof(boundary), "FF", &first, &count) && count == 0);
    bool grouped;
    const char *first_only[]={"7F","AA"};
    const char *second_only[]={"AA","7F"};
    const char *neither[]={"AA","BB"};
    const char *both[]={"7F","7F"};
    assert(scan_sa_pattern_group(boundary,sizeof(boundary),3,1,first_only,2,&grouped) && grouped);
    assert(scan_sa_pattern_group(boundary,sizeof(boundary),3,1,second_only,2,&grouped) && grouped);
    assert(scan_sa_pattern_group(boundary,sizeof(boundary),3,1,neither,2,&grouped) && !grouped);
    assert(scan_sa_pattern_group(boundary,sizeof(boundary),3,1,both,2,&grouped) && grouped);
    macho_test_set_allocation_failure(1);
    assert(!scan_sa_pattern(boundary, sizeof(boundary), 0, sizeof(boundary), "7F", &first, &count));
    assert(macho_test_outstanding_allocations() == 0);
    macho_test_set_allocation_failure(SIZE_MAX);
    assert(macho_test_outstanding_allocations() == 0);
}

static size_t make_selector_image(uint8_t image[IMAGE_SIZE], bool chained)
{
    memset(image, 0, IMAGE_SIZE);
    size_t text_size = sizeof(struct segment_command_64) + 2 * sizeof(struct section_64);
    size_t data_size = sizeof(struct segment_command_64) + sizeof(struct section_64);
    size_t fix_size = chained ? sizeof(struct linkedit_data_command) : 0;
    struct mach_header_64 h = { MH_MAGIC_64, CPU_TYPE_ARM64, CPU_SUBTYPE_ARM64E, MH_EXECUTE,
        chained ? 3U : 2U, (uint32_t) (text_size + data_size + fix_size), 0, 0 };
    size_t cursor = sizeof(h); memcpy(image, &h, sizeof(h));
    struct segment_command_64 text = { .cmd=LC_SEGMENT_64, .cmdsize=(uint32_t)text_size,
        .vmaddr=0x100000, .vmsize=768, .fileoff=0, .filesize=768, .nsects=2 };
    memcpy(text.segname, "__TEXT", 7); memcpy(image+cursor, &text, sizeof(text));
    struct section_64 meth = { .addr=0x100200, .size=48, .offset=512 };
    memcpy(meth.segname,"__TEXT",7); memcpy(meth.sectname,"__objc_methname",16);
    struct section_64 cstr = { .addr=0x100240, .size=32, .offset=576 };
    memcpy(cstr.segname,"__TEXT",7); memcpy(cstr.sectname,"__cstring",10);
    memcpy(image+cursor+sizeof(text),&meth,sizeof(meth)); memcpy(image+cursor+sizeof(text)+sizeof(meth),&cstr,sizeof(cstr));
    cursor += text_size;
    struct segment_command_64 data = { .cmd=LC_SEGMENT_64, .cmdsize=(uint32_t)data_size,
        .vmaddr=0x200000, .vmsize=256, .fileoff=768, .filesize=256, .nsects=1 };
    memcpy(data.segname,"__DATA",7); memcpy(image+cursor,&data,sizeof(data));
    struct section_64 refs = { .addr=0x200020, .size=chained ? 8 : 16, .offset=800 };
    memcpy(refs.segname,"__DATA",7); memcpy(refs.sectname,"__objc_selrefs",15);
    memcpy(image+cursor+sizeof(data),&refs,sizeof(refs)); cursor += data_size;
    memcpy(image+512,"dup:",5); memcpy(image+528,"dup:",5);
    if (!chained) {
        uint64_t collision=0xdeadbeef00100200ULL, actual=0x100210; /* second copy at 528 */
        memcpy(image+800,&collision,8); memcpy(image+808,&actual,8);
        return cursor;
    }
    struct linkedit_data_command fix = { LC_DYLD_CHAINED_FIXUPS, sizeof(fix), 900, 80 };
    memcpy(image+cursor,&fix,sizeof(fix));
    struct dyld_chained_fixups_header fh = { .fixups_version=0, .starts_offset=sizeof(fh) };
    memcpy(image+900,&fh,sizeof(fh));
    uint8_t *starts=image+900+sizeof(fh); uint32_t seg_count=2, offsets[2]={0,12};
    memcpy(starts,&seg_count,4); memcpy(starts+4,offsets,8);
    struct dyld_chained_starts_in_segment info = { .size=24, .page_size=0x1000,
        .pointer_format=DYLD_CHAINED_PTR_ARM64E_USERLAND, .segment_offset=0x100000,
        .page_count=1, .page_start={32} };
    memcpy(starts+12,&info,24);
    uint64_t encoded=528; memcpy(image+800,&encoded,8);
    return cursor+sizeof(fix);
}

static void test_selector_resolution(void)
{
    uint8_t image[IMAGE_SIZE]; uint64_t address, pointer;
    make_selector_image(image,false); assert(macho_validate_scanner_image(image,sizeof(image)));
    assert(scan_sa_find_selector(image,sizeof(image),"dup:",&address)); assert(address==0x200020+8);
    assert(macho_resolve_pointer(image,sizeof(image),800,&pointer)==MACHO_POINTER_RESOLVED && pointer==0xdeadbeef00100200ULL);

    make_selector_image(image,true); assert(macho_validate_scanner_image(image,sizeof(image)));
    assert(macho_resolve_pointer(image,sizeof(image),800,&pointer)==MACHO_POINTER_RESOLVED && pointer==0x100210);
    assert(scan_sa_find_selector(image,sizeof(image),"dup:",&address));
    uint64_t malformed=(1ULL<<62)|528; memcpy(image+800,&malformed,8);
    assert(macho_resolve_pointer(image,sizeof(image),800,&pointer)!=MACHO_POINTER_RESOLVED);
    assert(!scan_sa_find_selector(image,sizeof(image),"dup:",&address));

    make_selector_image(image,true);
    size_t format_offset=900+sizeof(struct dyld_chained_fixups_header)+12+
                         offsetof(struct dyld_chained_starts_in_segment,pointer_format);
    uint16_t format=DYLD_CHAINED_PTR_64_OFFSET; memcpy(image+format_offset,&format,sizeof(format));
    uint64_t offset_rebase=528; memcpy(image+800,&offset_rebase,8);
    assert(macho_resolve_pointer(image,sizeof(image),800,&pointer)==MACHO_POINTER_RESOLVED && pointer==0x100210);
    format=0xffff; memcpy(image+format_offset,&format,sizeof(format));
    assert(macho_resolve_pointer(image,sizeof(image),800,&pointer)!=MACHO_POINTER_RESOLVED);
    make_selector_image(image,true);
    struct dyld_chained_fixups_header bad_header; memcpy(&bad_header,image+900,sizeof(bad_header));
    bad_header.starts_offset=65; memcpy(image+900,&bad_header,sizeof(bad_header));
    assert(macho_resolve_pointer(image,sizeof(image),800,&pointer)!=MACHO_POINTER_RESOLVED);
    make_selector_image(image,true);
    uint16_t bad_start=0x1000;
    memcpy(image+900+sizeof(struct dyld_chained_fixups_header)+12+
           offsetof(struct dyld_chained_starts_in_segment,page_start),&bad_start,sizeof(bad_start));
    assert(macho_resolve_pointer(image,sizeof(image),800,&pointer)!=MACHO_POINTER_RESOLVED);

    make_selector_image(image,false); section(image); /* malformed selref size */
    size_t second=sizeof(struct mach_header_64)+(sizeof(struct segment_command_64)+2*sizeof(struct section_64));
    struct section_64 *refs=(void *)(image+second+sizeof(struct segment_command_64)); refs->size=9;
    assert(macho_validate_arm64(image,sizeof(image)));
    assert(!scan_sa_find_selector(image,sizeof(image),"dup:",&address));
}

static void set_chain_format(uint8_t *image, uint16_t format)
{
    size_t offset=900+sizeof(struct dyld_chained_fixups_header)+12+
                  offsetof(struct dyld_chained_starts_in_segment,pointer_format);
    memcpy(image+offset,&format,sizeof(format));
}

static void test_chained_multi_and_formats(void)
{
    uint8_t image[IMAGE_SIZE]; uint64_t target, sentinel;
    size_t info_offset=900+sizeof(struct dyld_chained_fixups_header)+12;
    size_t page_offset=info_offset+offsetof(struct dyld_chained_starts_in_segment,page_start);
    make_selector_image(image,true);
    uint32_t info_size=23; memcpy(image+info_offset,&info_size,4);
    assert(macho_resolve_pointer(image,sizeof(image),800,&target)==MACHO_POINTER_MALFORMED);
    make_selector_image(image,true);
    info_size=28; memcpy(image+info_offset,&info_size,4);
    uint16_t entries[3]={DYLD_CHAINED_PTR_START_MULTI|1,32,48|DYLD_CHAINED_PTR_START_LAST};
    memcpy(image+page_offset,entries,sizeof(entries));
    uint64_t encoded=528; memcpy(image+816,&encoded,8);
    assert(macho_resolve_pointer(image,sizeof(image),816,&target)==MACHO_POINTER_RESOLVED && target==0x100210);
    info_size=27; memcpy(image+info_offset,&info_size,4);
    assert(macho_resolve_pointer(image,sizeof(image),816,&target)==MACHO_POINTER_MALFORMED);
    info_size=28; memcpy(image+info_offset,&info_size,4);
    entries[0]=DYLD_CHAINED_PTR_START_MULTI|3; memcpy(image+page_offset,entries,sizeof(entries));
    assert(macho_resolve_pointer(image,sizeof(image),816,&target)==MACHO_POINTER_MALFORMED);
    make_selector_image(image,true); info_size=26; memcpy(image+info_offset,&info_size,4);
    uint16_t missing[2]={DYLD_CHAINED_PTR_START_MULTI|1,48}; memcpy(image+page_offset,missing,sizeof(missing));
    assert(macho_resolve_pointer(image,sizeof(image),816,&target)==MACHO_POINTER_MALFORMED);

    make_selector_image(image,true); set_chain_format(image,DYLD_CHAINED_PTR_64_OFFSET);
    uint64_t next4=(2ULL<<51); memcpy(image+800,&next4,8); encoded=528; memcpy(image+808,&encoded,8);
    assert(macho_resolve_pointer(image,sizeof(image),808,&target)==MACHO_POINTER_RESOLVED && target==0x100210);
    make_selector_image(image,true);
    uint64_t auth_next=(1ULL<<63)|(1ULL<<51); memcpy(image+800,&auth_next,8); memcpy(image+808,&encoded,8);
    sentinel=0xfeedface; target=sentinel;
    assert(macho_resolve_pointer(image,sizeof(image),800,&target)==MACHO_POINTER_UNSUPPORTED && target==sentinel);
    assert(macho_resolve_pointer(image,sizeof(image),808,&target)==MACHO_POINTER_RESOLVED && target==0x100210);
    uint64_t bind_next=(1ULL<<62)|(1ULL<<51); memcpy(image+800,&bind_next,8);
    assert(macho_resolve_pointer(image,sizeof(image),808,&target)==MACHO_POINTER_RESOLVED);
    size_t second=sizeof(struct mach_header_64)+(sizeof(struct segment_command_64)+2*sizeof(struct section_64));
    struct section_64 *refs=(void *)(image+second+sizeof(struct segment_command_64)); refs->size=16;
    uint64_t address; assert(scan_sa_find_selector(image,sizeof(image),"dup:",&address));

    make_selector_image(image,true);
    struct linkedit_data_command *fix=(void *)(image+second+sizeof(struct segment_command_64)+sizeof(struct section_64));
    fix->datasize=104;
    uint8_t *starts=image+900+sizeof(struct dyld_chained_fixups_header);
    uint32_t offsets[2]={12,36}; memcpy(starts+4,offsets,sizeof(offsets));
    struct dyld_chained_starts_in_segment data_info; memcpy(&data_info,starts+12,24);
    struct dyld_chained_starts_in_segment text_info=data_info;
    text_info.segment_offset=0; text_info.page_start[0]=512;
    memcpy(starts+12,&text_info,24); memcpy(starts+36,&data_info,24);
    memcpy(image+512,&auth_next,8);
    assert(macho_resolve_pointer(image,sizeof(image),800,&target)==MACHO_POINTER_RESOLVED && target==0x100210);

    const uint64_t high8=0xab;
    make_selector_image(image,true); set_chain_format(image,DYLD_CHAINED_PTR_64);
    encoded=0x123|(high8<<36); memcpy(image+800,&encoded,8);
    assert(macho_resolve_pointer(image,sizeof(image),800,&target)==MACHO_POINTER_RESOLVED && target==0xab00000000000123ULL);
    make_selector_image(image,true); set_chain_format(image,DYLD_CHAINED_PTR_64_OFFSET);
    encoded=0x123|(high8<<36); memcpy(image+800,&encoded,8);
    assert(macho_resolve_pointer(image,sizeof(image),800,&target)==MACHO_POINTER_RESOLVED && target==0xab00000000100123ULL);
    make_selector_image(image,true);
    encoded=0x123|(high8<<43); memcpy(image+800,&encoded,8);
    assert(macho_resolve_pointer(image,sizeof(image),800,&target)==MACHO_POINTER_RESOLVED && target==0xab00000000100123ULL);
}

static int write_success_scanner_fixture(const char *path)
{
    const size_t total=0x40000, data_file=0x3f000, strings_file=0x1000, refs_file=0x3f100;
    uint8_t *image=calloc(total,1); if (!image) return 2;
    size_t seg_size=sizeof(struct segment_command_64)+sizeof(struct section_64);
    struct mach_header_64 h={MH_MAGIC_64,CPU_TYPE_ARM64,CPU_SUBTYPE_ARM64E,MH_EXECUTE,2,
        (uint32_t)(2*seg_size),0,0}; memcpy(image,&h,sizeof(h)); size_t cursor=sizeof(h);
    struct segment_command_64 text={.cmd=LC_SEGMENT_64,.cmdsize=(uint32_t)seg_size,
        .vmaddr=0x100000,.vmsize=data_file,.fileoff=0,.filesize=data_file,.nsects=1};
    memcpy(text.segname,"__TEXT",7); memcpy(image+cursor,&text,sizeof(text));
    struct section_64 meth={.addr=0x101000,.size=512,.offset=strings_file};
    memcpy(meth.segname,"__TEXT",7); memcpy(meth.sectname,"__objc_methname",16);
    memcpy(image+cursor+sizeof(text),&meth,sizeof(meth)); cursor+=seg_size;
    struct segment_command_64 data={.cmd=LC_SEGMENT_64,.cmdsize=(uint32_t)seg_size,
        .vmaddr=0x200000,.vmsize=total-data_file,.fileoff=data_file,.filesize=total-data_file,.nsects=1};
    memcpy(data.segname,"__DATA",7); memcpy(image+cursor,&data,sizeof(data));
    struct section_64 refs={.addr=0x200100,.size=5*sizeof(uint64_t),.offset=refs_file};
    memcpy(refs.segname,"__DATA",7); memcpy(refs.sectname,"__objc_selrefs",15);
    memcpy(image+cursor+sizeof(data),&refs,sizeof(refs));
    const char *names[]={"_handleEvent:","addSpace:forDisplayUUID:","doBindingCommand:display:",
        "moveSpace:toDisplay:displayUUID:","removeSpace:"};
    size_t string_cursor=strings_file;
    for (size_t index=0;index<5;++index) {
        size_t length=strlen(names[index])+1; memcpy(image+string_cursor,names[index],length);
        uint64_t address=0x100000+string_cursor; memcpy(image+refs_file+index*8,&address,8);
        string_cursor+=length;
    }
    struct macho_byte_pattern pattern={0};
    const char *dock="88 ?? ?? ?? 08 01 19 91 00 01 40 F9 E2 03 13 AA ?? ?? ?? 94 88 ?? ?? ?? 08 E1 1A 91 00 01 40 F9";
    if (!macho_parse_pattern(dock,&pattern)) { free(image); return 2; }
    for (size_t index=0;index<pattern.length;++index) image[0x30000+index]=pattern.wildcards[index]?0:pattern.bytes[index];
    macho_free_pattern(&pattern);
    FILE *file=fopen(path,"wb"); if (!file) { free(image); return 2; }
    bool written=fwrite(image,1,total,file)==total; free(image);
    return fclose(file)==0&&written?0:2;
}

int main(int argc, char **argv)
{
    if (argc == 3 && strcmp(argv[1], "--write-success-scanner-fixture") == 0)
        return write_success_scanner_fixture(argv[2]);
    if (argc == 3 && strcmp(argv[1], "--write-scanner-fixture") == 0) {
        uint8_t image[IMAGE_SIZE];
        make_selector_image(image, false);
        FILE *file = fopen(argv[2], "wb");
        if (!file) return 2;
        bool written = fwrite(image, 1, sizeof(image), file) == sizeof(image);
        return fclose(file) == 0 && written ? 0 : 2;
    }
    test_headers_policy_and_commands(); test_object_and_file_ranges(); test_cross_command_consistency(); test_sections_and_addresses();
    test_patterns_and_allocations(); test_selector_resolution(); test_chained_multi_and_formats();
    puts("macho parser/scanner tests passed"); return 0;
}
