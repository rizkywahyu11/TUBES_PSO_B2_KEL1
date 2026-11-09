#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>

#define MAX_INPUT 1024
#define MAX_ARGS 100
#define MAX_CMDS 20
#define MAX_TOKENS 1024

// Struktur perintah
typedef struct {
    char *args[MAX_ARGS];
    char *input_file;
    char *output_file;
    int argc;
} Command;


// ========================================
// 1. TOKENIZER
// ========================================

int tokenize(char *input, char *tokens[]) {
    int count = 0;
    char *p = input;

    while (*p) {

        while (*p == ' ' || *p == '\t' || *p == '\n')
            p++;

        if (*p == '\0')
            break;

        if (count >= MAX_TOKENS - 1)
            return -1;

        // Membaca operator
        if (*p == '<' || *p == '>' || *p == '|') {

            if (*p == '<')
                tokens[count++] = "<";
            else if (*p == '>')
                tokens[count++] = ">";
            else
                tokens[count++] = "|";

            p++;
        }

        // Membaca argumen
        else {
            tokens[count++] = p;

            while (*p &&
                   *p != ' ' &&
                   *p != '\t' &&
                   *p != '\n' &&
                   *p != '<' &&
                   *p != '>' &&
                   *p != '|') {
                p++;
            }

            if (*p) {
                *p = '\0';
                p++;
            }
        }
    }

    tokens[count] = NULL;

    return count;
}


// ========================================
// 2. PARSER
// ========================================

int parse_commands(char *tokens[], int count, Command cmds[]) {

    memset(cmds, 0, sizeof(Command) * MAX_CMDS);

    int cmd_count = 1;

    for (int i = 0; i < count; i++) {

        Command *cmd = &cmds[cmd_count - 1];

        // PIPE
        if (strcmp(tokens[i], "|") == 0) {

            if (cmd->argc == 0 || cmd_count >= MAX_CMDS)
                return -1;

            cmd_count++;
        }

        // INPUT REDIRECTION
        else if (strcmp(tokens[i], "<") == 0) {

            if (cmd->input_file != NULL || i + 1 >= count)
                return -1;

            cmd->input_file = tokens[++i];

            if (strcmp(cmd->input_file, "<") == 0 ||
                strcmp(cmd->input_file, ">") == 0 ||
                strcmp(cmd->input_file, "|") == 0)
                return -1;
        }

        // OUTPUT REDIRECTION
        else if (strcmp(tokens[i], ">") == 0) {

            if (cmd->output_file != NULL || i + 1 >= count)
                return -1;

            cmd->output_file = tokens[++i];

            if (strcmp(cmd->output_file, "<") == 0 ||
                strcmp(cmd->output_file, ">") == 0 ||
                strcmp(cmd->output_file, "|") == 0)
                return -1;
        }

        // ARGUMEN BIASA
        else {

            if (cmd->argc >= MAX_ARGS - 1)
                return -1;

            cmd->args[cmd->argc++] = tokens[i];
        }
    }

    // Validasi setiap perintah
    for (int i = 0; i < cmd_count; i++) {

        if (cmds[i].argc == 0)
            return -1;

        cmds[i].args[cmds[i].argc] = NULL;
    }

    return cmd_count;
}


// ========================================
// 3. I/O HANDLER
// ========================================

int setup_redirection(Command *cmd) {

    // INPUT REDIRECTION
    if (cmd->input_file != NULL) {

        int fd = open(cmd->input_file, O_RDONLY);

        if (fd < 0) {
            perror("open input");
            return -1;
        }

        if (dup2(fd, STDIN_FILENO) < 0) {
            perror("dup2 input");
            close(fd);
            return -1;
        }

        close(fd);
    }

    // OUTPUT REDIRECTION
    if (cmd->output_file != NULL) {

        int fd = open(
            cmd->output_file,
            O_WRONLY | O_CREAT | O_TRUNC,
            0644
        );

        if (fd < 0) {
            perror("open output");
            return -1;
        }

        if (dup2(fd, STDOUT_FILENO) < 0) {
            perror("dup2 output");
            close(fd);
            return -1;
        }

        close(fd);
    }

    return 0;
}


// ========================================
// 4. PROCESS MANAGER
// ========================================

void execute_commands(Command cmds[], int cmd_count) {

    int pipes[MAX_CMDS - 1][2];
    pid_t pids[MAX_CMDS];

    // Membuat pipe
    for (int i = 0; i < cmd_count - 1; i++) {

        if (pipe(pipes[i]) < 0) {
            perror("pipe");

            for (int j = 0; j < i; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }

            return;
        }
    }

    // Membuat proses anak
    for (int i = 0; i < cmd_count; i++) {

        pids[i] = fork();

        if (pids[i] < 0) {

            perror("fork");

            for (int j = 0; j < cmd_count - 1; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }

            for (int j = 0; j < i; j++)
                waitpid(pids[j], NULL, 0);

            return;
        }

        if (pids[i] == 0) {

            // Input dari pipe sebelumnya
            if (i > 0) {

                if (dup2(pipes[i - 1][0], STDIN_FILENO) < 0) {
                    perror("dup2 pipe input");
                    exit(EXIT_FAILURE);
                }
            }

            // Output ke pipe berikutnya
            if (i < cmd_count - 1) {

                if (dup2(pipes[i][1], STDOUT_FILENO) < 0) {
                    perror("dup2 pipe output");
                    exit(EXIT_FAILURE);
                }
            }

            // Menutup seluruh pipe
            for (int j = 0; j < cmd_count - 1; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }

            // Mengatur redirection
            if (setup_redirection(&cmds[i]) < 0)
                exit(EXIT_FAILURE);

            // Menjalankan perintah
            execvp(cmds[i].args[0], cmds[i].args);

            perror("execvp");
            exit(EXIT_FAILURE);
        }
    }

    // Parent menutup seluruh pipe
    for (int i = 0; i < cmd_count - 1; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }

    // Menunggu seluruh proses anak
    for (int i = 0; i < cmd_count; i++) {
        waitpid(pids[i], NULL, 0);
    }
}


// ========================================
// 5. MAIN PROGRAM
// ========================================

int main() {

    char input[MAX_INPUT];
    char *tokens[MAX_TOKENS];

    while (1) {

        printf("advanced-shell> ");
        fflush(stdout);

        if (fgets(input, sizeof(input), stdin) == NULL) {
            printf("\n");
            break;
        }

        if (strlen(input) == 1)
            continue;

        // Parsing token
        int token_count = tokenize(input, tokens);

        if (token_count < 0) {
            printf("Error: Terlalu banyak token!\n");
            continue;
        }

        if (token_count == 0)
            continue;

        Command cmds[MAX_CMDS];

        // Parsing command
        int cmd_count = parse_commands(
            tokens,
            token_count,
            cmds
        );

        if (cmd_count < 0) {
            printf("Error: Sintaks perintah tidak valid!\n");
            continue;
        }

        // Built-in exit
        if (cmd_count == 1 &&
            strcmp(cmds[0].args[0], "exit") == 0)
            break;

        // Eksekusi
        execute_commands(cmds, cmd_count);
    }

    return 0;
}
