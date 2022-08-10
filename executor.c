/*
Name: William Ma
UID: 117199086
Directory ID: mrmabit
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <err.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sysexits.h>
#include "executor.h"
#include "command.h"

static int recurse(struct tree *t, int fdin, int fdout);

int execute(struct tree *t) {
   if (t == NULL) {
      return 0;
   }
   return recurse(t, -1, -1);
}

static int recurse(struct tree *t, int fdin, int fdout) {
    pid_t process;
    int status;
    /* Case 1: File redirection */
    if (t->conjunction == NONE) {
        /* Check if the command is to exit or change directory */
        if (strcmp(t->argv[0], "exit") == 0) {
            exit(0);
        } else if (strcmp(t->argv[0], "cd") == 0) {
            if (chdir(t->argv[1]) == -1) {
                perror("Failed to change directory\n");
            }
        /* Create a child and wait if it's the parent */
        } else {
            process = fork();
            if (process == -1) {
                perror("Fork error\n");
            } else {
                if (process > 0) {
                    wait(&status);
                    return status;
                /* Redirect the input and output files */
                } else {
                    /* Check if the command contains an input file. If it is, 
                    open it and duplicate the FD */
                    if (t->input != NULL) {
                        fdin = open(t->input, O_RDONLY);
                        if (fdin < 0) {
                            perror("Failed to open input file\n");
                        }
                        if (dup2(fdin, STDIN_FILENO) < 0) {
                            perror("Failed to duplicate input FD\n");
                        }
                        if (close(fdin) < 0) {
                            perror("Failed to close input file\n");
                        }
                    } 
                    /* Check if the command contains an output file. If it is, 
                    open it and duplicate the FD */
                    if (t->output != NULL) {
                        fdout = open(t->output, O_WRONLY | O_TRUNC | O_CREAT, 0664);
                        if (fdout < 0) {
                            perror("Failed to open output file\n");
                        }
                        if (dup2(fdout, STDIN_FILENO) < 0) {
                            perror("Failed to duplicate output FD\n");
                        }
                        if (close(fdout) < 0) {
                            perror("Failed to close output file\n");
                        }
                    } 
                    execvp(t->argv[0], t->argv);
                    fprintf(stderr, "Failed to execute %s\n", t->argv[0]); 
                    fflush(stdout);   
                    exit(-1);	
                }
            }
        }
    /* Case 2: Piping */
    } else if (t->conjunction == PIPE) {
        int pipe_fd[2];
        pid_t child_one, child_two;
        /* Check for input/output ambiguity */
        if (t->left->output != NULL) {
            printf("Ambiguous output redirect.\n");
        } else {
            if (t->right->input != NULL) {
                printf("Ambiguous input redirect.\n");
            } 
            /* Create the pipe */
            if (pipe(pipe_fd) < 0) {
                perror("Pipe error\n");
            }
            /* Create first child process and duplicate FDs.
            Repeat for all subtrees. */
            child_one = fork();
            if (child_one < 0) {
                perror("Fork error\n");
            } else if (child_one == 0) {    
                if (close(pipe_fd[0]) < 0) {
                    perror("Failed to close read side of pipe\n");
                }       
                if (dup2(pipe_fd[1], STDOUT_FILENO) < 0) {
                    perror("Failed to duplicate pipe write FD\n");
                }
                recurse(t->left, fdin, fdout);
                if (close(pipe_fd[1]) < 0) {
                    perror("Failed to close write side of pipe\n");
                }    
                exit(0);
            /* Create second child process and duplicate FDs.
            Repeat for all subtrees. */   
            } else {                
                child_two = fork();
                if (child_two < 0) {
                    perror("Fork error\n");
                } if (child_two == 0) { 
                    if (close(pipe_fd[1]) < 0) {
                        perror("Failed to close write side of pipe\n");
                    }   
                    if (dup2(pipe_fd[0], STDIN_FILENO) < 0) {
                        perror("Failed to duplicate pipe read FD\n");
                    }
                    recurse(t->right, fdin, fdout);
                    if (close(pipe_fd[0]) < 0) {
                        perror("Failed to close read side of pipe\n");
                    }
                    exit(0); 
                /* Close the pipe and wait on children to finish */    
                } else {
                    if (close(pipe_fd[0]) < 0) {
                        perror("Failed to close read side of pipe\n");
                    }
                    if (close(pipe_fd[1]) < 0) {
                        perror("Failed to close write side of pipe\n");
                    }
                    wait(NULL);
                    wait(NULL);
                }
            }
        } 
    /* Case 3: AND conjunction */
    } else if (t->conjunction == AND) {
        /* if the file is an input, open it */    
        if (t->input != NULL) {
            fdin = open(t->input, O_RDONLY);
            if (fdin < 0) {
                perror("Failed to open input file\n");
            }
        }
        /* if the file is an output, open it */ 
        if (t->output != NULL) {
            fdout = open(t->output, O_WRONLY | O_TRUNC | O_CREAT, 0664);
            if (fdout < 0) {
                perror("Failed to open output file\n");
            }
        }
        /* Repeat on all subtrees, beginning on the left.
        If the return value is 0, repeat on the right subtree 
        afterwards. */
        if (recurse(t->left, fdin, fdout) == 0) {
            recurse(t->right, fdin, fdout);
        }
    /* Case 4: Subshells */
    } else if (t->conjunction == SUBSHELL) {
        /* Create a child */
        process = fork();
        if (process < 0) {
            perror("Fork error\n");
        /* Wait if it's the parent */
        } else {
            if (process > 0) {
                wait(NULL);
            /* Prepare the child for processing */
            } else {
                /* If the file is an input, open it */
                if (t->input != NULL) {
                fdin = open(t->input, O_RDONLY);
                    if (fdin < 0) {
                        perror("Failed to open input file\n");
                    }
                }
                /* If the file is an output, open it */
                if (t->output != NULL) {
                    fdout = open(t->output, O_WRONLY | O_TRUNC | O_CREAT, 0664);
                    if (fdout < 0) {
                        perror("Failed to open output file\n");
                    }
                }
                /* Repeat for the left subtree */
                recurse(t->left, fdin, fdout);
                exit(0);
            }
        }
    } 
    return 0;
}
