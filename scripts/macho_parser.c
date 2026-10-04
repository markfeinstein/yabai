#include "macho_parser.h"

#include <ctype.h>
#include <limits.h>
#include <mach-o/fixup-chains.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#include <mach-o/reloc.h>
#include <stdlib.h>
#include <string.h>

struct macho_view {
    const uint8_t *data;
    size_t size;
    struct mach_header_64 header;
    size_t commands_offset;
    size_t commands_size;
};

#ifdef MACHO_PARSER_TESTING
static size_t allocation_fail_after = SIZE_MAX;
static size_t allocation_count;
static size_t outstanding_allocations;

static void *parser_calloc(size_t count, size_t size)
{
    if (allocation_count++ >= allocation_fail_after) return NULL;
    void *allocation = calloc(count, size);
    if (allocation) ++outstanding_allocations;
    return allocation;
}

static void parser_free(void *allocation)
{
    if (allocation) { --outstanding_allocations; free(allocation); }
}

void macho_test_set_allocation_failure(size_t successful_allocations)
{
    allocation_fail_after = successful_allocations;
    allocation_count = 0;
}

size_t macho_test_outstanding_allocations(void) { return outstanding_allocations; }
#else
#define parser_calloc calloc
#define parser_free free
#endif

static bool add_u64(uint64_t left, uint64_t right, uint64_t *result)
{
    if (right > UINT64_MAX - left) return false;
    *result = left + right;
    return true;
}

static bool file_range(const struct macho_view *view, uint64_t offset, uint64_t size)
{
    return offset <= view->size && size <= view->size - (size_t) offset;
}

static bool file_count_range(const struct macho_view *view, uint64_t offset,
                             uint64_t count, uint64_t element_size)
{
    return element_size == 0 || (count <= UINT64_MAX / element_size &&
           file_range(view, offset, count * element_size));
}

static bool fixed_name_equals(const char field[16], const char *name)
{
    size_t length = strlen(name);
    return length <= 16 && memcmp(field, name, length) == 0 &&
           (length == 16 || field[length] == '\0');
}

static bool init_view(const uint8_t *data, size_t size, struct macho_view *view)
{
    if (!data || !view || size < sizeof(view->header)) return false;
    memcpy(&view->header, data, sizeof(view->header));
    if (view->header.magic != MH_MAGIC_64 || view->header.cputype != CPU_TYPE_ARM64) return false;
    if ((size_t) view->header.sizeofcmds > size - sizeof(view->header)) return false;
    view->data = data;
    view->size = size;
    view->commands_offset = sizeof(view->header);
    view->commands_size = view->header.sizeofcmds;
    return true;
}

static uint32_t minimum_command_size(uint32_t command)
{
    switch (command) {
        case LC_SEGMENT: return sizeof(struct segment_command);
        case LC_SEGMENT_64: return sizeof(struct segment_command_64);
        case LC_SYMTAB: return sizeof(struct symtab_command);
        case LC_SYMSEG: return sizeof(struct symseg_command);
        case LC_DYSYMTAB: return sizeof(struct dysymtab_command);
        case LC_LOAD_DYLIB: case LC_ID_DYLIB: case LC_LOAD_WEAK_DYLIB:
        case LC_REEXPORT_DYLIB: case LC_LAZY_LOAD_DYLIB: case LC_LOAD_UPWARD_DYLIB:
            return sizeof(struct dylib_command);
        case LC_LOAD_DYLINKER: case LC_ID_DYLINKER: case LC_DYLD_ENVIRONMENT:
            return sizeof(struct dylinker_command);
        case LC_LOADFVMLIB: case LC_IDFVMLIB: return sizeof(struct fvmlib_command);
        case LC_SUB_FRAMEWORK: return sizeof(struct sub_framework_command);
        case LC_SUB_UMBRELLA: return sizeof(struct sub_umbrella_command);
        case LC_SUB_CLIENT: return sizeof(struct sub_client_command);
        case LC_SUB_LIBRARY: return sizeof(struct sub_library_command);
        case LC_PREBOUND_DYLIB: return sizeof(struct prebound_dylib_command);
        case LC_FVMFILE: return sizeof(struct fvmfile_command);
        case LC_UUID: return sizeof(struct uuid_command);
        case LC_RPATH: return sizeof(struct rpath_command);
        case LC_CODE_SIGNATURE: case LC_SEGMENT_SPLIT_INFO: case LC_FUNCTION_STARTS:
        case LC_DATA_IN_CODE: case LC_DYLIB_CODE_SIGN_DRS: case LC_LINKER_OPTIMIZATION_HINT:
        case LC_DYLD_EXPORTS_TRIE: case LC_DYLD_CHAINED_FIXUPS:
            return sizeof(struct linkedit_data_command);
        case LC_DYLD_INFO: case LC_DYLD_INFO_ONLY: return sizeof(struct dyld_info_command);
        case LC_VERSION_MIN_MACOSX: case LC_VERSION_MIN_IPHONEOS:
        case LC_VERSION_MIN_TVOS: case LC_VERSION_MIN_WATCHOS:
            return sizeof(struct version_min_command);
        case LC_BUILD_VERSION: return sizeof(struct build_version_command);
        case LC_MAIN: return sizeof(struct entry_point_command);
        case LC_SOURCE_VERSION: return sizeof(struct source_version_command);
        case LC_ENCRYPTION_INFO: return sizeof(struct encryption_info_command);
        case LC_ENCRYPTION_INFO_64: return sizeof(struct encryption_info_command_64);
        case LC_ROUTINES: return sizeof(struct routines_command);
        case LC_ROUTINES_64: return sizeof(struct routines_command_64);
        case LC_TWOLEVEL_HINTS: return sizeof(struct twolevel_hints_command);
        case LC_PREBIND_CKSUM: return sizeof(struct prebind_cksum_command);
        case LC_THREAD: case LC_UNIXTHREAD: return sizeof(struct thread_command);
        case LC_LINKER_OPTION: return sizeof(struct linker_option_command);
        case LC_NOTE: return sizeof(struct note_command);
        case LC_FILESET_ENTRY: return sizeof(struct fileset_entry_command);
        default: return sizeof(struct load_command);
    }
}

static bool local_string(const uint8_t *bytes, uint32_t cmdsize, uint32_t offset,
                         uint32_t minimum)
{
    return offset >= minimum && offset < cmdsize &&
           memchr(bytes + offset, '\0', cmdsize - offset) != NULL;
}

static bool validate_thread(const uint8_t *bytes, uint32_t cmdsize)
{
    size_t cursor = sizeof(struct thread_command);
    if (cursor == cmdsize) return false;
    while (cursor < cmdsize) {
        uint32_t count;
        if (sizeof(uint32_t) * 2 > cmdsize - cursor) return false;
        memcpy(&count, bytes + cursor + sizeof(uint32_t), sizeof(count));
        cursor += sizeof(uint32_t) * 2;
        if (count > (cmdsize - cursor) / sizeof(uint32_t)) return false;
        cursor += (size_t) count * sizeof(uint32_t);
    }
    return cursor == cmdsize;
}

static bool validate_linker_options(const uint8_t *bytes, uint32_t cmdsize)
{
    struct linker_option_command option;
    size_t cursor = sizeof(option);
    memcpy(&option, bytes, sizeof(option));
    for (uint32_t index = 0; index < option.count; ++index) {
        const uint8_t *end;
        if (cursor >= cmdsize || !(end = memchr(bytes + cursor, '\0', cmdsize - cursor))) return false;
        cursor = (size_t) (end - bytes) + 1;
    }
    while (cursor < cmdsize) if (bytes[cursor++] != 0) return false;
    return true;
}

static bool validate_command_payload(const struct macho_view *view, size_t offset,
                                     const struct load_command *command)
{
    const uint8_t *bytes = view->data + offset;
    uint32_t string_offset = 0;
    uint32_t minimum = minimum_command_size(command->cmd);
    switch (command->cmd) {
        case LC_SEGMENT: return false;
        case LC_SYMTAB: {
            struct symtab_command item; memcpy(&item, bytes, sizeof(item));
            return file_count_range(view, item.symoff, item.nsyms, sizeof(struct nlist_64)) &&
                   file_range(view, item.stroff, item.strsize);
        }
        case LC_DYSYMTAB: {
            struct dysymtab_command item; memcpy(&item, bytes, sizeof(item));
            return file_count_range(view, item.tocoff, item.ntoc, sizeof(struct dylib_table_of_contents)) &&
                   file_count_range(view, item.modtaboff, item.nmodtab, sizeof(struct dylib_module_64)) &&
                   file_count_range(view, item.extrefsymoff, item.nextrefsyms, sizeof(struct dylib_reference)) &&
                   file_count_range(view, item.indirectsymoff, item.nindirectsyms, sizeof(uint32_t)) &&
                   file_count_range(view, item.extreloff, item.nextrel, sizeof(struct relocation_info)) &&
                   file_count_range(view, item.locreloff, item.nlocrel, sizeof(struct relocation_info));
        }
        case LC_DYLD_INFO: case LC_DYLD_INFO_ONLY: {
            struct dyld_info_command item; memcpy(&item, bytes, sizeof(item));
            return file_range(view, item.rebase_off, item.rebase_size) &&
                   file_range(view, item.bind_off, item.bind_size) &&
                   file_range(view, item.weak_bind_off, item.weak_bind_size) &&
                   file_range(view, item.lazy_bind_off, item.lazy_bind_size) &&
                   file_range(view, item.export_off, item.export_size);
        }
        case LC_SYMSEG: {
            struct symseg_command item; memcpy(&item, bytes, sizeof(item));
            return file_range(view, item.offset, item.size);
        }
        case LC_TWOLEVEL_HINTS: {
            struct twolevel_hints_command item; memcpy(&item, bytes, sizeof(item));
            return file_count_range(view, item.offset, item.nhints, sizeof(struct twolevel_hint));
        }
        case LC_ENCRYPTION_INFO: {
            struct encryption_info_command item; memcpy(&item, bytes, sizeof(item));
            return file_range(view, item.cryptoff, item.cryptsize);
        }
        case LC_ENCRYPTION_INFO_64: {
            struct encryption_info_command_64 item; memcpy(&item, bytes, sizeof(item));
            return file_range(view, item.cryptoff, item.cryptsize);
        }
        case LC_MAIN: return true;
        case LC_BUILD_VERSION: {
            struct build_version_command build;
            memcpy(&build, bytes, sizeof(build));
            return build.ntools == (command->cmdsize - sizeof(build)) / sizeof(struct build_tool_version);
        }
        case LC_LOAD_DYLIB: case LC_ID_DYLIB: case LC_LOAD_WEAK_DYLIB:
        case LC_REEXPORT_DYLIB: case LC_LAZY_LOAD_DYLIB: case LC_LOAD_UPWARD_DYLIB: {
            struct dylib_command dylib; memcpy(&dylib, bytes, sizeof(dylib)); string_offset = dylib.dylib.name.offset; break;
        }
        case LC_LOAD_DYLINKER: case LC_ID_DYLINKER: case LC_DYLD_ENVIRONMENT: {
            struct dylinker_command item; memcpy(&item, bytes, sizeof(item)); string_offset = item.name.offset; break;
        }
        case LC_RPATH: { struct rpath_command item; memcpy(&item, bytes, sizeof(item)); string_offset = item.path.offset; break; }
        case LC_LOADFVMLIB: case LC_IDFVMLIB: { struct fvmlib_command item; memcpy(&item, bytes, sizeof(item)); string_offset = item.fvmlib.name.offset; break; }
        case LC_SUB_FRAMEWORK: { struct sub_framework_command item; memcpy(&item, bytes, sizeof(item)); string_offset = item.umbrella.offset; break; }
        case LC_SUB_UMBRELLA: { struct sub_umbrella_command item; memcpy(&item, bytes, sizeof(item)); string_offset = item.sub_umbrella.offset; break; }
        case LC_SUB_CLIENT: { struct sub_client_command item; memcpy(&item, bytes, sizeof(item)); string_offset = item.client.offset; break; }
        case LC_SUB_LIBRARY: { struct sub_library_command item; memcpy(&item, bytes, sizeof(item)); string_offset = item.sub_library.offset; break; }
        case LC_FVMFILE: { struct fvmfile_command item; memcpy(&item, bytes, sizeof(item)); string_offset = item.name.offset; break; }
        case LC_FILESET_ENTRY: {
            struct fileset_entry_command item; memcpy(&item, bytes, sizeof(item));
            if (!file_range(view, item.fileoff, 0)) return false;
            string_offset = item.entry_id.offset; break;
        }
        case LC_PREBOUND_DYLIB: {
            struct prebound_dylib_command item; memcpy(&item, bytes, sizeof(item));
            uint64_t module_bytes = ((uint64_t) item.nmodules + 7) / 8;
            return local_string(bytes, command->cmdsize, item.name.offset, minimum) &&
                   item.linked_modules.offset >= minimum && item.linked_modules.offset <= command->cmdsize &&
                   module_bytes <= command->cmdsize - item.linked_modules.offset;
        }
        case LC_LINKER_OPTION: return validate_linker_options(bytes, command->cmdsize);
        case LC_THREAD: case LC_UNIXTHREAD: return validate_thread(bytes, command->cmdsize);
        case LC_NOTE: {
            struct note_command note; memcpy(&note, bytes, sizeof(note));
            return file_range(view, note.offset, note.size);
        }
        case LC_CODE_SIGNATURE: case LC_SEGMENT_SPLIT_INFO: case LC_FUNCTION_STARTS:
        case LC_DATA_IN_CODE: case LC_DYLIB_CODE_SIGN_DRS: case LC_LINKER_OPTIMIZATION_HINT:
        case LC_DYLD_EXPORTS_TRIE: case LC_DYLD_CHAINED_FIXUPS: {
            struct linkedit_data_command linkedit; memcpy(&linkedit, bytes, sizeof(linkedit));
            if (!file_range(view, linkedit.dataoff, linkedit.datasize)) return false;
            return command->cmd != LC_DATA_IN_CODE ||
                   linkedit.datasize % sizeof(struct data_in_code_entry) == 0;
        }
        default: return true;
    }
    return local_string(bytes, command->cmdsize, string_offset, minimum);
}

static bool command_at(const struct macho_view *view, size_t offset, struct load_command *command)
{
    size_t end = view->commands_offset + view->commands_size;
    if (offset > end || sizeof(*command) > end - offset || sizeof(*command) > view->size - offset) return false;
    memcpy(command, view->data + offset, sizeof(*command));
    if (command->cmdsize == 0 || (command->cmdsize & 7U) != 0 ||
        command->cmdsize < minimum_command_size(command->cmd) ||
        command->cmdsize > end - offset || command->cmdsize > view->size - offset) return false;
    return validate_command_payload(view, offset, command);
}

static bool zero_fill_section(uint32_t flags)
{
    uint32_t type = flags & SECTION_TYPE;
    return type == S_ZEROFILL || type == S_GB_ZEROFILL || type == S_THREAD_LOCAL_ZEROFILL;
}

static bool mapped_image(const struct macho_view *view)
{
    return view->header.filetype != MH_OBJECT;
}

static bool validate_section(const struct macho_view *view,
                             const struct segment_command_64 *segment,
                             const struct section_64 *section)
{
    if (mapped_image(view)) {
        uint64_t vm_delta;
        if (memcmp(section->segname, segment->segname, sizeof(section->segname)) != 0 ||
            section->addr < segment->vmaddr) return false;
        vm_delta = section->addr - segment->vmaddr;
        if (vm_delta > segment->vmsize || section->size > segment->vmsize - vm_delta) return false;
        if (!zero_fill_section(section->flags)) {
            uint64_t file_delta;
            if (section->offset < segment->fileoff) return false;
            file_delta = section->offset - segment->fileoff;
            if (file_delta != vm_delta || file_delta > segment->filesize ||
                section->size > segment->filesize - file_delta ||
                !file_range(view, section->offset, section->size)) return false;
        }
    } else {
        uint64_t vm_delta;
        if (section->addr < segment->vmaddr) return false;
        vm_delta = section->addr - segment->vmaddr;
        if (vm_delta > segment->vmsize || section->size > segment->vmsize - vm_delta) return false;
        if (!zero_fill_section(section->flags)) {
            uint64_t file_delta;
            if (section->offset < segment->fileoff) return false;
            file_delta = section->offset - segment->fileoff;
            if (file_delta > segment->filesize || section->size > segment->filesize - file_delta ||
                !file_range(view, section->offset, section->size)) return false;
        }
    }
    if (section->nreloc != 0) {
        uint64_t bytes = (uint64_t) section->nreloc * sizeof(struct relocation_info);
        if (!file_range(view, section->reloff, bytes)) return false;
    }
    return true;
}

static bool segment_at(const struct macho_view *view, size_t offset,
                       const struct load_command *command, struct segment_command_64 *segment)
{
    size_t section_bytes;
    uint64_t ignored;
    memcpy(segment, view->data + offset, sizeof(*segment));
    if (segment->nsects != 0 && sizeof(struct section_64) >
        (SIZE_MAX - sizeof(*segment)) / segment->nsects) return false;
    section_bytes = (size_t) segment->nsects * sizeof(struct section_64);
    if (section_bytes > command->cmdsize - sizeof(*segment) ||
        sizeof(*segment) + section_bytes > view->size - offset ||
        (mapped_image(view) && segment->filesize > segment->vmsize) ||
        !add_u64(segment->fileoff, segment->filesize, &ignored) ||
        !file_range(view, segment->fileoff, segment->filesize) ||
        !add_u64(segment->vmaddr, segment->vmsize, &ignored)) return false;
    for (uint32_t index = 0; index < segment->nsects; ++index) {
        struct section_64 section;
        memcpy(&section, view->data + offset + sizeof(*segment) + (size_t) index * sizeof(section), sizeof(section));
        if (!validate_section(view, segment, &section)) return false;
    }
    return true;
}

static bool validate_post_commands(const struct macho_view *view)
{
    size_t offset = view->commands_offset;
    uint32_t symtab_count = 0, dysymtab_count = 0, main_count = 0, text_count = 0;
    struct symtab_command symtab = {0};
    struct dysymtab_command dysymtab = {0};
    struct entry_point_command main_command = {0};
    struct segment_command_64 text_segment = {0};
    for (uint32_t index = 0; index < view->header.ncmds; ++index) {
        struct load_command command;
        if (!command_at(view, offset, &command)) return false;
        if (command.cmd == LC_SYMTAB) {
            ++symtab_count; memcpy(&symtab, view->data + offset, sizeof(symtab));
        } else if (command.cmd == LC_DYSYMTAB) {
            ++dysymtab_count; memcpy(&dysymtab, view->data + offset, sizeof(dysymtab));
        } else if (command.cmd == LC_MAIN) {
            ++main_count; memcpy(&main_command, view->data + offset, sizeof(main_command));
        } else if (command.cmd == LC_SEGMENT_64) {
            struct segment_command_64 segment;
            memcpy(&segment, view->data + offset, sizeof(segment));
            if (fixed_name_equals(segment.segname, "__TEXT")) {
                ++text_count; text_segment = segment;
            }
        }
        offset += command.cmdsize;
    }
    if (symtab_count > 1 || dysymtab_count > 1 || main_count > 1) return false;
    if (dysymtab_count != 0) {
        if (symtab_count != 1) return false;
        if (dysymtab.ilocalsym != 0 || dysymtab.nlocalsym > symtab.nsyms ||
            dysymtab.iextdefsym != dysymtab.nlocalsym ||
            dysymtab.nextdefsym > symtab.nsyms - dysymtab.iextdefsym ||
            dysymtab.iundefsym != dysymtab.iextdefsym + dysymtab.nextdefsym ||
            dysymtab.nundefsym != symtab.nsyms - dysymtab.iundefsym) return false;
    }
    if (main_count != 0) {
        uint64_t absolute;
        if (view->header.filetype != MH_EXECUTE || text_count != 1 ||
            main_command.entryoff >= text_segment.filesize ||
            !add_u64(text_segment.fileoff, main_command.entryoff, &absolute) ||
            !file_range(view, absolute, 1)) return false;
    }
    return true;
}

typedef bool (*command_visitor)(const struct macho_view *, size_t, const struct load_command *, void *, bool *);
static bool visit_commands(const struct macho_view *view, command_visitor visitor, void *context, bool *found)
{
    size_t offset = view->commands_offset, end = offset + view->commands_size;
    *found = false;
    for (uint32_t index = 0; index < view->header.ncmds; ++index) {
        struct load_command command;
        if (!command_at(view, offset, &command)) return false;
        if (command.cmd == LC_SEGMENT_64) {
            struct segment_command_64 segment;
            if (!segment_at(view, offset, &command, &segment)) return false;
        }
        if (visitor && !visitor(view, offset, &command, context, found)) return false;
        offset += command.cmdsize;
    }
    return offset == end && validate_post_commands(view);
}

bool macho_validate_arm64(const uint8_t *data, size_t size)
{
    struct macho_view view; bool found;
    return init_view(data, size, &view) && visit_commands(&view, NULL, NULL, &found);
}

bool macho_validate_scanner_image(const uint8_t *data, size_t size)
{
    struct macho_view view;
    if (!init_view(data, size, &view) || !macho_validate_arm64(data, size) || view.header.filetype != MH_EXECUTE) return false;
    cpu_subtype_t subtype = view.header.cpusubtype & ~CPU_SUBTYPE_MASK;
    return subtype == CPU_SUBTYPE_ARM64_ALL || subtype == CPU_SUBTYPE_ARM64E;
}

struct section_context { const char *segment; const char *section; uint64_t *offset; uint64_t *size; };
static bool section_visitor(const struct macho_view *view, size_t offset,
                            const struct load_command *command, void *opaque, bool *found)
{
    struct section_context *context = opaque;
    struct segment_command_64 segment;
    if (command->cmd != LC_SEGMENT_64) return true;
    if (!segment_at(view, offset, command, &segment) || !fixed_name_equals(segment.segname, context->segment)) return true;
    for (uint32_t index = 0; index < segment.nsects; ++index) {
        struct section_64 section;
        memcpy(&section, view->data + offset + sizeof(segment) + (size_t) index * sizeof(section), sizeof(section));
        if (fixed_name_equals(section.segname, context->segment) && fixed_name_equals(section.sectname, context->section)) {
            if (zero_fill_section(section.flags)) return false;
            *context->offset = section.offset; *context->size = section.size; *found = true;
        }
    }
    return true;
}

bool macho_find_section(const uint8_t *data, size_t size, const char *segment_name,
                        const char *section_name, uint64_t *offset, uint64_t *section_size)
{
    struct macho_view view; bool found;
    struct section_context context = { segment_name, section_name, offset, section_size };
    return segment_name && section_name && offset && section_size && init_view(data, size, &view) &&
           visit_commands(&view, section_visitor, &context, &found) && found;
}

struct address_context { uint64_t file_offset; uint64_t *address; };
static bool address_visitor(const struct macho_view *view, size_t offset,
                            const struct load_command *command, void *opaque, bool *found)
{
    struct address_context *context = opaque;
    struct segment_command_64 segment; uint64_t delta;
    if (command->cmd != LC_SEGMENT_64) return true;
    if (!segment_at(view, offset, command, &segment)) return false;
    if (context->file_offset < segment.fileoff) return true;
    delta = context->file_offset - segment.fileoff;
    if (delta >= segment.filesize || delta >= segment.vmsize || delta > UINT64_MAX - segment.vmaddr) return true;
    *context->address = segment.vmaddr + delta; *found = true; return true;
}

bool macho_file_offset_to_vm_address(const uint8_t *data, size_t size, uint64_t file_offset,
                                     uint64_t *vm_address)
{
    struct macho_view view; bool found;
    struct address_context context = { file_offset, vm_address };
    return vm_address && file_offset < size && init_view(data, size, &view) &&
           mapped_image(&view) && visit_commands(&view, address_visitor, &context, &found) && found;
}

struct fixup_context { struct linkedit_data_command command; bool present; };
static bool fixup_visitor(const struct macho_view *view, size_t offset,
                          const struct load_command *command, void *opaque, bool *found)
{
    struct fixup_context *context = opaque; (void) found;
    if (command->cmd == LC_DYLD_CHAINED_FIXUPS) {
        if (context->present) return false;
        memcpy(&context->command, view->data + offset, sizeof(context->command)); context->present = true;
    }
    return true;
}


static bool image_base(const struct macho_view *view, uint64_t *base)
{
    size_t offset = view->commands_offset;
    for (uint32_t index = 0; index < view->header.ncmds; ++index) {
        struct load_command command; if (!command_at(view, offset, &command)) return false;
        if (command.cmd == LC_SEGMENT_64) {
            struct segment_command_64 segment; memcpy(&segment, view->data + offset, sizeof(segment));
            if (segment.fileoff == 0 && segment.filesize != 0) { *base = segment.vmaddr; return true; }
        }
        offset += command.cmdsize;
    }
    return false;
}

static bool chain_format_stride(uint16_t format, uint64_t *stride)
{
    if (format == DYLD_CHAINED_PTR_64 || format == DYLD_CHAINED_PTR_64_OFFSET) {
        *stride = 4; return true;
    }
    if (format == DYLD_CHAINED_PTR_ARM64E || format == DYLD_CHAINED_PTR_ARM64E_USERLAND ||
        format == DYLD_CHAINED_PTR_ARM64E_USERLAND24) {
        *stride = 8; return true;
    }
    return false;
}

static uint64_t chain_next(uint64_t raw, uint16_t format, uint64_t stride)
{
    if (format == DYLD_CHAINED_PTR_64 || format == DYLD_CHAINED_PTR_64_OFFSET)
        return ((raw >> 51) & 0xfff) * stride;
    return ((raw >> 51) & 0x7ff) * stride;
}

static enum macho_pointer_result decode_chain_target(uint64_t raw, uint16_t format,
                                                      uint64_t base, uint64_t *target)
{
    uint64_t value, high8, decoded;
    if (format == DYLD_CHAINED_PTR_64 || format == DYLD_CHAINED_PTR_64_OFFSET) {
        if (raw >> 63) return MACHO_POINTER_UNSUPPORTED;
        if (((raw >> 44) & 0x7f) != 0) return MACHO_POINTER_MALFORMED;
        value = raw & ((1ULL << 36) - 1); high8 = (raw >> 36) & 0xff;
        if (format == DYLD_CHAINED_PTR_64_OFFSET) {
            if (!add_u64(base, value, &decoded)) return MACHO_POINTER_MALFORMED;
            decoded |= high8 << 56;
        } else decoded = value | (high8 << 56);
        *target = decoded; return MACHO_POINTER_RESOLVED;
    }
    if (format == DYLD_CHAINED_PTR_ARM64E || format == DYLD_CHAINED_PTR_ARM64E_USERLAND ||
        format == DYLD_CHAINED_PTR_ARM64E_USERLAND24) {
        if ((raw >> 62) != 0) return MACHO_POINTER_UNSUPPORTED;
        value = raw & ((1ULL << 43) - 1); high8 = (raw >> 43) & 0xff;
        if (format != DYLD_CHAINED_PTR_ARM64E) {
            if (!add_u64(base, value, &decoded)) return MACHO_POINTER_MALFORMED;
            decoded |= high8 << 56;
        } else decoded = value | (high8 << 56);
        *target = decoded; return MACHO_POINTER_RESOLVED;
    }
    return MACHO_POINTER_UNSUPPORTED;
}

enum macho_pointer_result macho_resolve_pointer(const uint8_t *data, size_t size,
                                                uint64_t file_offset, uint64_t *target)
{
    struct macho_view view; struct fixup_context fixup = {0}; bool found; uint64_t base;
    if (!target || file_offset > size || sizeof(uint64_t) > size - (size_t) file_offset ||
        !init_view(data, size, &view) || !mapped_image(&view) ||
        !visit_commands(&view, fixup_visitor, &fixup, &found)) return MACHO_POINTER_MALFORMED;
    if (!fixup.present) {
        memcpy(target, data + file_offset, sizeof(*target));
        return MACHO_POINTER_RESOLVED;
    }
    if (!image_base(&view, &base) || fixup.command.datasize < sizeof(struct dyld_chained_fixups_header))
        return MACHO_POINTER_MALFORMED;
    const uint8_t *payload = data + fixup.command.dataoff;
    struct dyld_chained_fixups_header header; memcpy(&header, payload, sizeof(header));
    if (header.fixups_version != 0 || header.starts_offset < sizeof(header) ||
        header.starts_offset > fixup.command.datasize || header.imports_offset > fixup.command.datasize ||
        header.symbols_offset > fixup.command.datasize ||
        sizeof(struct dyld_chained_starts_in_image) > fixup.command.datasize - header.starts_offset)
        return MACHO_POINTER_MALFORMED;
    const uint8_t *starts = payload + header.starts_offset;
    uint32_t seg_count; memcpy(&seg_count, starts, sizeof(seg_count));
    if (seg_count > (fixup.command.datasize - header.starts_offset - sizeof(uint32_t)) / sizeof(uint32_t))
        return MACHO_POINTER_MALFORMED;

    uint32_t segment_count = 0, target_index = UINT32_MAX;
    struct segment_command_64 target_segment = {0};
    size_t command_offset = view.commands_offset;
    for (uint32_t index = 0; index < view.header.ncmds; ++index) {
        struct load_command command;
        if (!command_at(&view, command_offset, &command)) return MACHO_POINTER_MALFORMED;
        if (command.cmd == LC_SEGMENT_64) {
            struct segment_command_64 segment;
            memcpy(&segment, view.data + command_offset, sizeof(segment));
            if (file_offset >= segment.fileoff && file_offset - segment.fileoff < segment.filesize) {
                if (target_index != UINT32_MAX) return MACHO_POINTER_MALFORMED;
                target_index = segment_count; target_segment = segment;
            }
            ++segment_count;
        }
        command_offset += command.cmdsize;
    }
    if (seg_count != segment_count) return MACHO_POINTER_MALFORMED;
    if (target_index == UINT32_MAX) return MACHO_POINTER_NOT_CHAINED;

    uint64_t starts_table_size = sizeof(uint32_t) + (uint64_t) seg_count * sizeof(uint32_t);
    uint32_t info_offset;
    memcpy(&info_offset, starts + sizeof(uint32_t) + target_index * sizeof(uint32_t), sizeof(info_offset));
    if (info_offset == 0) return MACHO_POINTER_NOT_CHAINED;
    if (info_offset < starts_table_size || info_offset > fixup.command.datasize - header.starts_offset)
        return MACHO_POINTER_MALFORMED;
    uint64_t starts_file = (uint64_t) fixup.command.dataoff + header.starts_offset;
    if (starts_file + info_offset > size ||
        sizeof(struct dyld_chained_starts_in_segment) > size - (size_t) (starts_file + info_offset))
        return MACHO_POINTER_MALFORMED;
    struct dyld_chained_starts_in_segment info;
    memcpy(&info, data + starts_file + info_offset, sizeof(info));
    size_t starts_header = offsetof(struct dyld_chained_starts_in_segment, page_start);
    if (info.size < starts_header || info.size > fixup.command.datasize - header.starts_offset - info_offset ||
        info.size > size - (size_t) (starts_file + info_offset) ||
        (info.size - starts_header) % sizeof(uint16_t) != 0 ||
        info.page_count > (info.size - starts_header) / sizeof(uint16_t) ||
        (info.page_size != 0x1000 && info.page_size != 0x4000) ||
        target_segment.vmaddr < base || info.segment_offset != target_segment.vmaddr - base)
        return MACHO_POINTER_MALFORMED;
    uint64_t stride;
    if (!chain_format_stride(info.pointer_format, &stride)) return MACHO_POINTER_UNSUPPORTED;
    uint64_t segment_delta = file_offset - target_segment.fileoff;
    uint64_t page_index = segment_delta / info.page_size;
    if (page_index >= info.page_count) return MACHO_POINTER_NOT_CHAINED;
    uint64_t page_base = target_segment.fileoff + page_index * info.page_size;
    uint64_t segment_end = target_segment.fileoff + target_segment.filesize;
    uint64_t page_end = info.page_size > segment_end - page_base ? segment_end : page_base + info.page_size;
    uint16_t page_entry;
    uint64_t info_file = starts_file + info_offset;
    memcpy(&page_entry, data + info_file + starts_header + page_index * sizeof(uint16_t), sizeof(page_entry));
    if (page_entry == DYLD_CHAINED_PTR_START_NONE) return MACHO_POINTER_NOT_CHAINED;

    size_t total_entries = (info.size - starts_header) / sizeof(uint16_t);
    uint16_t chain_starts[4096]; size_t chain_count = 0;
    if (page_entry & DYLD_CHAINED_PTR_START_MULTI) {
        size_t overflow = page_entry & ~DYLD_CHAINED_PTR_START_MULTI;
        if (overflow < info.page_count || overflow >= total_entries) return MACHO_POINTER_MALFORMED;
        bool last = false;
        while (overflow < total_entries && chain_count < sizeof(chain_starts)/sizeof(chain_starts[0])) {
            uint16_t entry;
            memcpy(&entry, data + info_file + starts_header + overflow * sizeof(uint16_t), sizeof(entry));
            uint16_t start = entry & ~DYLD_CHAINED_PTR_START_LAST;
            for (size_t prior = 0; prior < chain_count; ++prior)
                if (chain_starts[prior] == start) return MACHO_POINTER_MALFORMED;
            chain_starts[chain_count++] = start;
            ++overflow;
            if (entry & DYLD_CHAINED_PTR_START_LAST) { last = true; break; }
        }
        if (!last) return MACHO_POINTER_MALFORMED;
    } else chain_starts[chain_count++] = page_entry;

    bool visited[4096] = {0};
    for (size_t start_index = 0; start_index < chain_count; ++start_index) {
        uint64_t start = chain_starts[start_index];
        if (start >= page_end - page_base || start % stride != 0) return MACHO_POINTER_MALFORMED;
        uint64_t chain = page_base + start;
        while (chain <= file_offset) {
            uint64_t relative = chain - page_base;
            size_t visited_index = (size_t) (relative / 4);
            if (visited_index >= sizeof(visited) || visited[visited_index] ||
                sizeof(uint64_t) > page_end - chain) return MACHO_POINTER_MALFORMED;
            visited[visited_index] = true;
            uint64_t raw; memcpy(&raw, data + chain, sizeof(raw));
            if (chain == file_offset) return decode_chain_target(raw, info.pointer_format, base, target);
            uint64_t next = chain_next(raw, info.pointer_format, stride);
            if (next == 0) break;
            if (next % stride != 0 || next > page_end - chain) return MACHO_POINTER_MALFORMED;
            chain += next;
        }
    }
    return MACHO_POINTER_NOT_CHAINED;
}

void macho_free_pattern(struct macho_byte_pattern *pattern)
{
    if (!pattern) return;
    parser_free(pattern->bytes); parser_free(pattern->wildcards); memset(pattern, 0, sizeof(*pattern));
}

bool macho_parse_pattern(const char *text, struct macho_byte_pattern *result)
{
    size_t count = 0; const char *cursor;
    if (!text || !result) return false;
    memset(result, 0, sizeof(*result));
    for (cursor = text; *cursor;) {
        while (isspace((unsigned char) *cursor)) ++cursor;
        if (!*cursor) break;
        const char *start = cursor; while (*cursor && !isspace((unsigned char) *cursor)) ++cursor;
        if ((size_t) (cursor - start) != 2 || count == MACHO_PATTERN_MAX_BYTES) return false;
        ++count;
    }
    if (count == 0) return false;
    result->bytes = parser_calloc(count, 1); result->wildcards = parser_calloc(count, sizeof(bool));
    if (!result->bytes || !result->wildcards) { macho_free_pattern(result); return false; }
    cursor = text;
    for (size_t index = 0; index < count; ++index) {
        char token[3], *end; unsigned long value;
        while (isspace((unsigned char) *cursor)) ++cursor;
        token[0] = cursor[0]; token[1] = cursor[1]; token[2] = 0; cursor += 2;
        if (token[0] == '?') result->wildcards[index] = true;
        else {
            if (!isxdigit((unsigned char) token[0]) || !isxdigit((unsigned char) token[1])) { macho_free_pattern(result); return false; }
            value = strtoul(token, &end, 16);
            if (end != token + 2 || value > UINT8_MAX) { macho_free_pattern(result); return false; }
            result->bytes[index] = (uint8_t) value;
        }
    }
    result->length = count; return true;
}

bool macho_pattern_matches(const uint8_t *data, size_t available, const struct macho_byte_pattern *pattern)
{
    if (!data || !pattern || pattern->length > available || !pattern->bytes || !pattern->wildcards) return false;
    for (size_t index = 0; index < pattern->length; ++index)
        if (!pattern->wildcards[index] && data[index] != pattern->bytes[index]) return false;
    return true;
}
