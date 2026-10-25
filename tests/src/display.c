struct display_visible_frame_fixture
{
    CGRect bounds;
    CGRect appkit_frame;
    CGRect appkit_visible_frame;
    uint32_t main_did;
    bool menu_hidden;
    int notch_height;
    CGRect menu;
    bool dock_hidden;
    uint32_t dock_did;
    int dock_orientation;
    CGRect dock;
};

static CGRect display_visible_frame_test_bounds(uint32_t did, void *context)
{
    (void) did;
    return ((struct display_visible_frame_fixture *) context)->bounds;
}

static CGRect display_visible_frame_test_visible_frame(uint32_t did, CGRect display_bounds, void *context)
{
    (void) did;
    struct display_visible_frame_fixture *fixture = context;
    return workspace_display_visible_frame_from_bounds(display_bounds, fixture->appkit_frame, fixture->appkit_visible_frame);
}

static uint32_t display_visible_frame_test_main(void *context) { return ((struct display_visible_frame_fixture *) context)->main_did; }
static bool display_visible_frame_test_menu_hidden(void *context) { return ((struct display_visible_frame_fixture *) context)->menu_hidden; }
static int display_visible_frame_test_notch(uint32_t did, void *context) { (void) did; return ((struct display_visible_frame_fixture *) context)->notch_height; }
static CGRect display_visible_frame_test_menu(uint32_t did, void *context) { (void) did; return ((struct display_visible_frame_fixture *) context)->menu; }
static bool display_visible_frame_test_dock_hidden(void *context) { return ((struct display_visible_frame_fixture *) context)->dock_hidden; }
static uint32_t display_visible_frame_test_dock_display(void *context) { return ((struct display_visible_frame_fixture *) context)->dock_did; }
static int display_visible_frame_test_dock_orientation(void *context) { return ((struct display_visible_frame_fixture *) context)->dock_orientation; }
static CGRect display_visible_frame_test_dock(void *context) { return ((struct display_visible_frame_fixture *) context)->dock; }

static void display_visible_frame_test_install(struct display_visible_frame_fixture *fixture)
{
    display_bounds_test_ops = (struct display_bounds_platform_ops) {
        .display_bounds = display_visible_frame_test_bounds,
        .visible_frame = display_visible_frame_test_visible_frame,
        .main_display_id = display_visible_frame_test_main,
        .menu_bar_hidden = display_visible_frame_test_menu_hidden,
        .notch_height = display_visible_frame_test_notch,
        .menu_bar_rect = display_visible_frame_test_menu,
        .dock_hidden = display_visible_frame_test_dock_hidden,
        .dock_display_id = display_visible_frame_test_dock_display,
        .dock_orientation = display_visible_frame_test_dock_orientation,
        .dock_rect = display_visible_frame_test_dock,
        .context = fixture
    };
}

static struct display_visible_frame_fixture display_visible_frame_test_fixture(void)
{
    return (struct display_visible_frame_fixture) {
        .bounds = CGRectMake(100, 200, 1000, 800),
        .appkit_frame = CGRectMake(0, 0, 1000, 800),
        .appkit_visible_frame = CGRectMake(80, 0, 920, 775),
        .main_did = 7,
        .menu = CGRectMake(100, 200, 1000, 25),
        .dock_did = 7,
        .dock_orientation = DOCK_ORIENTATION_LEFT,
        .dock = CGRectMake(100, 200, 80, 60)
    };
}

TEST_FUNC(display_animation_query_excludes_bigsur_and_modern_releases,
{
    bool saved_tahoe = _workspace_is_macos_version_tahoe;
    bool saved_sequoia = _workspace_is_macos_version_sequoia;
    bool saved_sonoma = _workspace_is_macos_version_sonoma;
    bool saved_ventura = _workspace_is_macos_version_ventura;
    bool saved_monterey = _workspace_is_macos_version_monterey;
    bool saved_bigsur = _workspace_is_macos_version_bigsur;

    workspace_set_macos_version(11);
    TEST_CHECK(display_manager_animation_query_is_supported(), false);
    workspace_set_macos_version(12);
    TEST_CHECK(display_manager_animation_query_is_supported(), true);
    workspace_set_macos_version(13);
    TEST_CHECK(display_manager_animation_query_is_supported(), true);
    workspace_set_macos_version(14);
    TEST_CHECK(display_manager_animation_query_is_supported(), false);
    workspace_set_macos_version(15);
    TEST_CHECK(display_manager_animation_query_is_supported(), false);
    workspace_set_macos_version(26);
    TEST_CHECK(display_manager_animation_query_is_supported(), false);
    workspace_set_macos_version(27);
    TEST_CHECK(display_manager_animation_query_is_supported(), false);

    _workspace_is_macos_version_tahoe = saved_tahoe;
    _workspace_is_macos_version_sequoia = saved_sequoia;
    _workspace_is_macos_version_sonoma = saved_sonoma;
    _workspace_is_macos_version_ventura = saved_ventura;
    _workspace_is_macos_version_monterey = saved_monterey;
    _workspace_is_macos_version_bigsur = saved_bigsur;
});

TEST_FUNC(display_visible_frame_property_selection_and_serialization_are_explicit,
{
    struct display_manager saved_display_manager = g_display_manager;
    struct display_visible_frame_fixture fixture = display_visible_frame_test_fixture();
    fixture.bounds = CGRectMake(0, 0, 2056, 1329);
    fixture.appkit_frame = CGRectMake(0, 0, 2056, 1329);
    fixture.appkit_visible_frame = CGRectMake(0, 0, 2056, 1290);
    display_visible_frame_test_install(&fixture);
    g_display_manager.mode = EXTERNAL_BAR_ALL;
    g_display_manager.top_padding = 40;
    g_display_manager.bottom_padding = 10;

    CGRect secondary = workspace_display_visible_frame_from_bounds(CGRectMake(2056, -100, 1920, 1080),
                                                                    CGRectMake(-1920, 200, 1920, 1080),
                                                                    CGRectMake(-1920, 200, 1870, 1050));
    TEST_CHECK(CGRectEqualToRect(secondary, CGRectMake(2056, -70, 1870, 1050)), true);

    char properties_text[] = "frame,visible-frame";
    FILE *parse_response = tmpfile();
    TEST_CHECK(parse_response != NULL, true);
    if (parse_response) {
        struct properties properties = parse_properties(parse_response,
                                                        (struct token) { .text = properties_text, .length = (int) strlen(properties_text) },
                                                        display_property_val, display_property_str,
                                                        array_count(display_property_str));
        TEST_CHECK(properties.did_parse, true);
        TEST_CHECK(properties.did_error, false);
        TEST_CHECK(properties.flags, DISPLAY_PROPERTY_FRAME | DISPLAY_PROPERTY_VISIBLE_FRAME);
        fclose(parse_response);
    }

    char *output = NULL;
    size_t output_length = 0;
    FILE *stream = open_memstream(&output, &output_length);
    TEST_CHECK(stream != NULL, true);
    if (stream) {
        display_serialize(stream, 7, DISPLAY_PROPERTY_FRAME);
        fclose(stream);
        TEST_CHECK(strcmp(output,
                          "{\n\t\"frame\":{\n\t\t\"x\":0.0000,\n\t\t\"y\":0.0000,\n\t\t\"w\":2056.0000,\n\t\t\"h\":1329.0000\n\t}\n}"), 0);
        TEST_CHECK(strstr(output, "visible-frame") == NULL, true);
        free(output);
    }

    output = NULL;
    output_length = 0;
    stream = open_memstream(&output, &output_length);
    TEST_CHECK(stream != NULL, true);
    if (stream) {
        display_serialize(stream, 7, DISPLAY_PROPERTY_VISIBLE_FRAME);
        fclose(stream);
        TEST_CHECK(strcmp(output,
                          "{\n\t\"visible-frame\":{\n\t\t\"x\":0.0000,\n\t\t\"y\":39.0000,\n\t\t\"w\":2056.0000,\n\t\t\"h\":1290.0000\n\t}\n}"), 0);
        TEST_CHECK(strstr(output, "\t\"frame\":") == NULL, true);
        free(output);
    }

    display_bounds_test_ops = (struct display_bounds_platform_ops) {0};
    g_display_manager = saved_display_manager;
});

TEST_FUNC(display_internal_constrained_bounds_retain_menu_bar_placement_adjustment,
{
    struct display_manager saved_display_manager = g_display_manager;
    struct display_visible_frame_fixture fixture = display_visible_frame_test_fixture();
    fixture.bounds = CGRectMake(0, 0, 2056, 1329);
    fixture.menu = CGRectMake(0, 0, 2056, 40);
    fixture.dock_hidden = true;
    display_visible_frame_test_install(&fixture);
    g_display_manager.mode = EXTERNAL_BAR_ALL;
    g_display_manager.top_padding = 30;
    g_display_manager.bottom_padding = 10;

    CGRect frame = display_bounds_constrained(7, true);
    TEST_CHECK(CGRectEqualToRect(frame, CGRectMake(0, 40, 2056, 1289)), true);

    display_bounds_test_ops = (struct display_bounds_platform_ops) {0};
    g_display_manager = saved_display_manager;
});

TEST_FUNC(display_internal_constrained_bounds_cover_system_ui_and_external_bar,
{
    struct display_manager saved_display_manager = g_display_manager;
    struct display_visible_frame_fixture fixture = display_visible_frame_test_fixture();
    display_visible_frame_test_install(&fixture);
    g_display_manager.mode = EXTERNAL_BAR_ALL;
    g_display_manager.top_padding = 40;
    g_display_manager.bottom_padding = 10;

    CGRect frame = display_bounds_constrained(7, true);
    TEST_CHECK(CGRectEqualToRect(frame, CGRectMake(180, 225, 920, 775)), true);

    fixture.dock_orientation = DOCK_ORIENTATION_RIGHT;
    frame = display_bounds_constrained(7, true);
    TEST_CHECK(CGRectEqualToRect(frame, CGRectMake(100, 225, 920, 775)), true);

    fixture.dock_orientation = DOCK_ORIENTATION_BOTTOM;
    frame = display_bounds_constrained(7, true);
    TEST_CHECK(CGRectEqualToRect(frame, CGRectMake(100, 225, 1000, 715)), true);

    fixture.dock_hidden = true;
    frame = display_bounds_constrained(7, true);
    TEST_CHECK(CGRectEqualToRect(frame, CGRectMake(100, 225, 1000, 775)), true);

    fixture.menu_hidden = true;
    fixture.notch_height = 30;
    frame = display_bounds_constrained(7, true);
    TEST_CHECK(CGRectEqualToRect(frame, CGRectMake(100, 230, 1000, 770)), true);

    fixture.dock_hidden = false;
    fixture.dock_did = 8;
    frame = display_bounds_constrained(7, true);
    TEST_CHECK(CGRectEqualToRect(frame, CGRectMake(100, 230, 1000, 770)), true);

    fixture.menu_hidden = false;
    fixture.dock_hidden = true;
    fixture.dock_did = 7;
    CGRect visible_frame = display_bounds_constrained(7, true);
    CGRect external_bar_frame = display_bounds_constrained(7, false);
    TEST_CHECK(CGRectEqualToRect(visible_frame, CGRectMake(100, 225, 1000, 775)), true);
    TEST_CHECK(CGRectEqualToRect(external_bar_frame, CGRectMake(100, 265, 1000, 725)), true);

    display_bounds_test_ops = (struct display_bounds_platform_ops) {0};
    g_display_manager = saved_display_manager;
});
