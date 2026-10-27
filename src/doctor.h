#ifndef DOCTOR_H
#define DOCTOR_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>

#include "window_management_bridge.h"

#define DOCTOR_CSR_ALLOW_UNRESTRICTED_FS 0x02
#define DOCTOR_CSR_ALLOW_TASK_FOR_PID    0x04
#define DOCTOR_REQUIRED_SIP (DOCTOR_CSR_ALLOW_UNRESTRICTED_FS | DOCTOR_CSR_ALLOW_TASK_FOR_PID)

enum doctor_probe {
    DOCTOR_PROBE_UNKNOWN,
    DOCTOR_PROBE_NO,
    DOCTOR_PROBE_YES
};

enum doctor_service_plist {
    DOCTOR_SERVICE_UNKNOWN,
    DOCTOR_SERVICE_ABSENT,
    DOCTOR_SERVICE_VALID,
    DOCTOR_SERVICE_VALID_SYMLINK,
    DOCTOR_SERVICE_INVALID,
    DOCTOR_SERVICE_WRONG_LABEL
};

enum doctor_sa_install {
    DOCTOR_SA_UNKNOWN,
    DOCTOR_SA_ABSENT,
    DOCTOR_SA_CURRENT,
    DOCTOR_SA_OUTDATED
};

enum doctor_sa_runtime {
    DOCTOR_SA_RUNTIME_UNKNOWN,
    DOCTOR_SA_RUNTIME_CURRENT,
    DOCTOR_SA_RUNTIME_OUTDATED,
    DOCTOR_SA_RUNTIME_INCOMPLETE,
    DOCTOR_SA_RUNTIME_UNREACHABLE
};

struct doctor_facts {
    enum doctor_probe identity;
    enum doctor_probe binary_executable;
    enum doctor_probe translated;
    enum doctor_probe arm64_slice;
    enum doctor_probe accessibility;
    enum doctor_probe sip_available;
    uint32_t sip_config;
    enum doctor_probe arm64e_applicable;
    enum doctor_probe arm64e_bootarg;
    enum doctor_service_plist service_plist;
    enum doctor_probe service_runtime;
    enum doctor_probe legacy_service_collision;
    enum doctor_probe config_present;
    enum doctor_probe config_readable;
    enum doctor_probe screen_capture_required;
    enum doctor_probe screen_capture_access;
    struct wm_bridge_diagnostic window_management;
    enum doctor_sa_install sa_install;
    enum doctor_probe sa_socket;
    enum doctor_sa_runtime sa_runtime;
    const char *runtime_arch;
};

int doctor_report(const struct doctor_facts *facts, FILE *stream);
int doctor_run(void);

#endif
