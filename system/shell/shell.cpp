// system/shell/shell.cpp
#include "system/shell/shell.hpp"
#include "libc/stdio.h"
#include "libc/string.h"
#include "drivers/keyboard/keyboard.h"
#include "memory/kheap.h"
#include "drivers/serial/serial.h"
#include "system/ffi/ffi.h"
#include "archs/kom/kom_aal.h"
#include "archs/cpu/cpu_hal.h"
#include "system/process/process.h"
#include "kernel/fastops.h"
#include "drivers/misc/speaker.h"
#include "kernel/config.h"
#include "system/console/console.h"

inline void* operator new(size_t, void* p) { return p; }

extern "C" {
    void console_blink_cursor(bool state);
    void device_print_all(bool detailed);
    void init_pcie_cpp(bool verbose);
    void yield();
    int  is_input_subsystem_ready();
    void stdio_flush();
    void timer_sleep(uint64_t ticks);
}

static Shell* global_shell = nullptr;

Shell::Shell() {
    cmdLen = 0;
    for (int i = 0; i < CMD_BUF_SIZE; i++) { cmdBuffer[i] = 0; }

    currentPath[0] = 'C';
    currentPath[1] = ':';
    currentPath[2] = '\\';
    currentPath[3] = '\0';

    isScriptMode = false;
}

void Shell::init() {}

void Shell::runStartup() {
    console_set_color(CONSOLE_COLOR_YELLOW, CONSOLE_COLOR_BLACK);
    printf("\n>>> STARTUP Sequence <<<\n");

    console_set_color(CONSOLE_COLOR_DARK_GREY, CONSOLE_COLOR_BLACK);
    printf("[ STARTUP ] ");
    console_set_color(CONSOLE_COLOR_WHITE, CONSOLE_COLOR_BLACK);
    printf("System services initializing...\n");

    executeScript("C:\\system\\startup.cfg");

    timer_sleep(10); 
    ffi_logger_flush_sync();

    console_set_color(CONSOLE_COLOR_DARK_GREY, CONSOLE_COLOR_BLACK);
    printf("[ STARTUP ] ");
    console_set_color(CONSOLE_COLOR_WHITE, CONSOLE_COLOR_BLACK);
    printf("System ready. ");
    console_set_color(CONSOLE_COLOR_LIGHT_GREY, CONSOLE_COLOR_BLACK);
    printf("Launching shell environment...\n\n");
    
    ffi_logger_flush_sync();
}

void Shell::showWelcome() {
    console_set_color(CONSOLE_COLOR_LIGHT_GREEN, CONSOLE_COLOR_BLACK);
    printf("%s Shell %s\n", SINGULARITY_SYS_NAME, SINGULARITY_SHELL_VER);

    console_set_color(CONSOLE_COLOR_LIGHT_GREY, CONSOLE_COLOR_BLACK);
    printf("Type \"help\" to see a list of shell commands\n");
    printf("Type \"systemcheck\" to see the system status\n\n");

    console_set_color(CONSOLE_COLOR_WHITE, CONSOLE_COLOR_BLACK);

    stdio_flush();
    printPrompt();
}

void Shell::logStartup(const char* msg, int status) {
    console_set_color(CONSOLE_COLOR_DARK_GREY, CONSOLE_COLOR_BLACK);
    printf("[ STARTUP ] ");

    console_set_color(CONSOLE_COLOR_WHITE, CONSOLE_COLOR_BLACK);
    printf("%-50s", msg);

    if (status == 1) {
        console_set_color(CONSOLE_COLOR_LIGHT_GREEN, CONSOLE_COLOR_BLACK);
        printf("[ OK ]\n");
    } else if (status == 2) {
        console_set_color(CONSOLE_COLOR_LIGHT_RED, CONSOLE_COLOR_BLACK);
        printf("[FAIL]\n");
    } else {
        printf("\n");
    }

    console_set_color(CONSOLE_COLOR_WHITE, CONSOLE_COLOR_BLACK);
}

void Shell::executeScript(const char* path) {
    KObject* obj = ons_resolve(path);
    if (!obj) { return; } else { /* Valid */ }
    if (obj->type != KObjectType::BLOB) { kobject_unref(obj); return; } else { /* Read Stream Activated */ }

    KBlob* blob = (KBlob*)obj;
    uint64_t size = blob->getSize();

    if (size == 0 || size > 65536) { kobject_unref(obj); return; } else { /* Size validated */ }

    char* buf = (char*)kmalloc((size_t)size + 1);
    if (!buf) { kobject_unref(obj); return; } else { /* Allocation secure */ }

    isScriptMode = true;
    size_t read_bytes = 0;
    if (blob->read(0, buf, size, &read_bytes) == KOM_OK && read_bytes > 0) {
        buf[read_bytes] = '\0';
        char* line = buf;
        char* next_line = nullptr;

        while (line && *line) {
            next_line = strpbrk(line, "\r\n");
            if (next_line) {
                *next_line = '\0';
                next_line++;
                while (*next_line == '\r' || *next_line == '\n') { next_line++; }
            } else {
                // Break naturally
            }

            char* trim_start = line;
            while (*trim_start == ' ' || *trim_start == '\t') { trim_start++; }

            if (strlen(trim_start) > 0) {
                if (strncmp(trim_start, "run ", 4) == 0 || strncmp(trim_start, "silent_run ", 11) == 0) {
                    bool totally_silent = (strncmp(trim_start, "silent_run ", 11) == 0);
                    char* p = totally_silent ? trim_start + 11 : trim_start + 4;
                    
                    while (*p == ' ') { p++; }
                    if (*p == '"') {
                        char* desc_start = p + 1;
                        char* desc_end = strchr(desc_start, '"');
                        if (desc_end) {
                            *desc_end = '\0';
                            char* cmd_start = desc_end + 1;
                            while (*cmd_start == ' ') { cmd_start++; }
                            if (*cmd_start) {
                                char cmdTemp[128];
                                strncpy(cmdTemp, cmd_start, 127);
                                cmdTemp[127] = '\0';
                                char* cmd = cmdTemp;
                                char* arg = nullptr;
                                for (int i = 0; cmdTemp[i] != '\0'; i++) {
                                    if (cmdTemp[i] == ' ') {
                                        cmdTemp[i] = '\0';
                                        arg = &cmdTemp[i + 1];
                                        break;
                                    } else {
                                        // Still searching parameters
                                    }
                                }
                                
                                uint8_t cpu_id = get_cpu_id_fast();
                                process_t* curr = current_process[cpu_id];
                                uint32_t old_flags = curr ? curr->flags : 0;
                                
                                if (totally_silent && curr) {
                                    curr->flags |= PROC_FLAG_SILENT;
                                } else {
                                    // Raw process flags retained
                                }

                                int ret = dispatchCommand(cmd, arg);
                                
                                if (totally_silent && curr) {
                                    curr->flags = old_flags; 
                                } else {
                                    // Flags natively retained
                                }
                                
                                if (!totally_silent) {
                                    ffi_logger_flush_sync(); 
                                    logStartup(desc_start, ret == 0 ? 1 : 2);
                                } else {
                                    // Silence upheld
                                }
                            } else {
                                // No command given
                            }
                        } else {
                            // Syntax error
                        }
                    } else {
                        // Syntax error
                    }
                } else {
                    processCommand(trim_start);
                }
            } else {
                // Empty string
            }
            line = next_line;
        }
    } else {
        // I/O read failure on initial target
    }
    isScriptMode = false;
    kfree(buf);
    kobject_unref(obj);
}

void Shell::printPrompt() {
    console_set_color(CONSOLE_COLOR_LIGHT_GREEN, CONSOLE_COLOR_BLACK);
    printf("root@sys ");
    console_set_color(CONSOLE_COLOR_LIGHT_BLUE, CONSOLE_COLOR_BLACK);
    printf("%s", currentPath);
    console_set_color(CONSOLE_COLOR_WHITE, CONSOLE_COLOR_BLACK);
    printf("> ");
    stdio_flush();
}

void Shell::resolveAbsolutePath(const char* input, char* output) {
    if (!input || input[0] == '\0' || strcmp(input, ".") == 0 || strcmp(input, ".\\") == 0) {
        strncpy(output, currentPath, 255);
        output[255] = '\0';
        return;
    } else {
        // Path needs deeper checks
    }

    if (strcmp(input, "..") == 0 || strcmp(input, "..\\") == 0) {
        strncpy(output, currentPath, 255);
        output[255] = '\0';
        char* last_slash = strrchr(output, '\\');
        if (last_slash && last_slash != output) {
            if (last_slash == &output[2]) {
                *(last_slash + 1) = '\0'; 
            } else {
                *last_slash = '\0';
            }
        } else {
            // Reached top level
        }
        return;
    } else {
        // Normal evaluation bounds
    }

    if (((input[0] >= 'A' && input[0] <= 'Z') || (input[0] >= 'a' && input[0] <= 'z')) && input[1] == ':') {
        strncpy(output, input, 255);
        output[255] = '\0';
        for(int i=0; output[i]; i++) { if(output[i] == '/') output[i] = '\\'; }
        if (strlen(output) == 2) { strncat(output, "\\", 2); } else { /* Valid */ }
    } else if (input[0] == '/' || input[0] == '\\') { 
        strncpy(output, input, 255);
        output[255] = '\0';
        for(int i=0; output[i]; i++) { if(output[i] == '/') output[i] = '\\'; }
    } else {
        size_t curLen = strlen(currentPath);
        strncpy(output, currentPath, 255);
        output[255] = '\0';

        if (curLen > 0 && output[curLen - 1] != '\\') {
            strncat(output, "\\", 255 - strlen(output));
        } else {
            // Properly formatted ending
        }
        strncat(output, input, 255 - strlen(output));
        for(int i=0; output[i]; i++) { if(output[i] == '/') output[i] = '\\'; }
    }
}

void Shell::processCommand() {
    if (cmdLen > 0 && cmdLen < CMD_BUF_SIZE) {
        processCommand(cmdBuffer);
    } else {
        // Void command sequence
    }
}

void Shell::processCommand(const char* cmdStr) {
    char tempBuf[CMD_BUF_SIZE];
    strncpy(tempBuf, cmdStr, CMD_BUF_SIZE - 1);
    tempBuf[CMD_BUF_SIZE - 1] = '\0';

    char* cmd = tempBuf;
    char* arg = nullptr;

    for (int i = 0; tempBuf[i] != '\0'; i++) {
        if (tempBuf[i] == ' ') {
            tempBuf[i] = '\0';
            arg = &tempBuf[i + 1];
            break;
        } else {
            // Keep looping bounds
        }
    }

    dispatchCommand(cmd, arg);

    console_set_color(CONSOLE_COLOR_WHITE, CONSOLE_COLOR_BLACK);
    stdio_flush();
}

void Shell::onKeyDown(char c) {
    if (c == '\n') {
        printf("\n");
        stdio_flush();
        processCommand();
        cmdLen = 0;
        for (int i = 0; i < CMD_BUF_SIZE; i++) { cmdBuffer[i] = 0; }
        ffi_logger_flush_sync();
        printPrompt();
    } else if (c == '\b') {
        if (cmdLen > 0) {
            cmdLen--;
            cmdBuffer[cmdLen] = '\0';
            printf("\b \b");
            stdio_flush();
        } else {
            // Cannot delete beyond zero point
        }
    } else {
        if (cmdLen < CMD_BUF_SIZE - 1) {
            cmdBuffer[cmdLen] = c;
            cmdLen++;
            char temp[2] = {c, '\0'};
            printf("%s", temp);
            stdio_flush();
        } else {
            beep(500, 100); 
        }
    }
}

void Shell::update() {
    while (keyboard_has_input()) {
        char c = keyboard_getchar();
        if (c != 0) {
            onKeyDown(c);
            console_blink_cursor(true);
        } else {
            // Empty char
        }
    }

    static uint64_t last_blink = 0;
    static bool blink_state = true;
    uint64_t now = hal_timer_get_ticks();
    
    if (now - last_blink > 125) { 
        blink_state = !blink_state;
        console_blink_cursor(blink_state);
        last_blink = now;
    } else {
        // Await cycle
    }
}

extern "C" {
    void shell_init_c() {
        void* ptr = kmalloc(sizeof(Shell));
        if (ptr) {
            global_shell = new (ptr) Shell();
            global_shell->init();
        } else {
            printf("CRITICAL: Failed to allocate Shell!\n");
        }
    }

    void shell_run_startup_c() {
        if (global_shell) { global_shell->runStartup(); } else { /* Dropped */ }
    }

    void shell_update_c() {
        if (global_shell) {
            static bool first_run = true;
            if (first_run) {
                global_shell->showWelcome();
                first_run = false;
            } else {
                // Proceed directly
            }
            global_shell->update();
        } else {
            // Null drop
        }
    }
}
