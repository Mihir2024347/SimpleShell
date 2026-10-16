#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#include <time.h>

#define MAX_COMMAND_LENGTH 1024
#define MAX_ARGUMENTS 100
#define MAX_COMMANDS 20
#define MAX_HISTORY 100

typedef struct {
    char command[MAX_COMMAND_LENGTH];
    pid_t child_pids[MAX_COMMANDS];
    int child_count;
    time_t execution_time;
    double elapsed_time;
    int return_status;
} HistoryEntry;

HistoryEntry history[MAX_HISTORY];
int total_history = 0;

volatile sig_atomic_t shell_running = 1;

void process_interrupt(int sig) {
    (void)sig;
    shell_running = 0;
}

char *read_user_input(char *input_command) {
    if (fgets(input_command, MAX_COMMAND_LENGTH, stdin) == NULL) {
        if (ferror(stdin) && errno == EINTR) {
            clearerr(stdin);
        }
        return NULL;
    }

    input_command[strcspn(input_command, "\n")] = '\0';

    return input_command;
}

void parse_command(char *input_command, char **arguments) {
    int argument_index = 0;

    char *current_token = strtok(input_command, " \t");

    while (current_token != NULL && argument_index < MAX_ARGUMENTS - 1) {
        arguments[argument_index] = current_token;
        argument_index++;

        current_token = strtok(NULL, " \t");
    }

    arguments[argument_index] = NULL;
}

int division_of_pipeline(char *input_command, char **pipeline_commands) {
    int pipeline_count = 0;
    char *current_position = input_command;

    while (*current_position != '\0') {
        if (pipeline_count >= MAX_COMMANDS) {
            fprintf(stderr, "Error: too many commands in pipeline\n");
            return -1;
        }

        pipeline_commands[pipeline_count] = current_position;
        pipeline_count++;

        while (*current_position != '\0' && *current_position != '|') {
            current_position++;
        }

        if (*current_position == '|') {
            *current_position = '\0';
            current_position++;
        }
    }

    return pipeline_count;
}

void storing_history(const char *input_command) {
    if (input_command[0] == '\0') {
        return;
    }

    if (total_history < MAX_HISTORY) {
        strncpy(history[total_history].command,
                input_command,
                MAX_COMMAND_LENGTH - 1);

        history[total_history].command[MAX_COMMAND_LENGTH - 1] = '\0';

        history[total_history].child_count = 0;
        history[total_history].return_status = 0;

        total_history++;
    }
    else {
        for (int history_index = 1; history_index < MAX_HISTORY; history_index++) {
            history[history_index - 1] = history[history_index];
        }

        strncpy(history[MAX_HISTORY - 1].command,
                input_command,
                MAX_COMMAND_LENGTH - 1);

        history[MAX_HISTORY - 1].command[MAX_COMMAND_LENGTH - 1] = '\0';

        history[MAX_HISTORY - 1].child_count = 0;
        history[MAX_HISTORY - 1].return_status = 0;
    }
}

void showing_of_history(void) {
    for (int history_index = 0; history_index < total_history; history_index++) {
        printf("%d  %s\n",
               history_index + 1,
               history[history_index].command);
    }
}

double differenceof_time(struct timespec start, struct timespec end) {
    return (double)(end.tv_sec - start.tv_sec)
         + (double)(end.tv_nsec - start.tv_nsec) / 1000000000.0;
}

void show_execution_details(void) {
    printf("\n\nExecution details:\n");

    for (int history_index = 0;
         history_index < total_history;
         history_index++) {

        char formatted_time[64];
        struct tm local_time;

        localtime_r(&history[history_index].execution_time,
                    &local_time);

        strftime(formatted_time,
                 sizeof(formatted_time),
                 "%Y-%m-%d %H:%M:%S",
                 &local_time);

        printf("\nCommand: %s\n",
               history[history_index].command);

        printf("PID: ");

        if (history[history_index].child_count == 0) {
            printf("N/A");
        }
        else {
            for (int child_index = 0;
                 child_index < history[history_index].child_count;
                 child_index++) {

                printf("%d",
                       history[history_index].child_pids[child_index]);

                if (child_index + 1 <
                    history[history_index].child_count) {
                    printf(", ");
                }
            }
        }

        printf("\n");
        printf("Executed at: %s\n", formatted_time);
        printf("Duration: %.6f seconds\n",
               history[history_index].elapsed_time);
        printf("Exit status: %d\n",
               history[history_index].return_status);
    }
}

int waiting_for_child(pid_t child_pid, int *child_status) {
    while (waitpid(child_pid, child_status, 0) < 0) {
        if (errno == EINTR) {
            continue;
        }

        perror("waitpid");
        return -1;
    }

    return 0;
}

int create_process_and_run(char *input_command,
                           HistoryEntry *history_record) {

    char *arguments[MAX_ARGUMENTS];

    parse_command(input_command, arguments);

    if (arguments[0] == NULL) {
        return 1;
    }

    int fork_result = fork();

    if (fork_result < 0) {
        perror("fork");
        return 1;
    }
    else if (fork_result == 0) {
        signal(SIGINT, SIG_DFL);

        execvp(arguments[0], arguments);

        perror("exec");
        exit(127);
    }
    else {
        history_record->child_pids[0] = fork_result;
        history_record->child_count = 1;

        int child_status;

        if (waiting_for_child(fork_result, &child_status) < 0) {
            return 1;
        }

        if (WIFEXITED(child_status)) {
            history_record->return_status =
                WEXITSTATUS(child_status);
        }
        else if (WIFSIGNALED(child_status)) {
            history_record->return_status =
                128 + WTERMSIG(child_status);
        }

        return 1;
    }
}

int run_pipeline(char *input_command,
                 HistoryEntry *history_record) {

    char *pipeline_commands[MAX_COMMANDS];

    int pipeline_count =
        division_of_pipeline(input_command,
                             pipeline_commands);

    if (pipeline_count < 0) {
        return 1;
    }

    if (pipeline_count < 2) {
        return create_process_and_run(input_command,
                                      history_record);
    }

    int pipe_fds[MAX_COMMANDS - 1][2];
    pid_t child_pids[MAX_COMMANDS];

    for (int pipe_index = 0;
         pipe_index < pipeline_count - 1;
         pipe_index++) {

        if (pipe(pipe_fds[pipe_index]) < 0) {
            perror("pipe");
            return 1;
        }
    }

    for (int command_index = 0;
         command_index < pipeline_count;
         command_index++) {

        char *arguments[MAX_ARGUMENTS];

        parse_command(pipeline_commands[command_index],
                      arguments);

        if (arguments[0] == NULL) {
            fprintf(stderr,
                    "Error: empty command in pipeline\n");

            for (int pipe_index = 0;
                 pipe_index < pipeline_count - 1;
                 pipe_index++) {

                close(pipe_fds[pipe_index][0]);
                close(pipe_fds[pipe_index][1]);
            }

            return 1;
        }

        child_pids[command_index] = fork();

        if (child_pids[command_index] < 0) {
            perror("fork");

            for (int pipe_index = 0;
                 pipe_index < pipeline_count - 1;
                 pipe_index++) {

                close(pipe_fds[pipe_index][0]);
                close(pipe_fds[pipe_index][1]);
            }

            return 1;
        }

        if (child_pids[command_index] == 0) {
            signal(SIGINT, SIG_DFL);

            if (command_index > 0) {
                if (dup2(pipe_fds[command_index - 1][0],
                         STDIN_FILENO) < 0) {

                    perror("dup2");
                    exit(1);
                }
            }

            if (command_index < pipeline_count - 1) {
                if (dup2(pipe_fds[command_index][1],
                         STDOUT_FILENO) < 0) {

                    perror("dup2");
                    exit(1);
                }
            }

            for (int pipe_index = 0;
                 pipe_index < pipeline_count - 1;
                 pipe_index++) {

                close(pipe_fds[pipe_index][0]);
                close(pipe_fds[pipe_index][1]);
            }

            execvp(arguments[0], arguments);

            perror("exec");
            exit(127);
        }

        history_record->child_pids[command_index] =
            child_pids[command_index];
    }

    history_record->child_count = pipeline_count;

    for (int pipe_index = 0;
         pipe_index < pipeline_count - 1;
         pipe_index++) {

        close(pipe_fds[pipe_index][0]);
        close(pipe_fds[pipe_index][1]);
    }

    int final_status = 0;

    for (int child_index = 0;
         child_index < pipeline_count;
         child_index++) {

        int child_status;

        if (waiting_for_child(child_pids[child_index],
                              &child_status) < 0) {
            return 1;
        }

        if (WIFEXITED(child_status)) {
            final_status = WEXITSTATUS(child_status);
        }
        else if (WIFSIGNALED(child_status)) {
            final_status = 128 + WTERMSIG(child_status);
        }
    }
    history_record->return_status = final_status;
    return 1;
}
int launch(char *input_command,
           HistoryEntry *history_record) {
    if (strchr(input_command, '|') != NULL) {
        return run_pipeline(input_command,
                            history_record);
    }
    return create_process_and_run(input_command,
                                  history_record);
}
void shell_loop(void) {
    char input_command[MAX_COMMAND_LENGTH];
    while (shell_running) {
        printf("simple-shell> ");
        fflush(stdout);
        if (read_user_input(input_command) == NULL) {
            if (!shell_running) {
                break;
            }
            if (feof(stdin)) {
                break;
            }
            continue;
        }
        if (input_command[0] == '\0') {
            continue;
        }
        storing_history(input_command);
        HistoryEntry *history_record =
            &history[total_history - 1];
        struct timespec start;
        struct timespec end;
        if (clock_gettime(CLOCK_MONOTONIC, &start) < 0) {
            perror("clock_gettime");
        }
        history_record->execution_time = time(NULL);
        if (strcmp(input_command, "history") == 0) {
            history_record->child_pids[0] = getpid();
            history_record->child_count = 1;
            showing_of_history();
            history_record->return_status = 0;
        }
        else {
            char command_copy[MAX_COMMAND_LENGTH];
            strncpy(command_copy,
                    input_command,
                    MAX_COMMAND_LENGTH - 1);
            command_copy[MAX_COMMAND_LENGTH - 1] = '\0';
            launch(command_copy, history_record);
        }
        if (clock_gettime(CLOCK_MONOTONIC, &end) < 0) {
            perror("clock_gettime");
            history_record->elapsed_time = 0.0;
        }
        else {
            history_record->elapsed_time =
                differenceof_time(start, end);
        }
    }
}
int main(void) {
    if (signal(SIGINT, process_interrupt) == SIG_ERR) {
        perror("signal");
        return 1;
    }
    shell_loop();
    show_execution_details();
    return 0;
}