TEST_FUNC(view_find_min_depth_leaf_node_handles_complete_tree_boundary,
{
    struct window_node nodes[257] = {0};

    for (int i = 0; i < 128; ++i) {
        nodes[i].left = &nodes[2 * i + 1];
        nodes[i].right = &nodes[2 * i + 2];
        nodes[i].left->parent = &nodes[i];
        nodes[i].right->parent = &nodes[i];
    }

    TEST_CHECK(view_find_min_depth_leaf_node(NULL) == NULL, true);
    TEST_CHECK(view_find_min_depth_leaf_node(nodes) == &nodes[128], true);
});
