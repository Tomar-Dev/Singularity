// system/shell/shell.hpp
#ifndef SHELL_HPP
#define SHELL_HPP

#include <stdint.h>

class Shell {
private:
    static const int CMD_BUF_SIZE = 256;
    char cmdBuffer[CMD_BUF_SIZE];
    int cmdLen;
    
    char currentPath[256];
    
    bool isScriptMode;

    void processCommand();
    void processCommand(const char* cmdStr);
    
    int dispatchCommand(const char* cmd, const char* arg);
    
    void printPrompt();
    void resolveAbsolutePath(const char* input, char* output);
    
    void logStartup(const char* msg, int status); 

    struct ShellCommand {
        const char* name;
        int (Shell::*handler)(const char*);
        const char* description;
    };
    
    static const ShellCommand command_table[];
    static const int command_count;

public:
    Shell();
    void init();
    void runStartup();
    void showWelcome();
    
    void update(); 
    void onKeyDown(char c);
    
    void executeScript(const char* path);

    // --- System & Core Commands ---
    int cmd_help(const char* arg);
    int cmd_clear(const char* arg);
    int cmd_echo(const char* arg);
    int cmd_log(const char* arg);
    int cmd_system(const char* arg);
    int cmd_systemcheck(const char* arg);
    int cmd_taskmgr(const char* arg);
    int cmd_numa(const char* arg);
    int cmd_reboot(const char* arg);
    int cmd_shutdown(const char* arg);
    int cmd_suspend(const char* arg);
    int cmd_power(const char* arg);
    int cmd_turbo(const char* arg);
    int cmd_beep(const char* arg);
    int cmd_usb(const char* arg);
    int cmd_i2c(const char* arg);
    int cmd_fbtest(const char* arg);
    int cmd_gui(const char* arg);
    int cmd_crash_stack(const char* arg);
    int cmd_wdt_test(const char* arg);
    int cmd_torture(const char* arg);
    int cmd_profile(const char* arg);
    int cmd_reap(const char* arg);
    int cmd_pcie(const char* arg);

    // --- Filesystem & Storage Commands ---
    int cmd_ls(const char* arg);
    int cmd_cat(const char* arg);
    int cmd_cd(const char* arg);
    int cmd_mkdir(const char* arg);
    int cmd_touch(const char* arg);
    int cmd_write(const char* arg);
    int cmd_mount(const char* arg);
    int cmd_ramdisk(const char* arg);
    int cmd_automount(const char* arg);
    int cmd_mkfs(const char* arg);
    int cmd_fdisk(const char* arg);
    int cmd_disks(const char* arg);
};

extern "C" {
    void shell_init_c();
    void shell_run_startup_c();
    void shell_update_c();
}

#endif