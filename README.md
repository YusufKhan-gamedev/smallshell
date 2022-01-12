# Small Shell
 A shell in C.
 It implements a subset of features of bash. 

### Features
 1. Use Unix process API to create and manage processes. 
    - fork() to create a new create process
    - execvp() to run a different program that is the user input command
    - waitpid() to check for the termination of the child process, and cleans up resources from child processes.
    - getpid() to get the process id
    - getenv() to get environment variable
 2. Support input and output redirection using dup2() system call
 3. Switch beteween foreground-only and foreground-and-background
 4. Expand any instance of "$$" in a command into the process ID
 5. Implement custom handlers to override 2 signals, SIGINT and SIGTSTP
 6. Used valgrind to ensure memory-leak free