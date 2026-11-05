#include "shared.h"

#include <stdio.h>
#include <string.h>

int main(void) {
    char filename[1024];
    char child_path[1024];
    char command[2048];
    char map_name[80];
    char ready_name[80];
    char received_name[80];
    SECURITY_ATTRIBUTES attributes = {sizeof(attributes), NULL, TRUE};
    STARTUPINFOA startup = {0};
    PROCESS_INFORMATION child = {0};
    HANDLE file = INVALID_HANDLE_VALUE;
    HANDLE mapping = NULL;
    HANDLE ready = NULL;
    HANDLE received = NULL;
    SharedData *data = NULL;
    int child_started = 0;
    int terminal_message = 0;
    int final_acknowledged = 0;
    int success = 0;

    fprintf(stderr, "Input file: ");
    if (fgets(filename, sizeof(filename), stdin) == NULL) {
        fprintf(stderr, "Cannot read the file name\n");
        return 1;
    }
    filename[strcspn(filename, "\r\n")] = '\0';
    if (filename[0] == '\0') {
        fprintf(stderr, "File name is empty\n");
        return 1;
    }

    file = CreateFileA(filename, GENERIC_READ, FILE_SHARE_READ, &attributes,
                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "CreateFile failed: %lu\n", GetLastError());
        goto cleanup;
    }

    DWORD pid = GetCurrentProcessId();
    snprintf(map_name, sizeof(map_name), "Local\\LabOC4Map_%lu", pid);
    snprintf(ready_name, sizeof(ready_name), "Local\\LabOC4Ready_%lu", pid);
    snprintf(received_name, sizeof(received_name), "Local\\LabOC4Received_%lu", pid);

    mapping = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
                                 0, sizeof(SharedData), map_name);
    if (mapping == NULL || GetLastError() == ERROR_ALREADY_EXISTS) {
        fprintf(stderr, "CreateFileMapping failed or name exists: %lu\n", GetLastError());
        goto cleanup;
    }
    data = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(*data));
    if (data == NULL) {
        fprintf(stderr, "MapViewOfFile failed: %lu\n", GetLastError());
        goto cleanup;
    }
    ready = CreateEventA(NULL, FALSE, FALSE, ready_name);
    if (ready == NULL || GetLastError() == ERROR_ALREADY_EXISTS) {
        fprintf(stderr, "CreateEvent(ready) failed or name exists: %lu\n", GetLastError());
        goto cleanup;
    }
    received = CreateEventA(NULL, FALSE, FALSE, received_name);
    if (received == NULL || GetLastError() == ERROR_ALREADY_EXISTS) {
        fprintf(stderr, "CreateEvent(received) failed or name exists: %lu\n", GetLastError());
        goto cleanup;
    }

    DWORD path_length = GetModuleFileNameA(NULL, child_path, sizeof(child_path));
    if (path_length == 0 || path_length >= sizeof(child_path)) {
        fprintf(stderr, "GetModuleFileName failed: %lu\n", GetLastError());
        goto cleanup;
    }
    char *slash = strrchr(child_path, '\\');
    if (slash == NULL || (size_t) (slash - child_path) + sizeof("child.exe") >= sizeof(child_path)) {
        fprintf(stderr, "Cannot locate child.exe beside parent.exe\n");
        goto cleanup;
    }
    strcpy(slash + 1, "child.exe");
    int command_length = snprintf(command, sizeof(command), "\"%s\" %s %s %s",
                                  child_path, map_name, ready_name, received_name);
    if (command_length < 0 || (size_t) command_length >= sizeof(command)) {
        fprintf(stderr, "Command line is too long\n");
        goto cleanup;
    }

    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = file;
    startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    if (!CreateProcessA(child_path, command, NULL, NULL, TRUE, 0,
                        NULL, NULL, &startup, &child)) {
        fprintf(stderr, "CreateProcess failed: %lu\n", GetLastError());
        goto cleanup;
    }
    child_started = 1;
    CloseHandle(file);
    file = INVALID_HANDLE_VALUE;

    HANDLE waits[2] = {ready, child.hProcess};
    for (;;) {
        DWORD state = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
        if (state == WAIT_OBJECT_0 + 1) {
            fprintf(stderr, "Child exited without a final message\n");
            break;
        }
        if (state != WAIT_OBJECT_0) {
            fprintf(stderr, "WaitForMultipleObjects failed: %lu\n", GetLastError());
            break;
        }
        if (data->kind == MESSAGE_RESULT) {
            if (printf("%ld\n", data->sum) < 0 || fflush(stdout) == EOF) {
                fprintf(stderr, "Cannot print result for line %lu\n", data->line);
                break;
            }
        } else if (data->kind == MESSAGE_DONE) {
            terminal_message = 1;
            success = 1;
        } else if (data->kind == MESSAGE_ERROR) {
            fprintf(stderr, "Child reported an error on line %lu\n", data->line);
            terminal_message = 1;
        } else {
            fprintf(stderr, "Unknown message from child\n");
            break;
        }
        if (!SetEvent(received)) {
            fprintf(stderr, "SetEvent(received) failed: %lu\n", GetLastError());
            success = 0;
            break;
        }
        if (terminal_message) {
            final_acknowledged = 1;
            break;
        }
    }

cleanup:
    if (child_started) {
        if (!final_acknowledged && WaitForSingleObject(child.hProcess, 0) == WAIT_TIMEOUT) {
            TerminateProcess(child.hProcess, 1);
        }
        if (WaitForSingleObject(child.hProcess, INFINITE) != WAIT_OBJECT_0) {
            fprintf(stderr, "Cannot wait for child: %lu\n", GetLastError());
            success = 0;
        }
        DWORD exit_code;
        if (!GetExitCodeProcess(child.hProcess, &exit_code) || exit_code != 0) {
            fprintf(stderr, "Child ended with an error\n");
            success = 0;
        }
        CloseHandle(child.hThread);
        CloseHandle(child.hProcess);
    }
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    if (received != NULL) CloseHandle(received);
    if (ready != NULL) CloseHandle(ready);
    if (data != NULL) UnmapViewOfFile(data);
    if (mapping != NULL) CloseHandle(mapping);
    return success ? 0 : 1;
}
