#include "../../src/menu_bar_model.h"
#include "../../src/focus_follows_mouse.h"
#include "../../src/window_server_snapshot.h"
#include "../../scripts/macho_parser.h"
#include "../../scripts/scan_sa_core.h"

void public_header_contract(void)
{
    struct menu_bar_settings settings = {0};
    struct focus_follows_mouse_state ffm = {0};
    struct window_server_snapshot snapshot = {0};
    struct window_server_window_record record = {0};
    (void) settings;
    (void) ffm;
    (void) snapshot;
    (void) record;
}
