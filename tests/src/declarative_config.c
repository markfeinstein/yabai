TEST_FUNC(declarative_inventory_is_canonical,
    TEST_CHECK(DECLARATIVE_CONFIG_COUNT, 42);
    TEST_CHECK(DECLARATIVE_RULE_FIELD_COUNT, 17);
    for (int i = 0; i < DECLARATIVE_CONFIG_COUNT; ++i) {
        const char *name = declarative_config_descriptors[i].name;
        TEST_CHECK(declarative_config_id_for_name(name, strlen(name)), i);
        TEST_CHECK(declarative_config_descriptors[i].validator_id >= 0, true);
        TEST_CHECK(declarative_config_descriptors[i].setter_id >= 0, true);
        for(int j=i+1;j<DECLARATIVE_CONFIG_COUNT;++j) TEST_CHECK(strcmp(name,declarative_config_descriptors[j].name)!=0,true);
    }
    for(int i=0;i<DECLARATIVE_RULE_FIELD_COUNT;++i){const struct declarative_rule_field_descriptor*d=&declarative_rule_field_descriptors[i];TEST_CHECK(declarative_rule_field_for_json_name(d->json_name,strlen(d->json_name)),i);if(d->cli_name)TEST_CHECK(declarative_rule_field_for_cli_name(d->cli_name,strlen(d->cli_name)),i);for(int j=i+1;j<DECLARATIVE_RULE_FIELD_COUNT;++j){TEST_CHECK(strcmp(d->json_name,declarative_rule_field_descriptors[j].json_name)!=0,true);TEST_CHECK(strcmp(d->query_name,declarative_rule_field_descriptors[j].query_name)!=0,true);if(d->cli_name&&declarative_rule_field_descriptors[j].cli_name)TEST_CHECK(strcmp(d->cli_name,declarative_rule_field_descriptors[j].cli_name)!=0,true);}}
    TEST_CHECK(declarative_rule_field_for_json_name("one_shot", 8), DECLARATIVE_RULE_FIELD_ONE_SHOT);
    TEST_CHECK(declarative_rule_field_for_cli_name("--one-shot", 10), DECLARATIVE_RULE_FIELD_ONE_SHOT);
    TEST_CHECK(strcmp(declarative_rule_field_descriptors[DECLARATIVE_RULE_FIELD_SUB_LAYER].query_name, "sub-layer"), 0);
    TEST_CHECK(strcmp(declarative_rule_field_descriptors[DECLARATIVE_RULE_FIELD_FULLSCREEN].json_name, "native_fullscreen"), 0);
)

TEST_FUNC(declarative_jsonc_normalization_preserves_source,
    uint8_t source[] = "\xef\xbb\xbf{\r\n// hello\r\n\"version\":1,/*x\n y*/\"config\":{}}";
    uint8_t original[sizeof(source)]; memcpy(original, source, sizeof(source));
    struct declarative_error error = {0};
    TEST_CHECK(declarative_jsonc_normalize(source, sizeof(source)-1, &error), true);
    TEST_CHECK(sizeof(source), sizeof(original));
    TEST_CHECK(source[0], ' '); TEST_CHECK(source[1], ' '); TEST_CHECK(source[2], ' ');
    for (size_t i = 0; i < sizeof(source)-1; ++i) if (original[i] == '\r' || original[i] == '\n') TEST_CHECK(source[i], original[i]);
    TEST_CHECK(strstr((char *)source, "hello") == NULL, true);
)

TEST_FUNC(declarative_jsonc_rejects_encoding_duplicates_and_surrogates,
    struct declarative_config_model *model = NULL; struct declarative_error error = {0};
    const char duplicate[] = "{\"version\":1,\"\\u0076ersion\":1}";
    TEST_CHECK(declarative_config_parse_bytes((uint8_t *)duplicate, strlen(duplicate), "secret", &model, &error), false);
    TEST_CHECK(error.stage, DECLARATIVE_ERROR_JSONC);
    TEST_CHECK(strstr(error.json_path, "duplicate") != NULL, true);
    TEST_CHECK(strstr(error.message, "version") == NULL, true);
    const char surrogate[] = "{\"version\":1,\"config\":{\"layout\":\"\\uD800\"}}";
    TEST_CHECK(declarative_config_parse_bytes((uint8_t *)surrogate, strlen(surrogate), "x", &model, &error), false);
    TEST_CHECK(error.stage, DECLARATIVE_ERROR_JSONC);
    const char nul[] = "{\"version\":1,\"config\":{\"layout\":\"\\u0000\"}}";
    TEST_CHECK(declarative_config_parse_bytes((uint8_t *)nul, strlen(nul), "x", &model, &error), false);
    uint8_t bad[] = {'{','/', '/', 0xff, '\n','}'};
    TEST_CHECK(declarative_config_parse_bytes(bad, sizeof(bad), "x", &model, &error), false);
    TEST_CHECK(error.stage, DECLARATIVE_ERROR_ENCODING);
)

TEST_FUNC(declarative_json_numeric_domain_table_matches_documented_contract,
    struct numeric_case { const char *key; const char *value; bool valid; } config_cases[] = {
        { "menubar_opacity", "0", true }, { "menubar_opacity", "1", true },
        { "menubar_opacity", "1.01", false }, { "menubar_opacity", "1e999", false },
        { "active_window_opacity", "0", false }, { "active_window_opacity", "1", true },
        { "active_window_opacity", "1.01", false },
        { "normal_window_opacity", "0", false }, { "normal_window_opacity", "1", true },
        { "normal_window_opacity", "1e999", false },
        { "split_ratio", "0.1", true }, { "split_ratio", "0.9", true },
        { "split_ratio", "0", false }, { "split_ratio", "1", false },
        { "single_window_max_width", "0", true }, { "single_window_max_width", "1600", true },
        { "single_window_max_width", "-1", false }, { "single_window_max_width", "1.5", false },
        { "single_window_width_ratio", "0.1", true }, { "single_window_width_ratio", "1", true },
        { "single_window_width_ratio", "0", false }, { "single_window_width_ratio", "1.01", false },
        { "window_animation_duration", "0", true }, { "window_animation_duration", "2", true },
        { "window_animation_duration", "-0.1", false }, { "window_animation_duration", "1e999", false },
        { "window_opacity_duration", "0", true }, { "window_opacity_duration", "2", true },
        { "window_opacity_duration", "2147483.5", true },
        { "window_opacity_duration", "2147483.75", false },
        { "window_opacity_duration", "1e999", false }
    };
    for (size_t i = 0; i < array_count(config_cases); ++i) {
        char json[256];
        snprintf(json, sizeof(json), "{\"version\":1,\"config\":{\"%s\":%s}}",
                 config_cases[i].key, config_cases[i].value);
        struct declarative_config_model *model = NULL;
        struct declarative_error error = {0};
        bool parsed = declarative_config_parse_bytes((uint8_t *) json, strlen(json), "numeric.jsonc", &model, &error);
        TEST_CHECK(parsed, config_cases[i].valid);
        declarative_config_destroy(model);
    }

    struct numeric_case rule_cases[] = {
        { "opacity", "0", true }, { "opacity", "1", true },
        { "opacity", "1.01", false }, { "opacity", "-0.01", false },
        { "opacity", "1e999", false }
    };
    for (size_t i = 0; i < array_count(rule_cases); ++i) {
        char json[320];
        snprintf(json, sizeof(json), "{\"version\":1,\"rules\":[{\"match\":{\"app\":{\"pattern\":\"x\"}},\"effects\":{\"opacity\":%s}}]}",
                 rule_cases[i].value);
        struct declarative_config_model *model = NULL;
        struct declarative_error error = {0};
        bool parsed = declarative_config_parse_bytes((uint8_t *) json, strlen(json), "rule-numeric.jsonc", &model, &error);
        TEST_CHECK(parsed, rule_cases[i].valid);
        declarative_config_destroy(model);
    }
)

TEST_FUNC(declarative_numeric_conversions_are_checked,
    int integer = 0; uint32_t u = 0; float f = 0;
    TEST_CHECK(declarative_number_to_int(@1.0, INT_MIN, INT_MAX, &integer), true);
    TEST_CHECK(integer, 1);
    TEST_CHECK(declarative_number_to_int(@YES, INT_MIN, INT_MAX, &integer), false);
    TEST_CHECK(declarative_number_to_int(@1.5, INT_MIN, INT_MAX, &integer), false);
    TEST_CHECK(declarative_number_to_uint(@4294967295.0, 0, UINT32_MAX, &u), true);
    TEST_CHECK(u, UINT32_MAX);
    TEST_CHECK(declarative_number_to_uint(@-1, 0, UINT32_MAX, &u), false);
    TEST_CHECK(declarative_number_to_float(@((double)FLT_MIN), true, &f), true);
    TEST_CHECK(f, FLT_MIN);
    TEST_CHECK(declarative_number_to_float(@((double)FLT_MIN / 2.0), true, &f), false);
    TEST_CHECK(declarative_number_to_float(@((double)FLT_MAX), true, &f), true);
    TEST_CHECK(declarative_number_to_float(@(DBL_MAX), true, &f), false);
)

TEST_FUNC(declarative_exact_schema_builds_owned_model,
    const char json[] =
        "{/*phase one*/\"version\":1e0,\"config\":{\"debug_output\":true,\"layout\":\"bsp\",\"split_ratio\":0.5,"
        "\"external_bar\":{\"mode\":\"all\",\"top_padding\":1,\"bottom_padding\":2}},"
        "\"spaces\":[{\"match\":{\"index\":1.0},\"settings\":{\"layout\":\"stack\",\"window_gap\":4,\"single_window_max_width\":1600,\"single_window_width_ratio\":0.85}}],"
        "\"rules\":[{\"label\":\"terminal\",\"one_shot\":true,\"match\":{\"role\":{\"pattern\":\"^AX\",\"exclude\":false}},"
        "\"effects\":{\"display\":{\"index\":2},\"follow\":true,\"grid\":[12,12,0,0,6,12],\"opacity\":1,\"sub_layer\":\"above\"}}]}";
    struct declarative_config_model *model = NULL; struct declarative_error error = {0};
    TEST_CHECK(declarative_config_parse_bytes((uint8_t *)json, strlen(json), "candidate.jsonc", &model, &error), true);
    TEST_CHECK(model != NULL, true);
    if (model) {
        TEST_CHECK(model->config[DECLARATIVE_CONFIG_DEBUG_OUTPUT].boolean, true);
        TEST_CHECK(model->space_count, 1);
        TEST_CHECK(model->spaces[0].settings[DECLARATIVE_CONFIG_SINGLE_WINDOW_MAX_WIDTH].integer, 1600);
        TEST_CHECK(model->spaces[0].settings[DECLARATIVE_CONFIG_SINGLE_WINDOW_WIDTH_RATIO].real, 0.85f);
        TEST_CHECK(model->rules[0].matcher_present[2], true);
        TEST_CHECK(strcmp(model->rules[0].matchers[2].pattern, "^AX"), 0);
        TEST_CHECK(model->rules[0].display_index, 2);
        TEST_CHECK(model->allocated_bytes > sizeof(*model), true);
    }
    declarative_config_destroy(model);
    const char unknown[] = "{\"version\":1,\"secret-value\":42}";
    TEST_CHECK(declarative_config_parse_bytes((uint8_t *)unknown, strlen(unknown), "x", &model, &error), false);
    TEST_CHECK(error.stage, DECLARATIVE_ERROR_SCHEMA);
    TEST_CHECK(strstr(error.message, "secret-value") == NULL, true);
    const char conflict[] = "{\"version\":1,\"rules\":[{\"match\":{\"app\":{\"pattern\":\"x\"}},\"effects\":{\"scratchpad\":\"s\",\"manage\":false}}]}";
    TEST_CHECK(declarative_config_parse_bytes((uint8_t *)conflict, strlen(conflict), "x", &model, &error), false);
)

TEST_FUNC(declarative_split_ratio_is_valid_globally_and_in_space_policy,
    const char *accepted[] = {
        "{\"version\":1,\"config\":{\"split_ratio\":0.5}}",
        "{\"version\":1,\"spaces\":[{\"match\":{\"index\":2},\"settings\":{\"split_ratio\":0.6}}]}",
        "{\"version\":2,\"modes\":[{\"name\":\"work\",\"spaces\":[{\"match\":{\"index\":2},\"settings\":{\"split_ratio\":0.7}}]}]}"
    };
    for (size_t i = 0; i < sizeof(accepted) / sizeof(*accepted); ++i) {
        struct declarative_config_model *model = NULL; struct declarative_error error = {0};
        TEST_CHECK(declarative_config_parse_bytes((const uint8_t *) accepted[i], strlen(accepted[i]), "ratio", &model, &error), true);
        declarative_config_destroy(model);
    }
    const char *rejected[] = {
        "{\"version\":1,\"spaces\":[{\"match\":{\"index\":2},\"settings\":{\"split_ratio\":0.099}}]}",
        "{\"version\":1,\"spaces\":[{\"match\":{\"index\":2},\"settings\":{\"split_ratio\":0.901}}]}",
        "{\"version\":1,\"spaces\":[{\"match\":{\"index\":2},\"settings\":{\"split_ratio\":true}}]}",
        "{\"version\":1,\"spaces\":[{\"match\":{\"index\":2},\"settings\":{\"split_ratio\":\"0.6\"}}]}"
    };
    for (size_t i = 0; i < sizeof(rejected) / sizeof(*rejected); ++i) {
        struct declarative_config_model *model = NULL; struct declarative_error error = {0};
        TEST_CHECK(declarative_config_parse_bytes((const uint8_t *) rejected[i], strlen(rejected[i]), "ratio", &model, &error), false);
        TEST_CHECK(error.stage, DECLARATIVE_ERROR_SCHEMA);
        declarative_config_destroy(model);
    }
)

TEST_FUNC(declarative_file_loader_enforces_descriptor_contract,
    char path[] = "/tmp/yabai-jsonc-XXXXXX"; int fd = mkstemp(path);
    TEST_CHECK(fd >= 0, true);
    const char json[] = "{\"version\":1}";
    if (fd >= 0) { TEST_CHECK(write(fd, json, strlen(json)), (ssize_t)strlen(json)); close(fd); }
    struct declarative_config_model *model = NULL; struct declarative_error error = {0};
    TEST_CHECK(declarative_config_parse_file(path, &model, &error), true);
    declarative_config_destroy(model);
    char linkpath[PATH_MAX]; snprintf(linkpath,sizeof(linkpath),"%s-link",path);unlink(linkpath);TEST_CHECK(symlink(path,linkpath),0);TEST_CHECK(declarative_config_parse_file(linkpath,&model,&error),true);declarative_config_destroy(model);unlink(linkpath);unlink(path);
    TEST_CHECK(declarative_config_parse_file("/tmp", &model, &error), false);
    TEST_CHECK(error.stage, DECLARATIVE_ERROR_FILE);
)

TEST_FUNC(declarative_enums_are_runtime_values,
    struct enum_case { const char *key, *spelling; int value; } cases[] = {
        {"focus_follows_mouse","off",FFM_DISABLED},{"focus_follows_mouse","autofocus",FFM_AUTOFOCUS},{"focus_follows_mouse","autoraise",FFM_AUTORAISE},
        {"display_arrangement_order","default",DISPLAY_ARRANGEMENT_ORDER_DEFAULT},{"display_arrangement_order","horizontal",DISPLAY_ARRANGEMENT_ORDER_X},{"display_arrangement_order","vertical",DISPLAY_ARRANGEMENT_ORDER_Y},
        {"window_origin_display","default",WINDOW_ORIGIN_DEFAULT},{"window_origin_display","focused",WINDOW_ORIGIN_FOCUSED},{"window_origin_display","cursor",WINDOW_ORIGIN_CURSOR},
        {"window_focus_fallback","off",WINDOW_FOCUS_FALLBACK_OFF},{"window_focus_fallback","recent",WINDOW_FOCUS_FALLBACK_RECENT},
        {"window_placement","first_child",CHILD_FIRST},{"window_placement","second_child",CHILD_SECOND},
        {"window_insertion_point","focused",INSERT_FOCUSED},{"window_insertion_point","first",INSERT_FIRST},{"window_insertion_point","last",INSERT_LAST},
        {"window_shadow","off",PURIFY_ALWAYS},{"window_shadow","float",PURIFY_MANAGED},{"window_shadow","on",PURIFY_DISABLED},
        {"menubar","off",MENU_BAR_DISABLED},{"menubar","on",MENU_BAR_ENABLED},
        {"menubar_style","compact",MENU_BAR_STYLE_COMPACT},{"menubar_style","expanded",MENU_BAR_STYLE_EXPANDED},
        {"menubar_space_filter","occupied",MENU_BAR_SPACE_FILTER_OCCUPIED},{"menubar_space_filter","all",MENU_BAR_SPACE_FILTER_ALL},
        {"layout","bsp",VIEW_BSP},{"layout","stack",VIEW_STACK},{"layout","float",VIEW_FLOAT},
        {"layout","accordion_horizontal",VIEW_ACCORDION_HORIZONTAL},{"layout","accordion_vertical",VIEW_ACCORDION_VERTICAL},
        {"split_type","vertical",SPLIT_Y},{"split_type","horizontal",SPLIT_X},{"split_type","auto",SPLIT_AUTO},
        {"auto_balance","off",SPLIT_NONE},{"auto_balance","on",SPLIT_X|SPLIT_Y},{"auto_balance","x-axis",SPLIT_X},{"auto_balance","y-axis",SPLIT_Y},
        {"mouse_modifier","alt",MOUSE_MOD_ALT},{"mouse_modifier","shift",MOUSE_MOD_SHIFT},{"mouse_modifier","cmd",MOUSE_MOD_CMD},{"mouse_modifier","ctrl",MOUSE_MOD_CTRL},{"mouse_modifier","fn",MOUSE_MOD_FN},
        {"mouse_action1","move",MOUSE_MODE_MOVE},{"mouse_action1","resize",MOUSE_MODE_RESIZE},{"mouse_action2","move",MOUSE_MODE_MOVE},{"mouse_action2","resize",MOUSE_MODE_RESIZE},
        {"mouse_drop_action","swap",MOUSE_MODE_SWAP},{"mouse_drop_action","stack",MOUSE_MODE_STACK}
    };
    for (size_t i=0;i<sizeof(cases)/sizeof(*cases);++i) {
        char json[256]; snprintf(json,sizeof(json),"{\"version\":1,\"config\":{\"%s\":\"%s\"}}",cases[i].key,cases[i].spelling);
        struct declarative_config_model *model=NULL; struct declarative_error error={0};
        TEST_CHECK(declarative_config_parse_bytes((uint8_t*)json,strlen(json),"enum",&model,&error),true);
        int id=declarative_config_id_for_name(cases[i].key,strlen(cases[i].key));
        if(model) TEST_CHECK(model->config[id].enumeration,cases[i].value);
        declarative_config_destroy(model);
    }
    for(int i=0;i<EASING_TYPE_COUNT;++i) {
        char json[256]; snprintf(json,sizeof(json),"{\"version\":1,\"config\":{\"window_animation_easing\":\"%s\"}}",animation_easing_type_str[i]);
        struct declarative_config_model *model=NULL; struct declarative_error error={0};
        TEST_CHECK(declarative_config_parse_bytes((uint8_t*)json,strlen(json),"easing",&model,&error),true);
        if(model) TEST_CHECK(model->config[DECLARATIVE_CONFIG_ANIMATION_EASING].enumeration,i);
        declarative_config_destroy(model);
    }
    struct enum_case layers[]={{"sub_layer","below",LAYER_BELOW},{"sub_layer","normal",LAYER_NORMAL},{"sub_layer","above",LAYER_ABOVE},{"sub_layer","auto",LAYER_AUTO}};
    for(size_t i=0;i<4;++i){char json[256];snprintf(json,sizeof(json),"{\"version\":1,\"rules\":[{\"match\":{\"app\":{\"pattern\":\"x\"}},\"effects\":{\"sub_layer\":\"%s\"}}]}",layers[i].spelling);struct declarative_config_model*m=NULL;struct declarative_error e={0};TEST_CHECK(declarative_config_parse_bytes((uint8_t*)json,strlen(json),"layer",&m,&e),true);if(m)TEST_CHECK(m->rules[0].layer,layers[i].value);declarative_config_destroy(m);}
)

TEST_FUNC(declarative_window_focus_fallback_schema_is_strict_and_global_only,
    const char *accepted[] = {
        "{\"version\":1,\"config\":{\"window_focus_fallback\":\"off\"}}",
        "{\"version\":1,\"config\":{\"window_focus_fallback\":\"recent\"}}"
    };
    for (size_t i = 0; i < sizeof(accepted) / sizeof(*accepted); ++i) {
        struct declarative_config_model *model = NULL; struct declarative_error error = {0};
        TEST_CHECK(declarative_config_parse_bytes((const uint8_t *) accepted[i], strlen(accepted[i]), "fallback", &model, &error), true);
        declarative_config_destroy(model);
    }
    const char *rejected[] = {
        "{\"version\":1,\"config\":{\"window_focus_fallback\":\"invalid\"}}",
        "{\"version\":1,\"config\":{\"window_focus_fallback\":true}}",
        "{\"version\":1,\"config\":{\"window_focus_fallback\":1}}",
        "{\"version\":1,\"config\":{\"window_focus_fallback\":null}}",
        "{\"version\":1,\"spaces\":[{\"match\":{\"index\":1},\"settings\":{\"window_focus_fallback\":\"recent\"}}]}"
    };
    for (size_t i = 0; i < sizeof(rejected) / sizeof(*rejected); ++i) {
        struct declarative_config_model *model = NULL; struct declarative_error error = {0};
        TEST_CHECK(declarative_config_parse_bytes((const uint8_t *) rejected[i], strlen(rejected[i]), "fallback", &model, &error), false);
        TEST_CHECK(error.stage, DECLARATIVE_ERROR_SCHEMA);
        declarative_config_destroy(model);
    }
)

TEST_FUNC(declarative_follow_presence_is_validated,
    const char *effects[]={"{}","{\"follow\":false}","{\"follow\":true}","{\"display\":{\"index\":1},\"follow\":false}","{\"space\":{\"index\":1},\"follow\":true}"};
    bool accepted[]={true,false,false,true,true};
    for(size_t i=0;i<5;++i){char json[384];snprintf(json,sizeof(json),"{\"version\":1,\"rules\":[{\"match\":{\"app\":{\"pattern\":\"x\"}},\"effects\":%s}]}",effects[i]);struct declarative_config_model*m=NULL;struct declarative_error e={0};TEST_CHECK(declarative_config_parse_bytes((uint8_t*)json,strlen(json),"follow",&m,&e),accepted[i]);if(m&&i>=3){TEST_CHECK(m->rules[0].follow_present,true);TEST_CHECK(m->rules[0].follow,i==4);}declarative_config_destroy(m);}
)

static size_t declarative_alloc_call, declarative_alloc_fail, declarative_alloc_live;
static void *declarative_test_calloc(size_t n,size_t s){++declarative_alloc_call;if(declarative_alloc_fail==declarative_alloc_call)return NULL;void*p=calloc(n,s);if(p)++declarative_alloc_live;return p;}
static void declarative_test_free(void*p){if(p){--declarative_alloc_live;free(p);}}

TEST_FUNC(declarative_model_allocator_is_transactional,
    const char json[]="{\"version\":1,\"spaces\":[{\"match\":{\"uuid\":\"u\"},\"settings\":{\"layout\":\"bsp\"},\"scaffold\":[{\"pattern\":\"m\"},{\"pattern\":\"c\"},{\"pattern\":\"s\"}]}],\"rules\":[{\"label\":\"l\",\"match\":{\"app\":{\"pattern\":\"a\"},\"title\":{\"pattern\":\"t\"},\"role\":{\"pattern\":\"r\"},\"subrole\":{\"pattern\":\"s\"}},\"effects\":{\"scratchpad\":\"p\"}}]}";
    struct declarative_allocator_ops ops={declarative_test_calloc,declarative_test_free};declarative_config_set_allocator_ops(&ops);
    declarative_alloc_call=declarative_alloc_fail=declarative_alloc_live=0;struct declarative_config_model*m=NULL;struct declarative_error e={0};TEST_CHECK(declarative_config_parse_bytes((uint8_t*)json,strlen(json),"alloc",&m,&e),true);size_t total=declarative_alloc_call;size_t expected=sizeof(*m)+sizeof(*m->spaces)+2+2+2+2+sizeof(*m->rules)+2+2+2+2+2+2;if(m)TEST_CHECK(m->allocated_bytes,expected);declarative_config_destroy(m);TEST_CHECK(declarative_alloc_live,0);
    for(size_t fail=1;fail<=total;++fail){declarative_alloc_call=declarative_alloc_live=0;declarative_alloc_fail=fail;m=(void*)1;memset(&e,0,sizeof(e));TEST_CHECK(declarative_config_parse_bytes((uint8_t*)json,strlen(json),"alloc",&m,&e),false);TEST_CHECK(m==NULL,true);TEST_CHECK(e.stage,DECLARATIVE_ERROR_ALLOCATION);TEST_CHECK(declarative_alloc_live,0);}
    declarative_config_set_allocator_ops(NULL);size_t used=DECLARATIVE_CONFIG_MAX_MODEL_BYTES-1;TEST_CHECK(declarative_model_budget_charge(&used,1),true);TEST_CHECK(used,DECLARATIVE_CONFIG_MAX_MODEL_BYTES);TEST_CHECK(declarative_model_budget_charge(&used,1),false);used=SIZE_MAX;TEST_CHECK(declarative_model_budget_charge(&used,1),false);
)

static const char declarative_io_json[]="{\"version\":1}";
static int declarative_io_mode,declarative_io_open_calls,declarative_io_read_calls,declarative_io_fstat_calls,declarative_io_close_calls;static size_t declarative_io_offset;
static int declarative_io_open(const char*p,int f){(void)p;(void)f;++declarative_io_open_calls;if(declarative_io_mode==1&&declarative_io_open_calls==1){errno=EINTR;return-1;}return 42;}
static void declarative_io_stat(struct stat*s){memset(s,0,sizeof(*s));s->st_mode=S_IFREG|0600;s->st_dev=1;s->st_ino=2;s->st_size=(off_t)strlen(declarative_io_json);s->st_mtimespec.tv_sec=3;s->st_mtimespec.tv_nsec=4;s->st_ctimespec.tv_sec=5;s->st_ctimespec.tv_nsec=6;}
static int declarative_io_fstat(int fd,struct stat*s){(void)fd;++declarative_io_fstat_calls;if(declarative_io_mode==2&&declarative_io_fstat_calls==1){errno=EINTR;return-1;}if(declarative_io_mode==5&&declarative_io_fstat_calls>=2){errno=EIO;return-1;}declarative_io_stat(s);if(declarative_io_mode==4)s->st_size++;if(declarative_io_fstat_calls>=2){switch(declarative_io_mode){case 10:s->st_dev++;break;case 11:s->st_ino++;break;case 12:s->st_mode=S_IFDIR|0700;break;case 13:s->st_size++;break;case 14:s->st_mtimespec.tv_nsec++;break;case 15:s->st_ctimespec.tv_nsec++;break;}}if(declarative_io_mode==6)s->st_size=1;return 0;}
static ssize_t declarative_io_read(int fd,void*b,size_t n){(void)fd;++declarative_io_read_calls;if(declarative_io_mode==3&&declarative_io_read_calls==1){errno=EINTR;return-1;}if(declarative_io_mode==6){memset(b,' ',n);return(ssize_t)n;}size_t length=strlen(declarative_io_json);if(declarative_io_offset==length)return 0;size_t amount=length-declarative_io_offset;if(amount>3)amount=3;if(amount>n)amount=n;memcpy(b,declarative_io_json+declarative_io_offset,amount);declarative_io_offset+=amount;return(ssize_t)amount;}
static int declarative_io_close(int fd){(void)fd;++declarative_io_close_calls;if(declarative_io_mode==7&&declarative_io_close_calls==1){errno=EINTR;return-1;}return 0;}
static void declarative_io_reset(int mode){declarative_io_mode=mode;declarative_io_open_calls=declarative_io_read_calls=declarative_io_fstat_calls=declarative_io_close_calls=0;declarative_io_offset=0;}

TEST_FUNC(declarative_file_ops_cover_detectable_changes,
    struct declarative_file_ops ops={declarative_io_open,declarative_io_read,declarative_io_fstat,declarative_io_close};declarative_config_set_file_ops(&ops);
    int success_modes[]={0,1,2,3,7};for(size_t i=0;i<5;++i){declarative_io_reset(success_modes[i]);struct declarative_config_model*m=NULL;struct declarative_error e={0};TEST_CHECK(declarative_config_parse_file("scripted",&m,&e),true);declarative_config_destroy(m);TEST_CHECK(declarative_io_close_calls,success_modes[i]==7?2:1);TEST_CHECK(declarative_io_read_calls>=2,true);}
    int failure_modes[]={4,5,6,10,11,12,13,14,15};for(size_t i=0;i<9;++i){declarative_io_reset(failure_modes[i]);struct declarative_config_model*m=NULL;struct declarative_error e={0};TEST_CHECK(declarative_config_parse_file("scripted",&m,&e),false);TEST_CHECK(m==NULL,true);TEST_CHECK(declarative_io_close_calls,1);if(failure_modes[i]!=6)TEST_CHECK(declarative_io_fstat_calls>=2,true);}
    declarative_config_set_file_ops(NULL);
)

TEST_FUNC(declarative_public_paths_are_checked_and_escaped,
    const uint8_t json[]="{\"version\":1}";struct declarative_config_model*m=NULL;struct declarative_error e={0};TEST_CHECK(declarative_config_parse_bytes(json,sizeof(json)-1,"line\npath",&m,&e),true);TEST_CHECK(strcmp(e.path,"line\\u000apath"),0);declarative_config_destroy(m);char bad[]={ 'x',(char)0xff,0};TEST_CHECK(declarative_config_parse_bytes(json,sizeof(json)-1,bad,&m,&e),false);TEST_CHECK(e.stage,DECLARATIVE_ERROR_PATH);
    char*heavy=malloc(PATH_MAX);memset(heavy,1,PATH_MAX-1);heavy[0]=(char)0xc3;heavy[1]=(char)0xa9;heavy[PATH_MAX-1]=0;struct declarative_file_ops ops={declarative_io_open,declarative_io_read,declarative_io_fstat,declarative_io_close};declarative_config_set_file_ops(&ops);declarative_io_reset(0);TEST_CHECK(declarative_config_parse_file(heavy,&m,&e),true);declarative_config_destroy(m);m=NULL;char*suffix=strstr(e.path,"...<truncated>");TEST_CHECK(suffix!=NULL,true);TEST_CHECK(utf8_validate_bytes(e.path,strlen(e.path)),true);if(suffix)TEST_CHECK(((size_t)(suffix-e.path)-2)%6,0);TEST_CHECK(declarative_config_parse_bytes((const uint8_t*)"x",1,heavy,&m,&e),false);TEST_CHECK(e.stage!=DECLARATIVE_ERROR_NONE,true);TEST_CHECK(strstr(e.path,"...<truncated>")!=NULL,true);declarative_config_set_file_ops(NULL);free(heavy);
    char*longpath=malloc(PATH_MAX+1);memset(longpath,'x',PATH_MAX);longpath[PATH_MAX]=0;TEST_CHECK(declarative_config_parse_bytes(json,sizeof(json)-1,longpath,&m,&e),false);TEST_CHECK(e.stage,DECLARATIVE_ERROR_PATH);free(longpath);
)

TEST_FUNC(declarative_rule_inventory_drives_runtime_surfaces,
    const char*values[DECLARATIVE_RULE_FIELD_COUNT]={"x",NULL,"x","x","x","x",NULL,NULL,NULL,"1:1:0:0:1:1","0.5","on","off","on","above","on","pad"};
    for(int i=0;i<DECLARATIVE_RULE_FIELD_COUNT;++i){const char*alias=declarative_rule_field_descriptors[i].cli_name;if(!alias||!values[i]||i==DECLARATIVE_RULE_FIELD_APP)continue;char buffer[160]={0};size_t first=(size_t)snprintf(buffer,sizeof(buffer),"app=x");snprintf(buffer+first+1,sizeof(buffer)-first-1,"%s=%s",alias,values[i]);char*message=buffer+first+1;struct rule rule={0};TEST_CHECK(parse_rule(NULL,&message,&rule,message_test_token(buffer)),true);rule_destroy(&rule);}
    TEST_CHECK(strcmp(declarative_rule_field_descriptors[DECLARATIVE_RULE_FIELD_ONE_SHOT].cli_name,ARGUMENT_RULE_ONE_SHOT),0);
    TEST_CHECK(strcmp(declarative_rule_field_descriptors[DECLARATIVE_RULE_FIELD_DISPLAY].cli_name,ARGUMENT_RULE_KEY_DISPLAY),0);
    TEST_CHECK(strcmp(declarative_rule_field_descriptors[DECLARATIVE_RULE_FIELD_SPACE].cli_name,ARGUMENT_RULE_KEY_SPACE),0);
)

TEST_FUNC(declarative_generated_limit_boundaries,
    struct declarative_config_model*m=NULL;struct declarative_error e={0};
    NSMutableString*object=[NSMutableString stringWithString:@"{"];for(int i=0;i<257;++i){if(i)[object appendString:@","];[object appendFormat:@"\"k%d\":0",i];if(i==255){[object appendString:@"}"];NSData*d=[object dataUsingEncoding:NSUTF8StringEncoding];uint8_t*copy=malloc(d.length);memcpy(copy,d.bytes,d.length);TEST_CHECK(declarative_jsonc_normalize(copy,d.length,&e),true);free(copy);[object deleteCharactersInRange:NSMakeRange(object.length-1,1)];}}[object appendString:@"}"];NSData*members=[object dataUsingEncoding:NSUTF8StringEncoding];uint8_t*member_copy=malloc(members.length);memcpy(member_copy,members.bytes,members.length);TEST_CHECK(declarative_jsonc_normalize(member_copy,members.length,&e),false);free(member_copy);
    for(size_t n=DECLARATIVE_CONFIG_MAX_KEY_BYTES;n<=DECLARATIVE_CONFIG_MAX_KEY_BYTES+1;++n){NSMutableString*s=[NSMutableString stringWithString:@"{\""];for(size_t i=0;i<n;++i)[s appendString:@"k"];[s appendString:@"\":0}"];NSData*d=[s dataUsingEncoding:NSUTF8StringEncoding];uint8_t*copy=malloc(d.length);memcpy(copy,d.bytes,d.length);TEST_CHECK(declarative_jsonc_normalize(copy,d.length,&e),n==DECLARATIVE_CONFIG_MAX_KEY_BYTES);free(copy);}
    NSMutableString*spaces=[NSMutableString stringWithString:@"{\"version\":1,\"spaces\":["];for(size_t i=0;i<DECLARATIVE_CONFIG_MAX_SPACES+1;++i){if(i)[spaces appendString:@","];[spaces appendString:@"{\"match\":{\"index\":1},\"settings\":{\"layout\":\"bsp\"}}"];if(i+1==DECLARATIVE_CONFIG_MAX_SPACES){[spaces appendString:@"]}"];NSData*d=[spaces dataUsingEncoding:NSUTF8StringEncoding];TEST_CHECK(declarative_config_parse_bytes(d.bytes,d.length,"spaces",&m,&e),true);if(m)TEST_CHECK(m->space_count,DECLARATIVE_CONFIG_MAX_SPACES);declarative_config_destroy(m);m=NULL;[spaces deleteCharactersInRange:NSMakeRange(spaces.length-2,2)];}}[spaces appendString:@"]}"];NSData*spaces_over=[spaces dataUsingEncoding:NSUTF8StringEncoding];TEST_CHECK(declarative_config_parse_bytes(spaces_over.bytes,spaces_over.length,"spaces",&m,&e),false);TEST_CHECK(strcmp(e.json_path,"$.spaces"),0);
    NSMutableString*rules=[NSMutableString stringWithString:@"{\"version\":1,\"rules\":["];for(size_t i=0;i<DECLARATIVE_CONFIG_MAX_RULES+1;++i){if(i)[rules appendString:@","];[rules appendString:@"{\"match\":{\"app\":{\"pattern\":\"x\"}}}"];if(i+1==DECLARATIVE_CONFIG_MAX_RULES){[rules appendString:@"]}"];NSData*d=[rules dataUsingEncoding:NSUTF8StringEncoding];TEST_CHECK(declarative_config_parse_bytes(d.bytes,d.length,"rules",&m,&e),true);if(m)TEST_CHECK(m->rule_count,DECLARATIVE_CONFIG_MAX_RULES);declarative_config_destroy(m);m=NULL;[rules deleteCharactersInRange:NSMakeRange(rules.length-2,2)];}}[rules appendString:@"]}"];NSData*rules_over=[rules dataUsingEncoding:NSUTF8StringEncoding];TEST_CHECK(declarative_config_parse_bytes(rules_over.bytes,rules_over.length,"rules",&m,&e),false);TEST_CHECK(strcmp(e.json_path,"$.rules"),0);
    struct string_case{const char*field;size_t limit;const char*prefix;const char*suffix;}strings[]={{"uuid",DECLARATIVE_CONFIG_MAX_STRING_BYTES,"{\"version\":1,\"spaces\":[{\"match\":{\"uuid\":\"","\"},\"settings\":{\"layout\":\"bsp\"}}]}"},{"label",DECLARATIVE_CONFIG_MAX_STRING_BYTES,"{\"version\":1,\"rules\":[{\"label\":\"","\",\"match\":{\"app\":{\"pattern\":\"x\"}}}]}"},{"scratchpad",DECLARATIVE_CONFIG_MAX_STRING_BYTES,"{\"version\":1,\"rules\":[{\"match\":{\"app\":{\"pattern\":\"x\"}},\"effects\":{\"scratchpad\":\"","\"}}]}"},{"pattern",DECLARATIVE_CONFIG_MAX_PATTERN_BYTES,"{\"version\":1,\"rules\":[{\"match\":{\"app\":{\"pattern\":\"","\"}}}]}"}};
    for(size_t c=0;c<sizeof(strings)/sizeof(*strings);++c)for(size_t extra=0;extra<2;++extra){NSMutableString*s=[NSMutableString stringWithUTF8String:strings[c].prefix];for(size_t i=0;i<strings[c].limit+extra;++i)[s appendString:@"a"];[s appendString:[NSString stringWithUTF8String:strings[c].suffix]];NSData*d=[s dataUsingEncoding:NSUTF8StringEncoding];TEST_CHECK(declarative_config_parse_bytes(d.bytes,d.length,"string",&m,&e),extra==0);declarative_config_destroy(m);m=NULL;if(extra)TEST_CHECK(strstr(e.json_path,strings[c].field)!=NULL,true);}
)

TEST_FUNC(declarative_schema_paths_are_exact_and_redacted,
    struct path_case{const char*json;const char*path;}cases[]={
        {"{}","$.version"},{"{\"version\":3}","$.version"},{"{\"version\":1,\"candidateSecret\":9}","$.<unknown>"},{"{\"version\":1,\"config\":7}","$.config"},{"{\"version\":1,\"config\":{\"candidateSecret\":9}}","$.config.<unknown>"},{"{\"version\":1,\"config\":{\"external_bar\":{\"mode\":\"off\",\"top_padding\":0}}}","$.config.external_bar.bottom_padding"},{"{\"version\":1,\"config\":{\"external_bar\":{\"mode\":\"off\",\"top_padding\":0,\"bottom_padding\":0,\"candidateSecret\":1}}}","$.config.external_bar.<unknown>"},
        {"{\"version\":1,\"spaces\":{}}","$.spaces"},{"{\"version\":1,\"spaces\":[7]}","$.spaces[0]"},{"{\"version\":1,\"spaces\":[{\"match\":{\"index\":1}}]}","$.spaces[0].settings"},{"{\"version\":1,\"spaces\":[{\"match\":{\"candidateSecret\":1},\"settings\":{\"layout\":\"bsp\"}}]}","$.spaces[0].match.<unknown>"},{"{\"version\":1,\"spaces\":[{\"match\":{\"uuid\":\"\"},\"settings\":{\"layout\":\"bsp\"}}]}","$.spaces[0].match.uuid"},{"{\"version\":1,\"spaces\":[{\"match\":{\"index\":1},\"settings\":{}}]}","$.spaces[0].settings"},
        {"{\"version\":1,\"rules\":{}}","$.rules"},{"{\"version\":1,\"rules\":[7]}","$.rules[0]"},{"{\"version\":1,\"rules\":[{}]}","$.rules[0].match"},{"{\"version\":1,\"rules\":[{\"match\":{}}]}","$.rules[0].match"},{"{\"version\":1,\"rules\":[{\"candidateSecret\":1,\"match\":{\"app\":{\"pattern\":\"x\"}}}]}","$.rules[0].<unknown>"},{"{\"version\":1,\"rules\":[{\"one_shot\":1,\"match\":{\"app\":{\"pattern\":\"x\"}}}]}","$.rules[0].one_shot"},{"{\"version\":1,\"rules\":[{\"match\":{\"candidateSecret\":{}}}]}","$.rules[0].match.<unknown>"},{"{\"version\":1,\"rules\":[{\"match\":{\"app\":{}}}]}","$.rules[0].match.app.pattern"},{"{\"version\":1,\"rules\":[{\"match\":{\"app\":{\"pattern\":\"x\",\"candidateSecret\":1}}}]}","$.rules[0].match.app.<unknown>"},{"{\"version\":1,\"rules\":[{\"match\":{\"app\":{\"pattern\":\"x\",\"exclude\":1}}}]}","$.rules[0].match.app.exclude"},
        {"{\"version\":1,\"rules\":[{\"match\":{\"app\":{\"pattern\":\"x\"}},\"effects\":7}]}","$.rules[0].effects"},{"{\"version\":1,\"rules\":[{\"match\":{\"app\":{\"pattern\":\"x\"}},\"effects\":{\"candidateSecret\":1}}]}","$.rules[0].effects.<unknown>"},{"{\"version\":1,\"rules\":[{\"match\":{\"app\":{\"pattern\":\"x\"}},\"effects\":{\"display\":{}}}]}","$.rules[0].effects.display.index"},{"{\"version\":1,\"rules\":[{\"match\":{\"app\":{\"pattern\":\"x\"}},\"effects\":{\"space\":{\"index\":0}}}]}","$.rules[0].effects.space.index"},{"{\"version\":1,\"rules\":[{\"match\":{\"app\":{\"pattern\":\"x\"}},\"effects\":{\"grid\":[1,1,0,-1,1,1]}}]}","$.rules[0].effects.grid[3]"},{"{\"version\":1,\"rules\":[{\"match\":{\"app\":{\"pattern\":\"x\"}},\"effects\":{\"follow\":false}}]}","$.rules[0].effects.follow"}};
    for(size_t i=0;i<sizeof(cases)/sizeof(*cases);++i){struct declarative_config_model*m=NULL;struct declarative_error e={0};TEST_CHECK(declarative_config_parse_bytes((uint8_t*)cases[i].json,strlen(cases[i].json),"paths",&m,&e),false);TEST_CHECK(strcmp(e.json_path,cases[i].path),0);TEST_CHECK(strstr(e.json_path,"candidateSecret")==NULL,true);TEST_CHECK(strstr(e.message,"candidateSecret")==NULL,true);TEST_CHECK(e.line>0,true);declarative_config_destroy(m);}
    for(int id=0;id<DECLARATIVE_CONFIG_COUNT;++id){char json[300],expected[200];snprintf(json,sizeof(json),"{\"version\":1,\"config\":{\"%s\":null}}",declarative_config_descriptors[id].name);snprintf(expected,sizeof(expected),"$.config.%s",declarative_config_descriptors[id].name);struct declarative_config_model*m=NULL;struct declarative_error e={0};TEST_CHECK(declarative_config_parse_bytes((uint8_t*)json,strlen(json),"config-fields",&m,&e),false);TEST_CHECK(strcmp(e.json_path,expected)==0||strstr(e.json_path,expected)==e.json_path,true);declarative_config_destroy(m);}
    const char*matcher_names[]={"app","title","role","subrole"};for(size_t i=0;i<4;++i){char json[320],expected[200];snprintf(json,sizeof(json),"{\"version\":1,\"rules\":[{\"match\":{\"%s\":{\"pattern\":\"x\",\"exclude\":1}}}]}",matcher_names[i]);snprintf(expected,sizeof(expected),"$.rules[0].match.%s.exclude",matcher_names[i]);struct declarative_config_model*m=NULL;struct declarative_error e={0};TEST_CHECK(declarative_config_parse_bytes((uint8_t*)json,strlen(json),"matchers",&m,&e),false);TEST_CHECK(strcmp(e.json_path,expected),0);}
    for(int id=DECLARATIVE_RULE_FIELD_DISPLAY;id<DECLARATIVE_RULE_FIELD_COUNT;++id){const char*name=declarative_rule_field_descriptors[id].json_name;char json[400],expected[200];snprintf(json,sizeof(json),"{\"version\":1,\"rules\":[{\"match\":{\"app\":{\"pattern\":\"x\"}},\"effects\":{\"%s\":null}}]}",name);snprintf(expected,sizeof(expected),"$.rules[0].effects.%s",name);struct declarative_config_model*m=NULL;struct declarative_error e={0};TEST_CHECK(declarative_config_parse_bytes((uint8_t*)json,strlen(json),"effects",&m,&e),false);TEST_CHECK(strncmp(e.json_path,expected,strlen(expected)),0);}
    const char*duplicates[]={"{\"version\":1,\"config\":{\"layout\":\"bsp\",\"layout\":\"stack\"}}","{\"version\":1,\"rules\":[{\"match\":{\"app\":{\"pattern\":\"x\",\"pattern\":\"secret\"}}}]}"};for(size_t i=0;i<2;++i){struct declarative_config_model*m=NULL;struct declarative_error e={0};TEST_CHECK(declarative_config_parse_bytes((uint8_t*)duplicates[i],strlen(duplicates[i]),"dup",&m,&e),false);TEST_CHECK(strstr(e.json_path,"<duplicate>")!=NULL,true);TEST_CHECK(e.line>0&&e.column>0,true);TEST_CHECK(strstr(e.message,"secret")==NULL,true);}
)

TEST_FUNC(declarative_exact_integral_lexemes_are_enforced,
    const char*invalid[]={
        "{\"version\":1.00000000000000000001}",
        "{\"version\":1,\"config\":{\"top_padding\":-2147483648.00000000000000000001}}",
        "{\"version\":1,\"config\":{\"top_padding\":2147483647.00000000000000000001}}",
        "{\"version\":1,\"config\":{\"insert_feedback_color\":4294967295.00000000000000000001}}",
        "{\"version\":1,\"spaces\":[{\"match\":{\"index\":1.00000000000000000001},\"settings\":{\"layout\":\"bsp\"}}]}",
        "{\"version\":1,\"rules\":[{\"match\":{\"app\":{\"pattern\":\"x\"}},\"effects\":{\"display\":{\"index\":2147483647.00000000000000000001}}}]}",
        "{\"version\":1,\"rules\":[{\"match\":{\"app\":{\"pattern\":\"x\"}},\"effects\":{\"grid\":[1,1,0,0,4294967295.00000000000000000001,1]}}]}"};
    for(size_t i=0;i<sizeof(invalid)/sizeof(*invalid);++i){struct declarative_config_model*m=NULL;struct declarative_error e={0};TEST_CHECK(declarative_config_parse_bytes((const uint8_t*)invalid[i],strlen(invalid[i]),"exact",&m,&e),false);TEST_CHECK(e.stage,DECLARATIVE_ERROR_SCHEMA);declarative_config_destroy(m);}
    const char*valid[]={"{\"version\":1}","{\"version\":1.0}","{\"version\":1e0}","{\"version\":10e-1}","{\"version\":0.01e2}","{\"version\":1,\"config\":{\"top_padding\":21474836470e-1}}","{\"version\":1,\"rules\":[{\"match\":{\"app\":{\"pattern\":\"x\"}},\"effects\":{\"grid\":[1e0,1.0,0e9,0.00,10e-1,1]}}]}"};
    for(size_t i=0;i<sizeof(valid)/sizeof(*valid);++i){struct declarative_config_model*m=NULL;struct declarative_error e={0};TEST_CHECK(declarative_config_parse_bytes((const uint8_t*)valid[i],strlen(valid[i]),"exact",&m,&e),true);declarative_config_destroy(m);}
)

TEST_FUNC(declarative_numeric_offsets_scale_with_bounded_traversals,
    const char small[]="{\"rul\\u0065s\":[{\"eff\\u0065cts\":{\"gr\\u0069d\":[1,1,0,0,6,12],\"display\":{\"ind\\u0065x\":2}},\"match\":{\"app\":{\"pattern\":\"x\"}}}],\"spaces\":[{\"settings\":{\"window_gap\":4},\"match\":{\"index\":1}}],\"config\":{\"external_bar\":{\"bottom_padding\":2,\"mode\":\"all\",\"top_padding\":1},\"top_padding\":3},\"v\\u0065rsion\":1}";
    struct declarative_config_model*m=NULL;struct declarative_error e={0};TEST_CHECK(declarative_config_parse_bytes((const uint8_t*)small,strlen(small),"offset-small",&m,&e),true);size_t small_traversals=declarative_config_get_structural_traversal_count();TEST_CHECK(small_traversals<=2,true);if(m){TEST_CHECK(m->config[DECLARATIVE_CONFIG_TOP_PADDING].integer,3);TEST_CHECK(m->config[DECLARATIVE_CONFIG_EXTERNAL_BAR].external_bar.bottom_padding,2);TEST_CHECK(m->spaces[0].index,1);TEST_CHECK(m->rules[0].display_index,2);TEST_CHECK(m->rules[0].grid[5],12U);}declarative_config_destroy(m);m=NULL;
    NSMutableString*large=[NSMutableString stringWithString:@"{\"rules\":["];for(size_t i=0;i<DECLARATIVE_CONFIG_MAX_RULES;++i){if(i)[large appendString:@","];[large appendFormat:@"{\"effects\":{\"space\":{\"index\":%zu},\"grid\":[1,1,0,0,%zu,1]},\"match\":{\"app\":{\"pattern\":\"x\"}}}",i+1,i];}[large appendString:@"],\"version\":1}"];NSData*data=[large dataUsingEncoding:NSUTF8StringEncoding];TEST_CHECK(declarative_config_parse_bytes(data.bytes,data.length,"offset-large",&m,&e),true);size_t large_traversals=declarative_config_get_structural_traversal_count();TEST_CHECK(large_traversals,small_traversals);TEST_CHECK(large_traversals<=2,true);if(m){TEST_CHECK(m->rule_count,DECLARATIVE_CONFIG_MAX_RULES);TEST_CHECK(m->rules[1023].space_index,1024);TEST_CHECK(m->rules[1023].grid[4],1023U);}declarative_config_destroy(m);m=NULL;
    const char invalid[]="{\"rules\":[{\"effects\":{\"gr\\u0069d\":[1,1,0,0,1.00000000000000000001,1]},\"match\":{\"app\":{\"pattern\":\"x\"}}}],\"version\":1}";TEST_CHECK(declarative_config_parse_bytes((const uint8_t*)invalid,strlen(invalid),"offset-invalid",&m,&e),false);TEST_CHECK(e.stage,DECLARATIVE_ERROR_SCHEMA);TEST_CHECK(strcmp(e.json_path,"$.rules[0].effects.grid[4]"),0);declarative_config_destroy(m);
)

TEST_FUNC(declarative_signed_integer_boundaries_are_overflow_safe,
    const char*negative[]={"-9223372036854775808","-9223372036854775809","-9223372036854775808e0","-9223372036854775809e0","-92233720368547758080e-1"};
    const char*forms[]={"{\"version\":%s}","{\"version\":1,\"spaces\":[{\"match\":{\"index\":%s},\"settings\":{\"layout\":\"bsp\"}}]}","{\"version\":1,\"rules\":[{\"match\":{\"app\":{\"pattern\":\"x\"}},\"effects\":{\"display\":{\"index\":%s}}}]}","{\"version\":1,\"rules\":[{\"match\":{\"app\":{\"pattern\":\"x\"}},\"effects\":{\"space\":{\"index\":%s}}}]}"};
    for(size_t f=0;f<sizeof(forms)/sizeof(*forms);++f)for(size_t n=0;n<sizeof(negative)/sizeof(*negative);++n){char json[512];snprintf(json,sizeof(json),forms[f],negative[n]);struct declarative_config_model*m=NULL;struct declarative_error e={0};TEST_CHECK(declarative_config_parse_bytes((const uint8_t*)json,strlen(json),"signed-boundary",&m,&e),false);TEST_CHECK(e.stage,DECLARATIVE_ERROR_SCHEMA);declarative_config_destroy(m);}
    const char*int_min_valid[]={"-2147483648","-2147483648e0","-21474836480e-1"};
    const char*int_min_invalid[]={"-2147483649","-2147483649e0","-21474836490e-1","-9223372036854775808"};
    for(size_t i=0;i<sizeof(int_min_valid)/sizeof(*int_min_valid);++i){char json[256];snprintf(json,sizeof(json),"{\"version\":1,\"config\":{\"top_padding\":%s}}",int_min_valid[i]);struct declarative_config_model*m=NULL;struct declarative_error e={0};TEST_CHECK(declarative_config_parse_bytes((const uint8_t*)json,strlen(json),"int-min",&m,&e),true);if(m)TEST_CHECK(m->config[DECLARATIVE_CONFIG_TOP_PADDING].integer,INT_MIN);declarative_config_destroy(m);}
    for(size_t i=0;i<sizeof(int_min_invalid)/sizeof(*int_min_invalid);++i){char json[256];snprintf(json,sizeof(json),"{\"version\":1,\"config\":{\"top_padding\":%s}}",int_min_invalid[i]);struct declarative_config_model*m=NULL;struct declarative_error e={0};TEST_CHECK(declarative_config_parse_bytes((const uint8_t*)json,strlen(json),"int-min",&m,&e),false);TEST_CHECK(e.stage,DECLARATIVE_ERROR_SCHEMA);declarative_config_destroy(m);}
    const uint8_t int64_min[]="-92233720368547758080e-1";struct model_builder b={.source=int64_min,.source_length=sizeof(int64_min)-1};uint64_t magnitude=0;bool negative_value=false;int64_t value=0;TEST_CHECK(exact_integer_at(&b,0,true,INT64_MIN,INT64_MAX,&magnitude,&negative_value),true);TEST_CHECK(magnitude,(uint64_t)INT64_MAX+1);TEST_CHECK(negative_value,true);TEST_CHECK(signed_magnitude_to_int64(magnitude,negative_value,&value),true);TEST_CHECK(value,INT64_MIN);
)

TEST_FUNC(declarative_positive_integer_boundaries_are_overflow_safe,
    const char*versions[]={"184467440737095516161","184467440737095516161.0","184467440737095516161e0"};
    for(size_t i=0;i<sizeof(versions)/sizeof(*versions);++i){char json[128];snprintf(json,sizeof(json),"{\"version\":%s}",versions[i]);struct declarative_config_model*m=NULL;struct declarative_error e={0};TEST_CHECK(declarative_config_parse_bytes((const uint8_t*)json,strlen(json),"positive-overflow",&m,&e),false);TEST_CHECK(e.stage,DECLARATIVE_ERROR_SCHEMA);declarative_config_destroy(m);}
    struct exact_case{const char*lexeme;uint64_t limit;bool accepted;uint64_t expected;}cases[]={
        {"1",1,true,1},{"2",1,false,0},{"184467440737095516161",1,false,0},{"184467440737095516161.0",1,false,0},{"184467440737095516161e0",1,false,0},
        {"18446744073709551615",UINT64_MAX,true,UINT64_MAX},{"18446744073709551616",UINT64_MAX,false,0},{"18446744073709551616.0",UINT64_MAX,false,0},{"18446744073709551616e0",UINT64_MAX,false,0}};
    for(size_t i=0;i<sizeof(cases)/sizeof(*cases);++i){const uint8_t*source=(const uint8_t*)cases[i].lexeme;struct model_builder b={.source=source,.source_length=strlen(cases[i].lexeme)};uint64_t magnitude=0;bool negative=false;TEST_CHECK(exact_integer_at(&b,0,false,0,cases[i].limit,&magnitude,&negative),cases[i].accepted);if(cases[i].accepted){TEST_CHECK(magnitude,cases[i].expected);TEST_CHECK(negative,false);}}
)

TEST_FUNC(declarative_source_positions_handle_cr_and_crlf,
    const char*comments[]={"{\r/* unterminated","{\r\n/* unterminated"};for(size_t i=0;i<2;++i){struct declarative_error e={0};uint8_t*b=(uint8_t*)strdup(comments[i]);TEST_CHECK(declarative_jsonc_normalize(b,strlen((char*)b),&e),false);TEST_CHECK(e.stage,DECLARATIVE_ERROR_JSONC);TEST_CHECK(e.line,2);TEST_CHECK(e.column,1);free(b);}
    const char*strict[]={"{\r\"version\":truX}","{\r\n\"version\":truX}"};for(size_t i=0;i<2;++i){struct declarative_config_model*m=NULL;struct declarative_error e={0};TEST_CHECK(declarative_config_parse_bytes((const uint8_t*)strict[i],strlen(strict[i]),"position",&m,&e),false);TEST_CHECK(e.stage,DECLARATIVE_ERROR_JSON);TEST_CHECK(e.line,2);TEST_CHECK(e.column,11);}
)

TEST_FUNC(declarative_numeric_field_matrix,
    struct numeric_case{const char*key;const char*good;const char*bad;}cases[]={{"window_opacity_duration","0","-1"},{"window_animation_duration","0","-1"},{"menubar_opacity","1","1.0001"},{"active_window_opacity","1","0"},{"normal_window_opacity","1","0"},{"insert_feedback_color","4294967295","4294967296"},{"insert_feedback_radius","0","-1"},{"insert_feedback_width","0.25","0"},{"top_padding","-2147483648","-2147483649"},{"bottom_padding","2147483647","2147483648"},{"left_padding","0","0.5"},{"right_padding","0","true"},{"window_gap","0","1e100"},{"accordion_padding","0","-1"},{"single_window_max_width","0","-1"},{"single_window_width_ratio","0.85","0"},{"split_ratio","0.1","0.099"}};
    for(size_t i=0;i<sizeof(cases)/sizeof(*cases);++i)for(int valid=0;valid<2;++valid){char json[300];snprintf(json,sizeof(json),"{\"version\":1,\"config\":{\"%s\":%s}}",cases[i].key,valid?cases[i].good:cases[i].bad);struct declarative_config_model*m=NULL;struct declarative_error e={0};TEST_CHECK(declarative_config_parse_bytes((uint8_t*)json,strlen(json),"numeric",&m,&e),valid);declarative_config_destroy(m);}
    double roundable=nextafter((double)FLT_MIN,DBL_MAX);float f=0;TEST_CHECK(declarative_number_to_float(@0.0,true,&f),true);TEST_CHECK(declarative_number_to_float(@0.0,false,&f),false);TEST_CHECK(declarative_number_to_float(@(nextafter((double)FLT_MIN,0.0)),true,&f),false);TEST_CHECK(declarative_number_to_float(@(roundable),true,&f),true);TEST_CHECK(declarative_number_to_float(@((double)FLT_MAX),true,&f),true);TEST_CHECK(declarative_number_to_float(@(nextafter((double)FLT_MAX,DBL_MAX)),true,&f),false);
    int iv=0;uint32_t uv=0;TEST_CHECK(declarative_number_to_int(@((double)INT_MIN),INT_MIN,INT_MAX,&iv),true);TEST_CHECK(declarative_number_to_int(@((double)INT_MAX),INT_MIN,INT_MAX,&iv),true);TEST_CHECK(declarative_number_to_int(@((double)INT_MAX+1.0),INT_MIN,INT_MAX,&iv),false);TEST_CHECK(declarative_number_to_uint(@0.0,0,UINT_MAX,&uv),true);TEST_CHECK(declarative_number_to_uint(@4294967295.0,0,UINT_MAX,&uv),true);
    const char*targets[]={"{\"display\":{\"index\":2147483647},\"follow\":false}","{\"space\":{\"index\":2147483647},\"follow\":true}","{\"display\":{\"index\":1},\"space\":{\"index\":1},\"follow\":true}","{\"display\":{\"index\":2147483648}}","{\"space\":{\"index\":0}}"};bool target_ok[]={true,true,true,false,false};for(size_t i=0;i<5;++i){char json[500];snprintf(json,sizeof(json),"{\"version\":1,\"rules\":[{\"match\":{\"app\":{\"pattern\":\"x\"}},\"effects\":%s}]}",targets[i]);struct declarative_config_model*m=NULL;struct declarative_error e={0};TEST_CHECK(declarative_config_parse_bytes((uint8_t*)json,strlen(json),"targets",&m,&e),target_ok[i]);declarative_config_destroy(m);}
    const char*opacities[]={"0","1","-0.001","1.001"};bool opacity_ok[]={true,true,false,false};for(size_t i=0;i<4;++i){char json[400];snprintf(json,sizeof(json),"{\"version\":1,\"rules\":[{\"match\":{\"app\":{\"pattern\":\"x\"}},\"effects\":{\"opacity\":%s}}]}",opacities[i]);struct declarative_config_model*m=NULL;struct declarative_error e={0};TEST_CHECK(declarative_config_parse_bytes((uint8_t*)json,strlen(json),"opacity",&m,&e),opacity_ok[i]);declarative_config_destroy(m);}
    const char*bars[]={"{\"mode\":\"off\",\"top_padding\":-2147483648,\"bottom_padding\":2147483647}","{\"mode\":\"off\",\"top_padding\":-2147483649,\"bottom_padding\":0}","{\"mode\":\"off\",\"top_padding\":0,\"bottom_padding\":2147483648}"};for(size_t i=0;i<3;++i){char json[400];snprintf(json,sizeof(json),"{\"version\":1,\"config\":{\"external_bar\":%s}}",bars[i]);struct declarative_config_model*m=NULL;struct declarative_error e={0};TEST_CHECK(declarative_config_parse_bytes((uint8_t*)json,strlen(json),"bar",&m,&e),i==0);declarative_config_destroy(m);}
    const char*grid[]={"[1,1,0,0,1,1]","[0,1,0,0,1,1]","[1,0,0,0,1,1]","[1,1,0,0,4294967295,1]","[1,1,0,0,4294967296,1]","[1,1,0,0,1.5,1]"};bool ok[]={true,false,false,true,false,false};for(size_t i=0;i<6;++i){char json[400];snprintf(json,sizeof(json),"{\"version\":1,\"rules\":[{\"match\":{\"app\":{\"pattern\":\"x\"}},\"effects\":{\"grid\":%s}}]}",grid[i]);struct declarative_config_model*m=NULL;struct declarative_error e={0};TEST_CHECK(declarative_config_parse_bytes((uint8_t*)json,strlen(json),"grid",&m,&e),ok[i]);declarative_config_destroy(m);}
)

TEST_FUNC(declarative_phase1_boundary_matrix,
    struct declarative_config_model*m=NULL;struct declarative_error e={0};
    uint8_t raw_nul[]={'{','}',0};TEST_CHECK(declarative_config_parse_bytes(raw_nul,sizeof(raw_nul),"nul",&m,&e),false);TEST_CHECK(e.stage,DECLARATIVE_ERROR_ENCODING);
    const char*invalid[]={"{\"version\":1,/*x}","{\"version\":1,//x","{\"version\":1,}","{'version':1}","{version:1}","{\"version\":0x1}","{\"version\":NaN}","{\"version\":1,\"config\":{\"layout\":\"bsp\",\"layout\":\"stack\"}}","{\"version\":1,\"rules\":[{\"match\":{\"app\":{\"pattern\":\"x\"}},\"effects\":{\"scratchpad\":\"float\"}}]}","{\"version\":1,\"rules\":[{\"label\":\"x\",\"match\":{\"app\":{\"pattern\":\"x\"}}},{\"label\":\"x\",\"match\":{\"title\":{\"pattern\":\"x\"}}}]}"};
    for(size_t i=0;i<sizeof(invalid)/sizeof(*invalid);++i)TEST_CHECK(declarative_config_parse_bytes((uint8_t*)invalid[i],strlen(invalid[i]),"matrix",&m,&e),false);
    const char*reserved[]={"float","sticky","shadow","split","zoom-parent","zoom-fullscreen","windowed-fullscreen","native-fullscreen","expose","pip","recover"};for(size_t i=0;i<sizeof(reserved)/sizeof(*reserved);++i){char json[450];snprintf(json,sizeof(json),"{\"version\":1,\"rules\":[{\"match\":{\"app\":{\"pattern\":\"x\"}},\"effects\":{\"scratchpad\":\"%s\"}}]}",reserved[i]);TEST_CHECK(declarative_config_parse_bytes((uint8_t*)json,strlen(json),"reserved",&m,&e),false);TEST_CHECK(strcmp(e.json_path,"$.rules[0].effects.scratchpad"),0);}
    uint8_t utf16le[]={0xff,0xfe,'{',0};uint8_t utf16be[]={0xfe,0xff,'{',0};uint8_t utf32be[]={0,0,0xfe,0xff};uint8_t misplaced[]={'{',' ',0xef,0xbb,0xbf,'}'};uint8_t*encodings[]={utf16le,utf16be,utf32be,misplaced};size_t encoding_sizes[]={sizeof(utf16le),sizeof(utf16be),sizeof(utf32be),sizeof(misplaced)};for(size_t i=0;i<4;++i){TEST_CHECK(declarative_config_parse_bytes(encodings[i],encoding_sizes[i],"encoding",&m,&e),false);TEST_CHECK(e.stage,DECLARATIVE_ERROR_ENCODING);}
    const char*valid[]={"{\"version\":1.0}","{\"version\":1e0}","{\"version\":1,\"config\":{\"layout\":\"bsp\"},\"rules\":[{\"match\":{\"role\":{\"pattern\":\"r\"}}}]}","{\"version\":1,\"rules\":[{\"match\":{\"subrole\":{\"pattern\":\"s\"}}}]}","{\"version\":1,\"config\":{},\"spaces\":[],\"rules\":[]}"};
    for(size_t i=0;i<sizeof(valid)/sizeof(*valid);++i){TEST_CHECK(declarative_config_parse_bytes((uint8_t*)valid[i],strlen(valid[i]),"matrix",&m,&e),true);declarative_config_destroy(m);m=NULL;}
    size_t cap=DECLARATIVE_CONFIG_MAX_FILE_SIZE;uint8_t*large=malloc(cap+1);memset(large,' ',cap+1);memcpy(large,"{\"version\":1}",13);TEST_CHECK(declarative_config_parse_bytes(large,cap,"cap",&m,&e),true);declarative_config_destroy(m);m=NULL;TEST_CHECK(declarative_config_parse_bytes(large,cap+1,"cap",&m,&e),false);free(large);
    for(size_t depth=DECLARATIVE_CONFIG_MAX_DEPTH;depth<=DECLARATIVE_CONFIG_MAX_DEPTH+1;++depth){size_t n=depth*2+1;uint8_t*s=malloc(n);for(size_t i=0;i<depth;++i)s[i]='[';s[depth]='0';for(size_t i=0;i<depth;++i)s[depth+1+i]=']';TEST_CHECK(declarative_jsonc_normalize(s,n,&e),depth==DECLARATIVE_CONFIG_MAX_DEPTH);free(s);}
    const char regex[]="{\"version\":1,\"rules\":[{\"match\":{\"app\":{\"pattern\":\"[\"}}}]}";TEST_CHECK(declarative_config_parse_bytes((uint8_t*)regex,strlen(regex),"matrix",&m,&e),false);TEST_CHECK(e.stage,DECLARATIVE_ERROR_REGEX);TEST_CHECK(strstr(e.message,"[")==NULL,true);TEST_CHECK(strcmp(e.json_path,"$.rules[0].match.app.pattern"),0);TEST_CHECK(e.line>0,true);
    char mutable_source[]="{\"version\":1,\"spaces\":[{\"match\":{\"uuid\":\"stable\"},\"settings\":{\"layout\":\"bsp\"}}]}";TEST_CHECK(declarative_config_parse_bytes((uint8_t*)mutable_source,strlen(mutable_source),"owned",&m,&e),true);memset(mutable_source,'x',strlen(mutable_source));if(m)TEST_CHECK(strcmp(m->spaces[0].uuid,"stable"),0);declarative_config_destroy(m);
)

TEST_FUNC(declarative_scaffold_schema_is_strict_and_owned,
{
    char valid[] = "{\"version\":1,\"spaces\":[{\"match\":{\"index\":2},\"scaffold\":[{\"pattern\":\"^Mail$\"},{\"pattern\":\"Calendar\",\"exclude\":false},{\"pattern\":\"Messages\"}]}]}";
    struct declarative_config_model *model = NULL; struct declarative_error error = {0};
    TEST_CHECK(declarative_config_parse_bytes((const uint8_t *) valid, strlen(valid), "scaffold", &model, &error), true);
    TEST_CHECK(model != NULL, true);
    memset(valid, 'x', strlen(valid));
    if (model) {
        TEST_CHECK(model->space_count, 1UL);
        TEST_CHECK(model->spaces[0].scaffold_count, 3UL);
        TEST_CHECK(strcmp(model->spaces[0].scaffold[0].pattern, "^Mail$"), 0);
        TEST_CHECK(model->spaces[0].scaffold[1].exclude, false);
    }
    declarative_config_destroy(model); model = NULL;
    const char *invalid[] = {
        "{\"version\":1,\"spaces\":[{\"match\":{\"index\":2},\"scaffold\":[]}]}",
        "{\"version\":1,\"spaces\":[{\"match\":{\"index\":2},\"scaffold\":[{\"pattern\":\"a\"},{\"pattern\":\"b\"},{\"pattern\":\"c\"},{\"pattern\":\"d\"}]}]}",
        "{\"version\":1,\"spaces\":[{\"match\":{\"index\":2},\"scaffold\":[{\"app\":{\"pattern\":\"Mail\"}}]}]}",
        "{\"version\":1,\"spaces\":[{\"match\":{\"index\":2},\"scaffold\":[{\"pattern\":\"Mail\",\"title\":\"x\"}]}]}",
        "{\"version\":1,\"spaces\":[{\"match\":{\"index\":2},\"scaffold\":[{\"pattern\":\"[\"}]}]}"
    };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(*invalid); ++i) {
        memset(&error, 0, sizeof(error));
        TEST_CHECK(declarative_config_parse_bytes((const uint8_t *) invalid[i], strlen(invalid[i]), "scaffold", &model, &error), false);
        TEST_CHECK(model == NULL, true);
    }
})
