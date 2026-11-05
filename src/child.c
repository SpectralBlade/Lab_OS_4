#include "shared.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

static int send_message(SharedData *data, HANDLE ready, HANDLE received,
                        LONG kind, LONG sum, DWORD line) {
    data->kind = kind;
    data->sum = sum;
    data->line = line;

    if (!SetEvent(ready)) {
        fprintf(stderr, "SetEvent(ready) failed: %lu\n", GetLastError());
        return 0;
    }
    if (WaitForSingleObject(received, INFINITE) != WAIT_OBJECT_0) {
        fprintf(stderr, "WaitForSingleObject(received) failed: %lu\n", GetLastError());
        return 0;
    }
    return 1;
}

static int add_number(const char *token, long long *sum) {
    char *end;
    long number;

    errno = 0;
    number = strtol(token, &end, 10);
    if (end == token || *end != '\0' || errno == ERANGE ||
        number < INT_MIN || number > INT_MAX) {
        return 0;
    }
    *sum += number;
    return *sum >= INT_MIN && *sum <= INT_MAX;
}

int main(int argc, char *argv[]) {
    HANDLE mapping = NULL;
    HANDLE ready = NULL;
    HANDLE received = NULL;
    SharedData *data = NULL;
    char token[64];
    size_t length = 0;
    long long sum = 0;
    DWORD line = 1;
    int has_number = 0;
    int result = 1;

    if (argc != 4) {
        fprintf(stderr, "Usage: child <mapping> <ready-event> <received-event>\n");
        return 1;
    }

    mapping = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, argv[1]);
    if (mapping == NULL) {
        fprintf(stderr, "OpenFileMapping failed: %lu\n", GetLastError());
        goto cleanup;
    }
    data = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(*data));
    if (data == NULL) {
        fprintf(stderr, "MapViewOfFile failed: %lu\n", GetLastError());
        goto cleanup;
    }
    ready = OpenEventA(EVENT_MODIFY_STATE, FALSE, argv[2]);
    if (ready == NULL) {
        fprintf(stderr, "OpenEvent(ready) failed: %lu\n", GetLastError());
        goto cleanup;
    }
    received = OpenEventA(SYNCHRONIZE, FALSE, argv[3]);
    if (received == NULL) {
        fprintf(stderr, "OpenEvent(received) failed: %lu\n", GetLastError());
        goto cleanup;
    }

    for (;;) {
        int character = fgetc(stdin);

        if (character == EOF && ferror(stdin)) {
            fprintf(stderr, "Cannot read input on line %lu\n", line);
            goto notify_error;
        }
        if (character == EOF || isspace((unsigned char) character)) {
            if (length != 0) {
                token[length] = '\0';
                if (!add_number(token, &sum)) {
                    fprintf(stderr, "Invalid number or sum on line %lu\n", line);
                    goto notify_error;
                }
                has_number = 1;
                length = 0;
            }
            if ((character == '\n' || character == EOF) && has_number) {
                if (!send_message(data, ready, received,
                                  MESSAGE_RESULT, (LONG) sum, line)) {
                    goto cleanup;
                }
                sum = 0;
                has_number = 0;
            }
            if (character == EOF) {
                break;
            }
            if (character == '\n') {
                ++line;
            }
        } else {
            if (length + 1 >= sizeof(token)) {
                fprintf(stderr, "Number is too long on line %lu\n", line);
                goto notify_error;
            }
            token[length++] = (char) character;
        }
    }

    if (send_message(data, ready, received, MESSAGE_DONE, 0, line)) {
        result = 0;
    }
    goto cleanup;

notify_error:
    send_message(data, ready, received, MESSAGE_ERROR, 0, line);

cleanup:
    if (received != NULL) CloseHandle(received);
    if (ready != NULL) CloseHandle(ready);
    if (data != NULL) UnmapViewOfFile(data);
    if (mapping != NULL) CloseHandle(mapping);
    return result;
}
