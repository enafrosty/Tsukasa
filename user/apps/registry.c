#include "registry.h"

#include "../../loader/exec.h"

/* CLI command entrypoints. */
void app_cmd_help_entry(void);
void app_cmd_echo_entry(void);
void app_cmd_pwd_entry(void);
void app_cmd_date_entry(void);
void app_cmd_sort_entry(void);
void app_cmd_sysfetch_entry(void);
void app_cmd_man_entry(void);
void app_cmd_grep_entry(void);
void app_cmd_cat_entry(void);
void app_cmd_net_entry(void);
void app_cmd_ping_entry(void);
void app_cmd_telnet_entry(void);
void app_cmd_abi_test_entry(void);
void app_gui_phase2_runtime_test_entry(void);
void app_gui_phase2_isolation_helper_entry(void);
void app_shell_init_entry(void);

/* GUI entrypoints. */
void app_terminal_gui_entry(void);
void app_filemgr_gui_entry(void);
void app_notepad_gui_entry(void);
void app_settings_gui_entry(void);
void app_calc_gui_entry(void);
void app_diagnostics_gui_entry(void);
void app_network_gui_entry(void);
void app_about_gui_entry(void);

/* Sample app from Phase 6. */
void hello_gui_main(void);

void user_apps_register_all(void)
{
    exec_register_builtin("/bin/help", app_cmd_help_entry);
    exec_register_builtin("/bin/echo", app_cmd_echo_entry);
    exec_register_builtin("/bin/pwd", app_cmd_pwd_entry);
    exec_register_builtin("/bin/date", app_cmd_date_entry);
    exec_register_builtin("/bin/sort", app_cmd_sort_entry);
    exec_register_builtin("/bin/sysfetch", app_cmd_sysfetch_entry);
    exec_register_builtin("/bin/man", app_cmd_man_entry);
    exec_register_builtin("/bin/grep", app_cmd_grep_entry);
    exec_register_builtin("/bin/cat", app_cmd_cat_entry);
    exec_register_builtin("/bin/net", app_cmd_net_entry);
    exec_register_builtin("/bin/ping", app_cmd_ping_entry);
    exec_register_builtin("/bin/telnet", app_cmd_telnet_entry);
    exec_register_builtin("/bin/abi-test", app_cmd_abi_test_entry);
    exec_register_builtin("/bin/gui-phase2-runtime-test", app_gui_phase2_runtime_test_entry);
    exec_register_builtin("/bin/gui-phase2-isolation-helper", app_gui_phase2_isolation_helper_entry);
    exec_register_builtin("/bin/shinit", app_shell_init_entry);

    exec_register_builtin("/apps/terminal", app_terminal_gui_entry);
    exec_register_builtin("/apps/filemgr", app_filemgr_gui_entry);
    exec_register_builtin("/apps/notepad", app_notepad_gui_entry);
    exec_register_builtin("/apps/settings", app_settings_gui_entry);
    exec_register_builtin("/apps/calc", app_calc_gui_entry);
    exec_register_builtin("/apps/diagnostics", app_diagnostics_gui_entry);
    exec_register_builtin("/apps/network", app_network_gui_entry);
    exec_register_builtin("/apps/about", app_about_gui_entry);

    exec_register_builtin("/examples/hello-gui", hello_gui_main);
}
