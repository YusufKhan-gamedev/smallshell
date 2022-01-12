/**
 * A shell in C
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h> 
#include <stdbool.h> 
#include <sys/stat.h> 
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>

int MAX_LENGTH = 2048;           // maximum length of command lines
int MAX_ARGS = 512;              // maximum arguments
int MAX_BG_PROCESSES = 100;      // maximum number of background processes
pid_t foreground_child_process;  // only one foreground is running at a time
bool foreground_only = false;    // mode

/**
 * Toggle the passed boolean variable
*/
void toggle(bool *var) {
    *var = !(*var);
}

/**
 * ^C, SIGINT, #2 --> terminate the foreground child process
*/
void handle_SIGINT(int signo) {
    kill(foreground_child_process, SIGKILL); // terminate the process
}

/**
 * ^Z, SIGTSTP --> toggle foreground-only mode
 * override SIGTSTP to signal switching beteween foreground-only and foreground-and-background
*/
void handle_SIGTSTP(int input_signal) {
    toggle(&foreground_only);
    if (foreground_only) {
        char* message = "Entering foreground-only mode (& is now ignored)\n";
        write(STDOUT_FILENO, message, 50);fflush(stdout);
    } else {
        char* message = "Exiting foreground-only mode\n";
        write(STDOUT_FILENO, message, 30); fflush(stdout);
    }
}

/**
 * Register handle_SIGINT as the signal handler
 * override SIGINT, that's when user press CTRL-C
 * call this function so the process will receive SIGINT
*/
void register_SIGINT() {
    struct sigaction SIGINT_action = {{0}};
	SIGINT_action.sa_handler = handle_SIGINT; // assign with our own handler
	sigfillset(&SIGINT_action.sa_mask);
	SIGINT_action.sa_flags = 0;
	sigaction(SIGINT, &SIGINT_action, NULL);  // override SIGINT
}

/**
 * Register handle_SIGTSTP as the signal handler
 * override SIGTSTP, that's when user press CTRL-Z enter kill -SIGTSTP $$
 * call this function so the process will receive SIGTSTP
*/
void register_SIGTSTP() {
    struct sigaction SIGTSTP_action = {{0}};
	SIGTSTP_action.sa_handler = handle_SIGTSTP; // assign with our own handler
	sigfillset(&SIGTSTP_action.sa_mask);
    SIGTSTP_action.sa_flags = SA_RESTART;       //automatically restart of the interrupted system call / library function after the signal is done.
	sigaction(SIGTSTP, &SIGTSTP_action, NULL);  // override SIGINT
}

/**
 * Call this function so the process won't receive SIGINT
*/
void ignore_SIGINT() {
    struct sigaction ignore_action = {{0}};
    ignore_action.sa_handler = SIG_IGN;      // assign with a constant
	sigaction(SIGINT, &ignore_action, NULL); // override SIGINT
}

/**
 * Call this function so the process won't receive SIGTSTP
*/
void ignore_SIGTSTP() {
    struct sigaction ignore_action = {{0}};
    ignore_action.sa_handler = SIG_IGN;       // assign with a constant
	sigaction(SIGTSTP, &ignore_action, NULL); // override SIGTSTP
}

// replace $$ with id
char* expand(char* path, pid_t id) {
    char* temp = path;  // save the pointer to the path

    // count how many $
    int count = 0;
    while(*path) {
        if ((*path) == '$')
            count++;
        path++;
    }

    path = temp;                // reset path
    int num_id = count / 2;     // how many $$
    int remainder = count % 2;  // remaining $
    char id_string[8];          // for pid
    sprintf(id_string, "%d", id); // convert integer id to string
    char* expansion = calloc(strlen(path)+ strlen(id_string)*num_id + 1, sizeof(char));
    strncpy(expansion, path, strlen(path)-count);  // copy the non-$$ portion
    for (int i = 0; i < num_id; i++) {     // append id 
        strcat(expansion, id_string);
    }
    for (int i = 0; i < remainder; i++) {  // append the rest &
        strcat(expansion, "$");
    }
    return expansion;
}


/**
 * Print out a message ontaining the previous exit value
*/
void print_status(int status_value){
    if (WIFEXITED(status_value)) {
        printf("exit value %d\n", WEXITSTATUS(status_value));
    } else {
        printf("terminated by signal %d\n", WTERMSIG(status_value));
    }
}


/**
 * Print out a message containing background processes exit value that has not printed out
*/
void print_background_status(int pid, int status_value) {
    if (WIFEXITED(status_value)) {
        printf("background pid %d is done: exit value %d\n", pid, WEXITSTATUS(status_value));
    } else {
        printf("background pid %d is done: terminated by signal %d\n", pid, WTERMSIG(status_value));
    }
}

/**
 * Read in user input from keyboard
*/
void get_user_input(char* input) {
    memset(input, '\0', MAX_LENGTH);
    write(STDOUT_FILENO, ": ", 2); fflush(stdout);
    scanf("%[^\n]s", input); // Read until it hits newline
    getchar(); // remove a char
}

/**
 * Parse user input by space into an array of strings
*/
void parse_command(int* length_address, char* input, char** parsed_command) {
    for(int i = 0; i < *(length_address); i++) {
        free(parsed_command[i]);
    }
    *(length_address) = 0;
    int i = 0;
    char* rest;
    char* token = strtok_r(input, " ", &rest);
    while(token) {
        parsed_command[i] = calloc(strlen(token) + 1, sizeof(char));
        strcpy(parsed_command[i], token);
        token = strtok_r(NULL, " ", &rest);
        (*length_address)++;
        i++;
    }
}

/**
 * struct that contains elements included in user input command and process-related data
*/
struct small_shell {
    int status_value ;
    char* input;
    char** parsed_command;
    int length;
    int* array_noncompleted_background_processes;
    int index_noncompleted;
    bool background;
    bool exit_shell;
};

/**
* Start the shell, keep it running and prompting for user input unitl user enters "exit"
*/
void start() {

    // create a smallsh shell object and initiaze the data members to zero-equivalence
    struct small_shell* sh = malloc(sizeof(struct small_shell));
    sh->status_value = 0;
    sh->input = calloc(MAX_LENGTH + 1, sizeof(char));
    sh->parsed_command = calloc(MAX_ARGS, sizeof(char*));
    sh->length = 0;
    sh->array_noncompleted_background_processes = calloc(MAX_BG_PROCESSES, sizeof(int));  // maximum number of background processes
    sh->index_noncompleted = 0;
    sh->background = false;
    sh->exit_shell = false;

    do {
        register_SIGTSTP();        // shell receives SIGTSTP
        get_user_input(sh->input); // read in user command
        parse_command(&(sh->length), sh->input, sh->parsed_command); // parse command

        // expand $$
        for(int i = 1; i < sh->length; i++) {
            char* temp = sh->parsed_command[i];
            sh->parsed_command[i] = expand(sh->parsed_command[i], getpid());
            free(temp);
        }

        // 3 built in commands
        // exit out shell
        if (sh->length > 0 && (strcmp(sh->parsed_command[0], "exit") == 0)) {   
            sh->exit_shell = true;
        }
        // change directory
        else if (sh->length > 0 && (strcmp(sh->parsed_command[0], "cd") == 0)) { 
            if (sh->length == 1) { // change into home direcoty
                char* home_dir = getenv("HOME");
                chdir(home_dir);
            } else { // change into the given directory
                chdir(sh->parsed_command[1]);
            }
        }
        // print out status
        else if (sh->length > 0 && (strcmp(sh->parsed_command[0], "status") == 0)) { 
            print_status(sh->status_value);
        }
        
        // non-built in commands
        else {
            // check all background processes that were not terminated during the last iteration.
            // if terminated and has not be printed out, print out a message
            for (int i = 0; i < sh->index_noncompleted; i++) {
                pid_t pid = sh->array_noncompleted_background_processes[i];
                if (pid > 0 && (waitpid(pid, &(sh->status_value), WNOHANG) == pid)) {
                    print_background_status(pid, sh->status_value);
                    sh->array_noncompleted_background_processes[i] = 0; // change it to 0 indicate a message has been printed out
                }
            }

            // blank line or #comment
            if ((sh->length == 0) || sh->parsed_command[0][0] == '#' )
                continue;
            
            // check if the command ends with & background
            sh->background = false; // reset
            if (strcmp(sh->parsed_command[sh->length-1], "&") == 0) {
                sh->parsed_command[sh->length-1] = NULL; // change "&" to NULL
                sh->background = true;
            }
            
            // fork() new process
            pid_t child_pid = fork();
            switch (child_pid) {
                case -1:
                    exit(1);
                    break;
                case 0:
                    // Redirect Input < and Output >
                    for(int i = 1; i < sh->length; i++) {
                        if (sh->parsed_command[i] != NULL && (strcmp(sh->parsed_command[i], ">") == 0)) { 
                            int out_fd = open(sh->parsed_command[i+1], O_WRONLY | O_CREAT | O_TRUNC, 0640);
                            if (out_fd == -1) { 
                                perror("target open()"); 
                                exit(1); 
                            }
                            int result_target = dup2(out_fd, 1);  // write to file instead of the terminal
                            if (result_target == -1) { 
                                perror("target dup2()"); 
                                exit(1); 
                            }
                            // change > filename to NULL before execvp()
                            sh->parsed_command[i] = NULL; 
                            sh->parsed_command[i+1] = NULL;
                        } else if (sh->parsed_command[i] != NULL && (strcmp(sh->parsed_command[i], "<") == 0)) {
                            int in_fd = open(sh->parsed_command[i+1], O_RDONLY);
                            if (in_fd == -1) { 
                                printf("cannot open %s for input\n", sh->parsed_command[i+1]); fflush(stdout);
                                exit(1); 
                            }
                            int result_source = dup2(in_fd, 0);  // read from file instead of the terminal
                            if (result_source == -1) { 
                                perror("source dup2()"); 
                                exit(1); 
                            }
                            // change < filename to NULL before execvp()
                            sh->parsed_command[i] = NULL;
                            sh->parsed_command[i+1] = NULL;
                        }
                    }

                    foreground_child_process = getpid();  // update the global variable. There is only 1 foreground process running at a time.
                    ignore_SIGTSTP(); // both background and foreground child process ignore SIGTSTP

                    // two cases where child process should recieve SIGINT
                    // 1) a forground process   2) foreground-only mode
                    if (!sh->background || foreground_only) {
                        register_SIGINT();
                    }

                    // run the command using execvp() that will return only if error occured
                    sh->parsed_command[sh->length] = NULL;
                    execvp(sh->parsed_command[0], sh->parsed_command);
                    perror(sh->parsed_command[0]);
                    exit(1);
                    break;

                default:
                    ignore_SIGINT(); // parent process running the shell receives SIGTSTP

                    // shell doesn't wait for a background command when it's not forground_only mode
                    if (sh->background && !foreground_only) {
                        printf("background pid is %d\n", child_pid); fflush(stdout);
                        sh->array_noncompleted_background_processes[sh->index_noncompleted++] = child_pid;
                        waitpid(child_pid, &(sh->status_value), WNOHANG);
                    }
                    
                    else {
                        // the shell must wait for the completion of the foreground process
                        waitpid(child_pid, &(sh->status_value), 0);
                        // print out message if the process terminated abnormally
                        if (WIFSIGNALED(sh->status_value)) {
                            printf("terminated by signal %d\n", WTERMSIG(sh->status_value)); fflush(stdout);
                        }
                    }

                    // check all background processes
                    for (int i = 0; i < sh->index_noncompleted; i++) {
                        pid_t pid = sh->array_noncompleted_background_processes[i];
                        if (pid > 0 && (waitpid(pid, &(sh->status_value), WNOHANG) == pid)) {
                            print_background_status(pid, sh->status_value);
                            sh->array_noncompleted_background_processes[i] = 0;
                        }
                    }
                    break;
            }
            
        }

    } while (!sh->exit_shell);

    // deallocate
    free(sh->input);
    for (int i = 0; i < sh->length; i++) {
        free(sh->parsed_command[i]);
    }
    free(sh->parsed_command);
    free(sh->array_noncompleted_background_processes);
    free(sh);
}

/**
 * Entry point
*/
int main(int argc, char* argv[]) { 
    start();
    return 0;
}