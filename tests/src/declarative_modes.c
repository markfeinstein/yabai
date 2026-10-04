static void append_mode_policy(NSMutableString *json, size_t index)
{
    [json appendFormat:@"{\"match\":{\"index\":%zu},\"settings\":{\"window_gap\":%zu}}", index + 1, index];
}

TEST_FUNC(declarative_modes_v1_compatibility_and_v2_schema,
{
    const char *v1[] = {"{\"version\":1}", "{\"version\":1,\"spaces\":[]}", "{\"version\":1,\"config\":{},\"rules\":[]}"};
    for (size_t i = 0; i < 3; ++i) {
        struct declarative_config_model *model = NULL; struct declarative_error error = {0};
        TEST_CHECK(declarative_config_parse_bytes((const uint8_t *)v1[i], strlen(v1[i]), "v1", &model, &error), true);
        if (model) TEST_CHECK(model->version, 1);
        declarative_config_destroy(model);
    }
    struct path_case { const char *json, *path; } invalid[] = {
        {"{\"version\":1,\"modes\":[]}", "$.modes"},
        {"{\"version\":1,\"active_mode\":\"x\"}", "$.active_mode"},
        {"{\"version\":1,\"spaces\":[{\"match\":{\"index\":1},\"scaffold\":[]}]}", "$.spaces[0].scaffold"},
        {"{\"version\":2,\"modes\":{}}", "$.modes"},
        {"{\"version\":2,\"modes\":[{\"name\":1,\"spaces\":[]}]}", "$.modes[0].name"},
        {"{\"version\":2,\"modes\":[{\"name\":\"\",\"spaces\":[]}]}", "$.modes[0].name"},
        {"{\"version\":2,\"modes\":[{\"name\":\"x\",\"spaces\":{}}]}", "$.modes[0].spaces"},
        {"{\"version\":2,\"spaces\":[{\"match\":{\"index\":1}}]}", "$.spaces[0].settings"},
        {"{\"version\":2,\"modes\":[{\"name\":\"x\",\"spaces\":[{\"match\":{\"index\":1}}]}]}", "$.modes[0].spaces[0].settings"},
        {"{\"version\":2,\"modes\":[],\"active_mode\":1}", "$.active_mode"},
        {"{\"version\":2,\"modes\":[{\"name\":\"x\",\"spaces\":[]}],\"active_mode\":\"y\"}", "$.active_mode"}
    };
    for (size_t i = 0; i < sizeof(invalid)/sizeof(*invalid); ++i) {
        struct declarative_config_model *model = NULL; struct declarative_error error = {0};
        TEST_CHECK(declarative_config_parse_bytes((const uint8_t *)invalid[i].json, strlen(invalid[i].json), "invalid", &model, &error), false);
        TEST_CHECK(error.stage, DECLARATIVE_ERROR_SCHEMA);
        TEST_CHECK(strcmp(error.json_path, invalid[i].path), 0);
        declarative_config_destroy(model);
    }
    char owned[] = "{\"version\":2,\"modes\":[{\"name\":\"work\",\"spaces\":[{\"match\":{\"uuid\":\"mode-uuid\"},\"scaffold\":[{\"pattern\":\"mode-pattern\"}]}]}],\"active_mode\":\"work\"}";
    struct declarative_config_model *model = NULL; struct declarative_error error = {0};
    TEST_CHECK(declarative_config_parse_bytes((const uint8_t *)owned, strlen(owned), "owned", &model, &error), true);
    memset(owned, 'x', strlen(owned));
    if (model) {
        TEST_CHECK(strcmp(model->modes[0].name, "work"), 0);
        TEST_CHECK(strcmp(model->active_mode, "work"), 0);
        TEST_CHECK(strcmp(model->modes[0].spaces[0].uuid, "mode-uuid"), 0);
        TEST_CHECK(strcmp(model->modes[0].spaces[0].scaffold[0].pattern, "mode-pattern"), 0);
    }
    declarative_config_destroy(model);
})

TEST_FUNC(declarative_modes_schema_errors_and_limits,
{
    for (size_t count = DECLARATIVE_CONFIG_MAX_MODES; count <= DECLARATIVE_CONFIG_MAX_MODES + 1; ++count) {
        NSMutableString *json = [NSMutableString stringWithString:@"{\"version\":2,\"modes\":["];
        for (size_t i = 0; i < count; ++i) { if (i) [json appendString:@","]; [json appendFormat:@"{\"name\":\"m%zu\",\"spaces\":[]}", i]; }
        [json appendString:@"]}"]; NSData *data = [json dataUsingEncoding:NSUTF8StringEncoding];
        struct declarative_config_model *model = NULL; struct declarative_error error = {0};
        TEST_CHECK(declarative_config_parse_bytes(data.bytes, data.length, "mode-limit", &model, &error), count == DECLARATIVE_CONFIG_MAX_MODES);
        if (count > DECLARATIVE_CONFIG_MAX_MODES) TEST_CHECK(strcmp(error.json_path, "$.modes"), 0);
        declarative_config_destroy(model);
    }
    struct aggregate_case { size_t base, first, second; bool accepted; const char *path; } cases[] = {
        {1, DECLARATIVE_CONFIG_MAX_SPACES - 1, 0, true, NULL},
        {0, DECLARATIVE_CONFIG_MAX_SPACES + 1, 0, false, "$.modes[0].spaces"},
        {0, DECLARATIVE_CONFIG_MAX_SPACES, 1, false, "$.modes[1].spaces"}
    };
    for (size_t c = 0; c < sizeof(cases)/sizeof(*cases); ++c) {
        NSMutableString *json = [NSMutableString stringWithString:@"{\"version\":2"];
        if (cases[c].base) { [json appendString:@",\"spaces\":["]; for (size_t i = 0; i < cases[c].base; ++i) { if (i) [json appendString:@","]; append_mode_policy(json, i); } [json appendString:@"]"]; }
        [json appendString:@",\"modes\":[{\"name\":\"a\",\"spaces\":["];
        for (size_t i = 0; i < cases[c].first; ++i) { if (i) [json appendString:@","]; append_mode_policy(json, i); }
        [json appendString:@"]}"];
        if (cases[c].second) { [json appendString:@",{\"name\":\"b\",\"spaces\":["]; for (size_t i = 0; i < cases[c].second; ++i) { if (i) [json appendString:@","]; append_mode_policy(json, i); } [json appendString:@"]}"]; }
        [json appendString:@"]}"]; NSData *data = [json dataUsingEncoding:NSUTF8StringEncoding];
        struct declarative_config_model *model = NULL; struct declarative_error error = {0};
        TEST_CHECK(declarative_config_parse_bytes(data.bytes, data.length, "policy-limit", &model, &error), cases[c].accepted);
        if (!cases[c].accepted) { TEST_CHECK(error.stage, DECLARATIVE_ERROR_SCHEMA); TEST_CHECK(strcmp(error.json_path, cases[c].path), 0); }
        if (model) TEST_CHECK(model->space_count + model->modes[0].space_count, DECLARATIVE_CONFIG_MAX_SPACES);
        declarative_config_destroy(model);
    }
})

static size_t modes_alloc_call, modes_alloc_fail, modes_alloc_live;
static void *modes_test_calloc(size_t n,size_t s){++modes_alloc_call;if(modes_alloc_call==modes_alloc_fail)return NULL;void*p=calloc(n,s);if(p)++modes_alloc_live;return p;}
static void modes_test_free(void*p){if(p){--modes_alloc_live;free(p);}}

TEST_FUNC(declarative_modes_composition_is_pure_stable_and_transactional,
{
    const char json[] = "{\"version\":2,\"spaces\":[{\"match\":{\"uuid\":\"u\"},\"settings\":{\"window_gap\":1},\"scaffold\":[{\"pattern\":\"base\"}]},{\"match\":{\"index\":2},\"settings\":{\"window_gap\":2}}],\"modes\":[{\"name\":\"home\",\"spaces\":[{\"match\":{\"uuid\":\"u\"},\"settings\":{\"window_gap\":5}}]},{\"name\":\"work\",\"spaces\":[{\"match\":{\"uuid\":\"u\"},\"settings\":{\"window_gap\":9},\"scaffold\":[]}]}],\"active_mode\":\"home\"}";
    struct declarative_config_model *model = NULL; struct declarative_error parse_error = {0};
    TEST_CHECK(declarative_config_parse_bytes((const uint8_t *)json, strlen(json), "compose", &model, &parse_error), true);
    struct declarative_space_composition composition = {0}; struct declarative_modes_error error = {0};
    TEST_CHECK(declarative_modes_compose(model, "work", &composition, &error), true);
    TEST_CHECK(composition.space_count, 3UL);
    size_t expected_bytes = 3 * sizeof(*composition.spaces) + 2 + 5 + 2;
    TEST_CHECK(composition.allocated_bytes, expected_bytes);
    if (composition.space_count == 3) {
        TEST_CHECK(composition.spaces[0].settings[DECLARATIVE_CONFIG_WINDOW_GAP].integer, 1);
        TEST_CHECK(composition.spaces[1].scaffold_present, false);
        TEST_CHECK(composition.spaces[2].settings[DECLARATIVE_CONFIG_WINDOW_GAP].integer, 9);
        TEST_CHECK(composition.spaces[2].scaffold_present, true);
        TEST_CHECK(composition.spaces[2].scaffold_count, 0UL);
        TEST_CHECK(composition.spaces[0].uuid != model->spaces[0].uuid, true);
        TEST_CHECK(composition.spaces[0].scaffold[0].pattern != model->spaces[0].scaffold[0].pattern, true);
        composition.spaces[0].uuid[0] = 'z'; composition.spaces[0].scaffold[0].pattern[0] = 'B';
        TEST_CHECK(strcmp(model->spaces[0].uuid, "u"), 0); TEST_CHECK(strcmp(model->spaces[0].scaffold[0].pattern, "base"), 0);
    }
    declarative_modes_composition_destroy(&composition);
    TEST_CHECK(composition.spaces, NULL);
    TEST_CHECK(composition.space_count, 0UL);
    TEST_CHECK(composition.allocated_bytes, 0UL);
    TEST_CHECK(declarative_modes_compose(model, NULL, &composition, &error), true);
    TEST_CHECK(composition.space_count, 3UL);
    if (composition.space_count == 3) TEST_CHECK(composition.spaces[2].settings[DECLARATIVE_CONFIG_WINDOW_GAP].integer, 5);
    declarative_modes_composition_destroy(&composition);
    TEST_CHECK(declarative_modes_compose(model, "missing", &composition, &error), false);
    TEST_CHECK(error.code, DECLARATIVE_MODES_ERROR_EXPLICIT_MODE_NOT_FOUND);
    TEST_CHECK(error.detail.stage, DECLARATIVE_ERROR_RUNTIME);
    TEST_CHECK(strcmp(error.detail.json_path, "$.selected_mode"), 0);
    struct declarative_config_model invalid_active = {.active_mode="missing", .modes=model->modes, .mode_count=model->mode_count};
    TEST_CHECK(declarative_modes_compose(&invalid_active, NULL, &composition, &error), false);
    TEST_CHECK(error.code, DECLARATIVE_MODES_ERROR_ACTIVE_MODE_NOT_FOUND);
    struct declarative_config_model no_active = {.spaces=model->spaces, .space_count=model->space_count, .modes=model->modes, .mode_count=model->mode_count};
    TEST_CHECK(declarative_modes_compose(&no_active, NULL, &composition, &error), true);
    TEST_CHECK(composition.space_count, model->space_count);
    declarative_modes_composition_destroy(&composition);
    TEST_CHECK(declarative_modes_compose_selection(model, NULL, false, &composition, &error), true);
    TEST_CHECK(composition.space_count, model->space_count);
    if (composition.space_count) TEST_CHECK(composition.spaces[0].scaffold_present, true);
    declarative_modes_composition_destroy(&composition);

    struct declarative_allocator_ops model_ops={declarative_test_calloc,declarative_test_free};declarative_config_set_allocator_ops(&model_ops);declarative_alloc_call=declarative_alloc_fail=declarative_alloc_live=0;struct declarative_config_model*allocated=NULL;TEST_CHECK(declarative_config_parse_bytes((const uint8_t*)json,strlen(json),"mode-alloc",&allocated,&parse_error),true);size_t model_calls=declarative_alloc_call;declarative_config_destroy(allocated);TEST_CHECK(declarative_alloc_live,0UL);for(size_t fail=1;fail<=model_calls;++fail){declarative_alloc_call=declarative_alloc_live=0;declarative_alloc_fail=fail;allocated=(void*)1;TEST_CHECK(declarative_config_parse_bytes((const uint8_t*)json,strlen(json),"mode-alloc",&allocated,&parse_error),false);TEST_CHECK(allocated==NULL,true);TEST_CHECK(parse_error.stage,DECLARATIVE_ERROR_ALLOCATION);TEST_CHECK(declarative_alloc_live,0UL);}declarative_config_set_allocator_ops(NULL);
    struct declarative_allocator_ops ops={modes_test_calloc,modes_test_free};declarative_modes_set_allocator_ops(&ops);modes_alloc_call=modes_alloc_fail=modes_alloc_live=0;TEST_CHECK(declarative_modes_compose(model,NULL,&composition,&error),true);size_t calls=modes_alloc_call;declarative_modes_composition_destroy(&composition);TEST_CHECK(modes_alloc_live,0UL);for(size_t fail=1;fail<=calls;++fail){modes_alloc_call=modes_alloc_live=0;modes_alloc_fail=fail;TEST_CHECK(declarative_modes_compose(model,NULL,&composition,&error),false);TEST_CHECK(error.code,DECLARATIVE_MODES_ERROR_RESOURCE);TEST_CHECK(modes_alloc_live,0UL);}declarative_modes_set_allocator_ops(NULL);
    declarative_config_destroy(model);
})
