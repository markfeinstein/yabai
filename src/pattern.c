#include "pattern.h"

#include <ctype.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>

// regcomp expands bounded repetition and connects the positions that can
// follow one another. A short pattern can therefore require quadratic or
// multiplicative memory. Keep enough budget for any literal accepted by the
// daemon protocol, while refusing structures that expand far beyond it.
struct pattern_cost
{
    double work;
    double transitions;
    double first;
    double last;
    bool nullable;
};

struct pattern_frame
{
    struct pattern_cost alternatives;
    struct pattern_cost branch;
    bool has_alternatives;
    bool has_branch;
};

static double pattern_cost_total(struct pattern_cost cost)
{
    return cost.work + cost.transitions;
}

static struct pattern_cost pattern_cost_position(void)
{
    return (struct pattern_cost) { .work = 3, .first = 1, .last = 1 };
}

static struct pattern_cost pattern_cost_empty(void)
{
    return (struct pattern_cost) { .work = 1, .nullable = true };
}

static struct pattern_cost pattern_cost_node(struct pattern_cost cost)
{
    cost.work += 1 + cost.first + cost.last;
    return cost;
}

static struct pattern_cost pattern_cost_concat(struct pattern_cost a, struct pattern_cost b)
{
    return pattern_cost_node((struct pattern_cost) {
        .work = a.work + b.work,
        .transitions = a.transitions + b.transitions + a.last * b.first,
        .first = a.first + (a.nullable ? b.first : 0),
        .last = b.last + (b.nullable ? a.last : 0),
        .nullable = a.nullable && b.nullable
    });
}

static struct pattern_cost pattern_cost_union(struct pattern_cost a, struct pattern_cost b)
{
    return pattern_cost_node((struct pattern_cost) {
        .work = a.work + b.work,
        .transitions = a.transitions + b.transitions,
        .first = a.first + b.first,
        .last = a.last + b.last,
        .nullable = a.nullable || b.nullable
    });
}

static struct pattern_cost pattern_cost_repeat(struct pattern_cost atom, int min, int max)
{
    if (min <= 1 && max <= 1) {
        if (max == -1) atom.transitions += atom.last * atom.first;
        atom.nullable = atom.nullable || min == 0;
        return pattern_cost_node(atom);
    }

    if (max != -1 && max < min) max = min;

    struct pattern_cost result = pattern_cost_empty();
    for (int i = 0; i < min && pattern_cost_total(result) <= PATTERN_COST_LIMIT; ++i) {
        result = i ? pattern_cost_concat(result, atom) : atom;
    }

    if (max == -1) {
        result = pattern_cost_concat(result, pattern_cost_repeat(atom, 0, -1));
    } else if (max > min) {
        struct pattern_cost optional = pattern_cost_union(pattern_cost_empty(), atom);
        for (int i = min + 1; i < max && pattern_cost_total(optional) <= PATTERN_COST_LIMIT; ++i) {
            optional = pattern_cost_union(pattern_cost_empty(), pattern_cost_concat(atom, optional));
        }
        result = min ? pattern_cost_concat(result, optional) : optional;
    }

    return result;
}

static int pattern_parse_count(const char **cursor)
{
    int count = 0;
    while (isdigit((unsigned char) **cursor)) {
        if (count <= RE_DUP_MAX) count = count * 10 + (**cursor - '0');
        ++*cursor;
    }
    return count > RE_DUP_MAX ? RE_DUP_MAX + 1 : count;
}

static bool pattern_parse_bound(const char **cursor, int *min, int *max)
{
    const char *c = *cursor;
    if (c[0] != '{' || !isdigit((unsigned char) c[1])) return false;

    ++c;
    *min = pattern_parse_count(&c);
    *max = *min;
    if (*c == ',') {
        ++c;
        *max = isdigit((unsigned char) *c) ? pattern_parse_count(&c) : -1;
    }
    if (*c != '}') return false;

    *cursor = c + 1;
    return true;
}

static const char *pattern_skip_bracket(const char *cursor)
{
    if (*cursor == '^') ++cursor;
    if (*cursor == ']') ++cursor;

    while (*cursor && *cursor != ']') {
        if (cursor[0] == '[' && (cursor[1] == ':' || cursor[1] == '.' || cursor[1] == '=')) {
            char delimiter = cursor[1];
            cursor += 2;
            while (*cursor && !(cursor[0] == delimiter && cursor[1] == ']')) ++cursor;
            if (*cursor) cursor += 2;
        } else {
            ++cursor;
        }
    }
    return *cursor ? cursor + 1 : cursor;
}

static void pattern_frame_alternate(struct pattern_frame *frame)
{
    struct pattern_cost branch = frame->has_branch ? frame->branch : pattern_cost_empty();
    frame->alternatives = frame->has_alternatives ? pattern_cost_union(frame->alternatives, branch) : branch;
    frame->has_alternatives = true;
    frame->has_branch = false;
}

static void pattern_frame_append(struct pattern_frame *frame, struct pattern_cost atom)
{
    frame->branch = frame->has_branch ? pattern_cost_concat(frame->branch, atom) : atom;
    frame->has_branch = true;
}

double pattern_cost(const char *pattern)
{
    if (!pattern) return INFINITY;

    struct pattern_frame frames[PATTERN_MAX_DEPTH + 1] = {0};
    int depth = 0;

    for (const char *cursor = pattern; *cursor;) {
        struct pattern_cost atom;
        char c = *cursor++;

        if (c == '(') {
            if (depth == PATTERN_MAX_DEPTH) return INFINITY;
            frames[++depth] = (struct pattern_frame) {0};
            continue;
        } else if (c == '|') {
            pattern_frame_alternate(&frames[depth]);
            continue;
        } else if (c == ')' && depth > 0) {
            pattern_frame_alternate(&frames[depth]);
            atom = frames[depth--].alternatives;
        } else if (c == '[') {
            cursor = pattern_skip_bracket(cursor);
            atom = pattern_cost_position();
        } else {
            if (c == '\\' && *cursor) ++cursor;
            atom = pattern_cost_position();
        }

        int min, max;
        if (*cursor == '*') {
            ++cursor;
            atom = pattern_cost_repeat(atom, 0, -1);
        } else if (*cursor == '+') {
            ++cursor;
            atom = pattern_cost_repeat(atom, 1, -1);
        } else if (*cursor == '?') {
            ++cursor;
            atom = pattern_cost_repeat(atom, 0, 1);
        } else if (pattern_parse_bound(&cursor, &min, &max)) {
            atom = pattern_cost_repeat(atom, min, max);
        }

        pattern_frame_append(&frames[depth], atom);
        if (pattern_cost_total(frames[depth].branch) > PATTERN_COST_LIMIT) return INFINITY;
    }

    while (depth > 0) {
        pattern_frame_alternate(&frames[depth]);
        pattern_frame_append(&frames[depth - 1], frames[depth].alternatives);
        --depth;
    }

    pattern_frame_alternate(&frames[0]);
    return pattern_cost_total(frames[0].alternatives);
}

enum pattern_compile_result pattern_compile(regex_t *regex, const char *pattern)
{
    if (!regex || !pattern) return PATTERN_COMPILE_INVALID;
    if (pattern_cost(pattern) > PATTERN_COST_LIMIT) return PATTERN_COMPILE_TOO_COMPLEX;
    return regcomp(regex, pattern, REG_EXTENDED | REG_NOSUB) == 0
         ? PATTERN_COMPILE_OK
         : PATTERN_COMPILE_INVALID;
}
