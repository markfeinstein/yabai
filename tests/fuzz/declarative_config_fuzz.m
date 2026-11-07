unsigned char __src_osax_payload[1];
unsigned int __src_osax_payload_len;
unsigned char __src_osax_loader[1];
unsigned int __src_osax_loader_len;

#include "../../src/manifest.m"

static void declarative_config_fuzz_parse(const uint8_t *data, size_t size)
{
    struct declarative_config_model *model = NULL;
    struct declarative_error error = {0};
    (void) declarative_config_parse_bytes(data, size, "<fuzz-input>", &model, &error);
    declarative_config_destroy(model);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size > DECLARATIVE_CONFIG_MAX_FILE_SIZE) return 0;

    declarative_config_fuzz_parse(data, size);
    static const uint8_t valid_seed[] = "{\"version\":1,\"config\":{}}";
    declarative_config_fuzz_parse(valid_seed, sizeof(valid_seed) - 1);
    return 0;
}
