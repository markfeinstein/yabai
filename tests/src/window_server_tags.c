TEST_FUNC(window_server_tag_names_masks_and_order_are_exact,
{
    const uint64_t expected_masks[] = {
        UINT64_C(1) << 0, UINT64_C(1) << 1, UINT64_C(1) << 11,
        UINT64_C(1) << 13, UINT64_C(1) << 31, UINT64_C(1) << 32,
        UINT64_C(1) << 39, UINT64_C(1) << 42, UINT64_C(1) << 44,
        UINT64_C(1) << 46, UINT64_C(1) << 49, UINT64_C(1) << 56,
        UINT64_C(1) << 57, UINT64_C(1) << 58, UINT64_C(1) << 60
    };
    const char *expected_names[] = {
        "document", "floating", "on_all_workspaces", "kit_visible", "modal",
        "window_manager_aware", "hidden", "fullscreen", "super_sticky", "menu_bar",
        "desktop_picture", "fullscreen_capable", "fullscreen_tile_capable",
        "ignore_for_screen_sharing", "miniaturized"
    };
    TEST_CHECK(window_server_tag_descriptor_count, array_count(expected_masks));
    uint64_t combined = 0;
    for (size_t i = 0; i < window_server_tag_descriptor_count; ++i) {
        TEST_CHECK(window_server_tag_descriptors[i].mask, expected_masks[i]);
        TEST_CHECK(window_server_tag_descriptors[i].bit,
                   (unsigned) __builtin_ctzll(expected_masks[i]));
        TEST_CHECK(strcmp(window_server_tag_descriptors[i].name, expected_names[i]), 0);
        TEST_CHECK(combined & window_server_tag_descriptors[i].mask, 0ULL);
        if (i) TEST_CHECK(window_server_tag_descriptors[i - 1].bit <
                          window_server_tag_descriptors[i].bit, true);
        combined |= window_server_tag_descriptors[i].mask;
    }
    TEST_CHECK(combined, (uint64_t) WINDOW_SERVER_TAG_KNOWN_MASK);
    TEST_CHECK(WINDOW_SERVER_TAG_DOCUMENT, UINT64_C(0x1));
    TEST_CHECK(WINDOW_SERVER_TAG_FLOATING, UINT64_C(0x2));
    TEST_CHECK(WINDOW_SERVER_TAG_MODAL, UINT64_C(0x80000000));
    TEST_CHECK(WINDOW_SERVER_TAG_FULLSCREEN_CAPABLE, UINT64_C(0x100000000000000));
    TEST_CHECK(WINDOW_SERVER_TAG_FULLSCREEN_TILE_CAPABLE, UINT64_C(0x200000000000000));
    TEST_CHECK(WINDOW_SERVER_TAG_IGNORE_FOR_SCREEN_SHARING, UINT64_C(0x400000000000000));
    TEST_CHECK(WINDOW_SERVER_TAG_MINIATURIZED, UINT64_C(0x1000000000000000));
});

TEST_FUNC(window_server_tag_known_mask_preserves_unknown_bits,
{
    uint64_t unknown = (UINT64_C(1) << 7) | (UINT64_C(1) << 63);
    uint64_t tags = WINDOW_SERVER_TAG_DOCUMENT | WINDOW_SERVER_TAG_MENU_BAR | unknown;
    TEST_CHECK(tags & WINDOW_SERVER_TAG_KNOWN_MASK,
               WINDOW_SERVER_TAG_DOCUMENT | WINDOW_SERVER_TAG_MENU_BAR);
    TEST_CHECK(tags & ~WINDOW_SERVER_TAG_KNOWN_MASK, unknown);
});
