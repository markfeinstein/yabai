TEST_FUNC(daemon_identity_macho_uuid_bounds_are_strict,
{
    struct {
        struct mach_header_64 header;
        struct uuid_command command;
    } image = {
        .header = { .magic = MH_MAGIC_64, .cputype = CPU_TYPE_ARM64, .filetype = MH_EXECUTE,
                    .ncmds = 1, .sizeofcmds = sizeof(struct uuid_command) },
        .command = { .cmd = LC_UUID, .cmdsize = sizeof(struct uuid_command),
                     .uuid = { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
                               0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff } }
    };
    uint8_t uuid[16] = {0};
    TEST_CHECK(daemon_identity_macho_uuid(&image.header, uuid), true);
    TEST_CHECK(memcmp(uuid, image.command.uuid, sizeof(uuid)), 0);

    struct mach_header_64 saved = image.header;
    image.header.magic = MH_CIGAM_64;
    TEST_CHECK(daemon_identity_macho_uuid(&image.header, uuid), false);
    image.header = saved; image.header.filetype = MH_DYLIB;
    TEST_CHECK(daemon_identity_macho_uuid(&image.header, uuid), false);
    image.header = saved; image.header.ncmds = DAEMON_IDENTITY_MAX_COMMANDS + 1;
    TEST_CHECK(daemon_identity_macho_uuid(&image.header, uuid), false);
    image.header = saved; image.header.sizeofcmds = DAEMON_IDENTITY_MAX_COMMAND_BYTES + 1;
    TEST_CHECK(daemon_identity_macho_uuid(&image.header, uuid), false);
    image.header = saved; image.command.cmdsize = sizeof(struct load_command) - 1;
    TEST_CHECK(daemon_identity_macho_uuid(&image.header, uuid), false);
    image.command.cmdsize = sizeof(struct uuid_command) + 1;
    TEST_CHECK(daemon_identity_macho_uuid(&image.header, uuid), false);
    image.command.cmdsize = sizeof(struct uuid_command); image.header.sizeofcmds--;
    TEST_CHECK(daemon_identity_macho_uuid(&image.header, uuid), false);
    image.header = saved; image.command.cmd = LC_SEGMENT_64;
    TEST_CHECK(daemon_identity_macho_uuid(&image.header, uuid), false);
});

TEST_FUNC(daemon_identity_json_contains_protocol_identity,
{
    struct {
        struct mach_header_64 header;
        struct uuid_command command;
    } image = {
        .header = { .magic = MH_MAGIC_64, .cputype = CPU_TYPE_X86_64, .filetype = MH_EXECUTE,
                    .ncmds = 1, .sizeofcmds = sizeof(struct uuid_command) },
        .command = { .cmd = LC_UUID, .cmdsize = sizeof(struct uuid_command),
                     .uuid = { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
                               0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff } }
    };
    char *output = NULL; size_t output_length = 0;
    FILE *stream = open_memstream(&output, &output_length);
    TEST_CHECK(stream != NULL, true);
    if (stream) {
        TEST_CHECK(daemon_identity_write_json(stream, 42, &image.header), true);
        fclose(stream);
        TEST_CHECK(strcmp(output, "{\"pid\":42,\"architecture\":\"x86_64\",\"uuid\":\"00112233-4455-6677-8899-AABBCCDDEEFF\"}\n"), 0);
    }
    free(output);
    image.header.cputype = CPU_TYPE_POWERPC;
    TEST_CHECK(daemon_identity_write_json(stdout, 42, &image.header), false);

    char query[] = "--daemon-identity\0";
    output = NULL; output_length = 0; stream = open_memstream(&output, &output_length);
    handle_domain_query(stream, message_test_token("query"), query);
    fclose(stream);
    TEST_CHECK(strstr(output, "\"pid\":") != NULL, true);
    TEST_CHECK(strstr(output, "\"architecture\":\"") != NULL, true);
    TEST_CHECK(strstr(output, "\"uuid\":\"") != NULL, true);
    free(output);

    char trailing[] = "--daemon-identity\0extra\0";
    output = NULL; output_length = 0; stream = open_memstream(&output, &output_length);
    handle_domain_query(stream, message_test_token("query"), trailing);
    fclose(stream);
    TEST_CHECK(output[0], FAILURE_MESSAGE[0]);
    free(output);
});
