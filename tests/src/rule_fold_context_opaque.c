#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <regex.h>
#include "../../src/rule.h"

static const struct rule_fold_context *opaque_context;

#ifdef FORGE_RULE_FOLD_CONTEXT
static struct rule_fold_context forged_context;
#endif

int main(void)
{
    return opaque_context != NULL;
}
