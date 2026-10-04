#ifndef DECLARATIVE_RUNTIME_TYPES_H
#define DECLARATIVE_RUNTIME_TYPES_H

#include <stdbool.h>
#include <stdint.h>

enum declarative_setting_source
{
    DECLARATIVE_SETTING_BUILTIN,
    DECLARATIVE_SETTING_NATIVE_GLOBAL,
    DECLARATIVE_SETTING_NATIVE_SPACE,
    DECLARATIVE_SETTING_RUNTIME_GLOBAL,
    DECLARATIVE_SETTING_RUNTIME_SPACE
};

enum declarative_runtime_space_field
{
    DECLARATIVE_RUNTIME_SPACE_LAYOUT,
    DECLARATIVE_RUNTIME_SPACE_TOP_PADDING,
    DECLARATIVE_RUNTIME_SPACE_BOTTOM_PADDING,
    DECLARATIVE_RUNTIME_SPACE_LEFT_PADDING,
    DECLARATIVE_RUNTIME_SPACE_RIGHT_PADDING,
    DECLARATIVE_RUNTIME_SPACE_WINDOW_GAP,
    DECLARATIVE_RUNTIME_SPACE_ACCORDION_PADDING,
    DECLARATIVE_RUNTIME_SPACE_SPLIT_RATIO,
    DECLARATIVE_RUNTIME_SPACE_SPLIT_TYPE,
    DECLARATIVE_RUNTIME_SPACE_AUTO_BALANCE,
    DECLARATIVE_RUNTIME_SPACE_FIELD_COUNT
};

struct declarative_runtime_external_bar
{
    int mode;
    int top_padding;
    int bottom_padding;
};

union declarative_runtime_scalar
{
    bool boolean;
    int integer;
    uint32_t u32;
    float real;
    struct declarative_runtime_external_bar external_bar;
};

struct declarative_runtime_cell
{
    bool present;
    enum declarative_setting_source source;
    union declarative_runtime_scalar value;
};

#endif
