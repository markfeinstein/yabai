static struct ws_window_classification_input ws_classifier_test_base(void)
{
    return (struct ws_window_classification_input) {
        .valid_fields = WINDOW_SERVER_WINDOW_FIELD_BASE,
        .tags = WINDOW_SERVER_TAG_DOCUMENT,
        .attributes = 0,
        .parent_id = 0,
        .pid = 100,
        .level = 0,
        .normal_level = 0,
        .floating_level = 3,
        .modal_level = 8,
        .frame = CGRectMake(10, 20, 800, 600),
        .owner = WS_OWNER_REGULAR,
        .ax = WS_AX_STANDARD_WINDOW,
        .tag_layout = WS_TAG_LAYOUT_SAMPLED
    };
}

TEST_FUNC(window_server_classifier_ordered_decision_table_is_conservative,
{
    struct ws_window_classification_input input = ws_classifier_test_base();
    struct ws_window_classification classification = ws_classify_window_candidate(&input);
    TEST_CHECK(classification.window_class, WS_WINDOW_CLASS_MANAGEABLE);
    TEST_CHECK(classification.reasons & WS_REASON_AX_CORROBORATED, WS_REASON_AX_CORROBORATED);

    input = ws_classifier_test_base(); input.tracked = true; input.valid_fields = 0;
    classification = ws_classify_window_candidate(&input);
    TEST_CHECK(classification.window_class, WS_WINDOW_CLASS_MANAGEABLE);
    TEST_CHECK(classification.reasons & WS_REASON_TRACKED_PRESERVED, WS_REASON_TRACKED_PRESERVED);

    input = ws_classifier_test_base(); input.valid_fields = WINDOW_SERVER_WINDOW_FIELD_ID;
    classification = ws_classify_window_candidate(&input);
    TEST_CHECK(classification.window_class, WS_WINDOW_CLASS_UNCERTAIN);
    TEST_CHECK(classification.reasons & WS_REASON_MISSING_BASE_METADATA, WS_REASON_MISSING_BASE_METADATA);

    input = ws_classifier_test_base(); input.tag_layout = WS_TAG_LAYOUT_UNSUPPORTED;
    classification = ws_classify_window_candidate(&input);
    TEST_CHECK(classification.window_class, WS_WINDOW_CLASS_UNCERTAIN);
    TEST_CHECK(classification.reasons & WS_REASON_UNSUPPORTED_TAG_LAYOUT, WS_REASON_UNSUPPORTED_TAG_LAYOUT);

    input = ws_classifier_test_base(); input.tags |= WINDOW_SERVER_TAG_MENU_BAR;
    classification = ws_classify_window_candidate(&input);
    TEST_CHECK(classification.window_class, WS_WINDOW_CLASS_SYSTEM_OVERLAY);

    input = ws_classifier_test_base(); input.parent_id = 9;
    classification = ws_classify_window_candidate(&input);
    TEST_CHECK(classification.window_class, WS_WINDOW_CLASS_USER_FACING_NONMANAGEABLE);
    input.ax = WS_AX_UNAVAILABLE;
    TEST_CHECK(ws_classify_window_candidate(&input).window_class, WS_WINDOW_CLASS_UNCERTAIN);

    input = ws_classifier_test_base(); input.level = 17;
    classification = ws_classify_window_candidate(&input);
    TEST_CHECK(classification.window_class, WS_WINDOW_CLASS_USER_FACING_NONMANAGEABLE);
    input.ax = WS_AX_UNAVAILABLE;
    TEST_CHECK(ws_classify_window_candidate(&input).window_class, WS_WINDOW_CLASS_UNCERTAIN);

    input = ws_classifier_test_base(); input.tags |= WINDOW_SERVER_TAG_MINIATURIZED;
    classification = ws_classify_window_candidate(&input);
    TEST_CHECK(classification.window_class, WS_WINDOW_CLASS_USER_FACING_NONMANAGEABLE);
    input.include_minimized = true;
    TEST_CHECK(ws_classify_window_candidate(&input).window_class, WS_WINDOW_CLASS_MANAGEABLE);

    input = ws_classifier_test_base(); input.tags = 0;
    classification = ws_classify_window_candidate(&input);
    TEST_CHECK(classification.window_class, WS_WINDOW_CLASS_UNCERTAIN);
    TEST_CHECK(classification.reasons & WS_REASON_MISSING_SEMANTIC_TAG, WS_REASON_MISSING_SEMANTIC_TAG);

    input = ws_classifier_test_base(); input.ax = WS_AX_OTHER;
    classification = ws_classify_window_candidate(&input);
    TEST_CHECK(classification.window_class, WS_WINDOW_CLASS_USER_FACING_NONMANAGEABLE);

    input = ws_classifier_test_base(); input.ax = WS_AX_NO_MATCH;
    classification = ws_classify_window_candidate(&input);
    TEST_CHECK(classification.window_class, WS_WINDOW_CLASS_UNCERTAIN);

    input = ws_classifier_test_base(); input.owner = WS_OWNER_PROHIBITED;
    classification = ws_classify_window_candidate(&input);
    TEST_CHECK(classification.window_class, WS_WINDOW_CLASS_USER_FACING_NONMANAGEABLE);

    input = ws_classifier_test_base(); input.ax = WS_AX_UNAVAILABLE;
    classification = ws_classify_window_candidate(&input);
    TEST_CHECK(classification.window_class, WS_WINDOW_CLASS_MANAGEABLE);
    TEST_CHECK(classification.reasons & WS_REASON_NO_AX_DOCUMENT, WS_REASON_NO_AX_DOCUMENT);

    input = ws_classifier_test_base(); input.ax = WS_AX_UNAVAILABLE;
    input.tags = WINDOW_SERVER_TAG_FLOATING; input.level = input.floating_level;
    classification = ws_classify_window_candidate(&input);
    TEST_CHECK(classification.window_class, WS_WINDOW_CLASS_UNCERTAIN);
    TEST_CHECK(classification.reasons & WS_REASON_FLOATING_WITHOUT_AX, WS_REASON_FLOATING_WITHOUT_AX);

    input = ws_classifier_test_base(); input.ax = WS_AX_UNAVAILABLE; input.frame.size.width = 0;
    classification = ws_classify_window_candidate(&input);
    TEST_CHECK(classification.window_class, WS_WINDOW_CLASS_UNCERTAIN);
    TEST_CHECK(classification.reasons & WS_REASON_INVALID_FRAME, WS_REASON_INVALID_FRAME);

    input = ws_classifier_test_base(); input.ax = WS_AX_UNAVAILABLE; input.owner = WS_OWNER_UNKNOWN;
    classification = ws_classify_window_candidate(&input);
    TEST_CHECK(classification.window_class, WS_WINDOW_CLASS_UNCERTAIN);
    TEST_CHECK(classification.reasons & WS_REASON_OWNER_NOT_REGULAR, WS_REASON_OWNER_NOT_REGULAR);
});

TEST_FUNC(window_server_classifier_attribute_and_bit58_are_diagnostic_only,
{
    struct ws_window_classification_input base = ws_classifier_test_base();
    base.ax = WS_AX_UNAVAILABLE;
    struct ws_window_classification baseline = ws_classify_window_candidate(&base);

    struct ws_window_classification_input attribute = base;
    attribute.attributes |= UINT64_C(0x2);
    struct ws_window_classification attribute_result = ws_classify_window_candidate(&attribute);
    TEST_CHECK(attribute_result.window_class, baseline.window_class);
    TEST_CHECK(attribute_result.reasons & WS_REASON_ATTRIBUTE_BIT_2, WS_REASON_ATTRIBUTE_BIT_2);

    struct ws_window_classification_input bit58 = base;
    bit58.tags |= WINDOW_SERVER_TAG_IGNORE_FOR_SCREEN_SHARING;
    struct ws_window_classification bit58_result = ws_classify_window_candidate(&bit58);
    TEST_CHECK(bit58_result.window_class, baseline.window_class);
    TEST_CHECK(bit58_result.reasons & WS_REASON_IGNORE_SCREEN_SHARING,
               WS_REASON_IGNORE_SCREEN_SHARING);

    struct window_server_window_record legacy_record = {
        .id = 1, .parent_id = 0, .level = 0,
        .tags = WINDOW_SERVER_TAG_DOCUMENT,
        .attributes = 0,
        .valid_fields = WINDOW_SERVER_WINDOW_FIELD_BASE
    };
    struct ws_legacy_eligibility_input legacy = { .record = &legacy_record };
    TEST_CHECK(ws_legacy_window_is_eligible(&legacy), false);
    legacy_record.attributes = 2;
    TEST_CHECK(ws_legacy_window_is_eligible(&legacy), true);
    legacy_record.attributes = 0;
    legacy_record.tags |= WINDOW_SERVER_TAG_IGNORE_FOR_SCREEN_SHARING;
    TEST_CHECK(ws_legacy_window_is_eligible(&legacy), true);
});

TEST_FUNC(window_server_classifier_names_and_reasons_are_stable,
{
    TEST_CHECK(strcmp(ws_window_class_name(WS_WINDOW_CLASS_MANAGEABLE), "manageable"), 0);
    TEST_CHECK(strcmp(ws_window_class_name(WS_WINDOW_CLASS_USER_FACING_NONMANAGEABLE),
                      "user-facing-nonmanageable"), 0);
    TEST_CHECK(strcmp(ws_window_class_name(WS_WINDOW_CLASS_SYSTEM_OVERLAY), "system-overlay"), 0);
    TEST_CHECK(strcmp(ws_window_class_name(WS_WINDOW_CLASS_UNCERTAIN), "uncertain"), 0);
    uint64_t combined = 0;
    for (size_t i = 0; i < ws_window_classification_reason_descriptor_count; ++i) {
        TEST_CHECK(combined & ws_window_classification_reason_descriptors[i].mask, 0ULL);
        if (i) TEST_CHECK(ws_window_classification_reason_descriptors[i - 1].mask <
                          ws_window_classification_reason_descriptors[i].mask, true);
        TEST_CHECK(ws_window_classification_reason_descriptors[i].name[0] != '\0', true);
        combined |= ws_window_classification_reason_descriptors[i].mask;
    }
    TEST_CHECK(combined != 0, true);
});
