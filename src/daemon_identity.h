#ifndef DAEMON_IDENTITY_H
#define DAEMON_IDENTITY_H

#include <inttypes.h>
#include <mach-o/loader.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#define DAEMON_IDENTITY_MAX_COMMAND_BYTES (1024U * 1024U)
#define DAEMON_IDENTITY_MAX_COMMANDS 4096U

static inline const char *daemon_identity_architecture(cpu_type_t cpu_type)
{
    switch (cpu_type) {
    case CPU_TYPE_ARM64: return "arm64";
    case CPU_TYPE_X86_64: return "x86_64";
    default: return NULL;
    }
}

static inline bool daemon_identity_macho_uuid(const struct mach_header_64 *header,
                                               uint8_t uuid[16])
{
    if (!header || !uuid || header->magic != MH_MAGIC_64 || header->filetype != MH_EXECUTE ||
        header->ncmds == 0 || header->ncmds > DAEMON_IDENTITY_MAX_COMMANDS ||
        header->sizeofcmds > DAEMON_IDENTITY_MAX_COMMAND_BYTES) return false;

    const uint8_t *cursor = (const uint8_t *) (header + 1);
    size_t remaining = header->sizeofcmds;
    bool found = false;
    for (uint32_t i = 0; i < header->ncmds; ++i) {
        if (remaining < sizeof(struct load_command)) return false;
        const struct load_command *command = (const struct load_command *) cursor;
        if (command->cmdsize < sizeof(struct load_command) || command->cmdsize > remaining) return false;
        if (command->cmd == LC_UUID) {
            if (found || command->cmdsize != sizeof(struct uuid_command)) return false;
            memcpy(uuid, ((const struct uuid_command *) command)->uuid, 16);
            found = true;
        }
        cursor += command->cmdsize;
        remaining -= command->cmdsize;
    }
    return found && remaining == 0;
}

static inline bool daemon_identity_write_json(FILE *stream, pid_t pid,
                                               const struct mach_header_64 *header)
{
    uint8_t uuid[16];
    const char *architecture = header ? daemon_identity_architecture(header->cputype) : NULL;
    if (!stream || pid <= 0 || !architecture || !daemon_identity_macho_uuid(header, uuid)) return false;
    return fprintf(stream,
                   "{\"pid\":%d,\"architecture\":\"%s\","
                   "\"uuid\":\"%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X\"}\n",
                   pid, architecture,
                   uuid[0], uuid[1], uuid[2], uuid[3], uuid[4], uuid[5], uuid[6], uuid[7],
                   uuid[8], uuid[9], uuid[10], uuid[11], uuid[12], uuid[13], uuid[14], uuid[15]) > 0;
}

#endif
