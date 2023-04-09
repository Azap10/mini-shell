#include "process.h"
#include <stdbool.h>
#include <dirent.h>
#include <signal.h>

typedef struct Node {
    struct Node* next;
    char directory[256];
} node;

typedef struct List {
    size_t size;
    node* lst;
} list;

list* directory_stack = NULL;

list* list_create();

int simpleCase(const CMD *cmdList, bool BG);

int redirectionCase(const CMD *cmdList);

int pipeCase(const CMD *cmdList, bool BG);

int andOrCase(const CMD *cmdList, int And);

int subcommandCase(const CMD *cmdList, bool BG);

int bgEndCase(const CMD *cmdList, bool BG, bool parentBG);

int background_process(const CMD *cmdList);

int cdCase(const CMD *cmdList, bool BG);

int pushdCase(const CMD *cmdList, bool BG);

int popdCase(const CMD *cmdList, bool BG);

void reap_zombies();

int realProcess(const CMD *cmdList, bool BG);

int process(const CMD *cmdList) {
    return realProcess(cmdList, false);
}

int realProcess(const CMD *cmdList, bool BG) {
    char exit_status[10];
    int return_value;
    // define local variables
    for (int i = 0; i < cmdList->nLocal; i++) {
        setenv(cmdList->locVar[i], cmdList->locVal[i], 1);
    }

    reap_zombies();

    // printf("Process Type: %d\n", cmdList->type);
    // execute commands
    switch (cmdList->type) {
        case SIMPLE:
            if (strcmp(cmdList->argv[0], "cd") == 0) {
                return_value = cdCase(cmdList, BG);    
            }
            else if (strcmp(cmdList->argv[0], "pushd") == 0) {
                return_value = pushdCase(cmdList, BG); 
            }
            else if (strcmp(cmdList->argv[0], "popd") == 0) {
                return_value = popdCase(cmdList, BG);
            }
            else {
                return_value = simpleCase(cmdList, BG);
            }
            break;

        case PIPE:
            return_value = pipeCase(cmdList, BG);
            break;

        case SEP_AND:
            return_value = andOrCase(cmdList, 0);
            break;

        case SEP_OR:
            return_value = andOrCase(cmdList, 1);
            break;

        case SUBCMD:
            return_value = subcommandCase(cmdList, BG);
            break;

        case SEP_BG:
            return_value = bgEndCase(cmdList, true, BG);
            break;

        case SEP_END:
            return_value = bgEndCase(cmdList, false, BG);
            break;
            
        default:
            break;
    }

    reap_zombies();

    // printf("statuses %d, %d\n", return_value, STATUS(return_value));
    sprintf(exit_status, "%d", return_value);
    setenv("?", exit_status, 1);
    // printf("%d\n", return_value);
    return return_value;
}

void reap_zombies() {
    int child_status;
    int child_pid;
    while ((child_pid = waitpid(-1, &child_status, WNOHANG)) > 0)
        fprintf(stderr, "Completed: %d (%d)\n", child_pid, STATUS(child_status));
}

// simple statement with no redirection
int simpleCase(const CMD *cmdList, bool BG) {
    if (BG) {
        return background_process(cmdList);
    }

    int error;
    int fork_status = fork();
    if (fork_status == 0) {
        // redirect input if necessary
        if (cmdList->fromType != NONE || cmdList->toType != NONE) {
            if ((error = redirectionCase(cmdList)) != 0) {
                exit(error);
            }
        }
        
        if (execvp(cmdList->argv[0], cmdList->argv) < 0) {
            perror("execvp failed");
            exit(errno);
        }
        exit(0);
    } else if (fork_status < 0) {
        perror("fork failed");
        return errno;
    } 
    else {
        int child_status;
        signal(SIGINT, SIG_IGN);
        waitpid(fork_status, &child_status, 0);
        signal(SIGINT, SIG_DFL);
        return child_status;
    }
}

int cdCase(const CMD *cmdList, bool BG) {
    if (BG) {
        return background_process(cmdList);
    }    
    if (cmdList->argc > 2) {
        perror("too many arguments for cd");
        return 1;
    }

    if (cmdList->argv[1] == NULL) {
        if (getenv("HOME") == NULL) {
            perror("HOME does not exist");
            return 1;
        }
        else if (chdir(getenv("HOME")) < 0) {
            perror("chdir failed");
            return errno;
        }
    }
    else {
        DIR* dir = opendir(cmdList->argv[1]);
        if (dir == NULL) {
            perror("Directory does not exist");
            return errno;
        }
        else if (chdir(cmdList->argv[1]) < 0) {
            closedir(dir);
            perror("chdir failed");
            return errno;
        }
        closedir(dir);
    }
    return 0;
}

int pushdCase(const CMD *cmdList, bool BG) {
    if (BG) {
        return background_process(cmdList);
    }    
    // ensure command and directory stack set up correctly
    if (cmdList->argc != 2) {
        perror("pushd takes 1 argument");
        return 1;
    } 
    else if (directory_stack == NULL) {
        list_create();
    }

    // create new node, add current directory to it
    node* new = malloc(sizeof(node));
    if (new == NULL) {
        perror("malloc failed");
        return 1;
    }
    else if (getcwd(new->directory, sizeof(new->directory)) == NULL) {
        perror("getcwd failed");
        return errno;
    }

    // change directory, update cwd for printing
    char cwd[256];
    DIR* dir = opendir(cmdList->argv[1]);
    if (dir == NULL) {
        perror("pushd must be passed valid directory");
        free(new);
        return errno;
    }
    else if (chdir(cmdList->argv[1]) < 0) {
        closedir(dir);
        perror("chdir failed");
        free(new);
        return errno;
    }
    else if (getcwd(cwd, sizeof(cwd)) == NULL) {
        perror("getcwd failed");
        free(new);
        return errno;
    }
    closedir(dir);
    new->next = directory_stack->lst;
    directory_stack->lst = new;

    // print stack
    printf("%s", cwd);
    node* cursor = directory_stack->lst;
    while (cursor != NULL) {
        printf(" %s", cursor->directory);
        cursor = cursor->next;
    }
    printf("\n");
    return 0;
}

int popdCase(const CMD *cmdList, bool BG) {
    if (BG) {
        return background_process(cmdList);
    }    
    // ensure stack and commands setup properly
    if (directory_stack == NULL || directory_stack->lst == NULL) {
        perror("popd failed");
        return 1;
    }
    else if (cmdList->argc > 1) {
        perror("too many arguments for popd");
        return 1;
    }

    // change directory, update cwd for printing
    char cwd[256];
    if (chdir(directory_stack->lst->directory) < 0) {
        perror("chdir failed");
        return errno;
    }
    else if (getcwd(cwd, sizeof(cwd)) == NULL) {
        perror("getcwd failed");
        return errno;
    }

    // reassign pointers
    node* tmp = directory_stack->lst;
    directory_stack->lst = directory_stack->lst->next;
    free(tmp);

    // print stack
    printf("%s", cwd);
    node* cursor = directory_stack->lst;
    while (cursor != NULL) {
        printf(" %s", cursor->directory);
        cursor = cursor->next;
    }
    printf("\n");
    return 0;
}

// Case where there exists redirection in simple statement
int redirectionCase(const CMD *cmdList) {
    int new_stdin_fd = -1;
    int new_stdout_fd = -1;
    // open new input and output files if necessary.
    if (cmdList->fromType == RED_IN) {
        if ((new_stdin_fd = open(cmdList->fromFile, O_RDONLY)) < 0) {
            perror("open failed for read in");
            return errno;
        } 
        dup2(new_stdin_fd, 0);
    } 
    else if (cmdList->fromType == RED_IN_HERE) {
        char filename[14];
        strcpy(filename, "tmpfileXXXXXX");
        new_stdin_fd = mkstemp(filename);
        if (new_stdin_fd == -1 || write(new_stdin_fd, cmdList->fromFile, strlen(cmdList->fromFile)) == -1) {
            perror("HERE doc failed");
            return errno;
        }
        lseek(new_stdin_fd, 0, 0);
        dup2(new_stdin_fd, 0);
    }
    if (cmdList->toType == RED_OUT) {
        if ((new_stdout_fd = open(cmdList->toFile, O_WRONLY | O_CREAT | O_TRUNC, 420)) < 0) {
            perror("open failed");
            if (new_stdin_fd > -1)
                close(new_stdin_fd);
            return errno;
        }
        dup2(new_stdout_fd, 1);
    }
    else if (cmdList->toType == RED_OUT_APP) {
        if ((new_stdout_fd = open(cmdList->toFile, O_APPEND | O_WRONLY | O_CREAT, 420)) < 0) {
            perror("open failed");
            if (new_stdin_fd > -1)
                close(new_stdin_fd);
            return errno;
        }
        dup2(new_stdout_fd, 1);
    }
    
    if (new_stdin_fd > -1)
        close(new_stdin_fd);
    if (new_stdout_fd > -1)
        close(new_stdout_fd);
    return 0;
}

// Case where a pipe is used between commands
int pipeCase(const CMD *cmdList, bool BG) {
    if (BG) {
        return background_process(cmdList);
    }

    int fd[2], fork_status_left, fork_status_right, process_status_left, process_status_right;

    if (pipe(fd) < 0) {
        perror("pipe failed");
        return errno;
    }

    fork_status_left = fork();
    if (fork_status_left == 0) {
        // do child stuff for first command
        dup2(fd[1], 1);
        close(fd[0]);
        close(fd[1]);
        exit(process(cmdList->left));
    }
    else if (fork_status_left < 0) {
        perror("fork failed");
        return errno;
    }
    else {
        fork_status_right = fork();
        if (fork_status_right == 0) {
            // do child stuff for second command
            dup2(fd[0], 0);
            close(fd[0]);
            close(fd[1]);
            exit(process(cmdList->right));
        }
        else if (fork_status_right < 0) {
            perror("fork failed");
            return errno;
        }
        else {
            close(fd[0]);
            close(fd[1]);
            // handle parent process for first command
            signal(SIGINT, SIG_IGN);
            waitpid(fork_status_left, &process_status_left, 0);
            //handle parent process for second command.
            waitpid(fork_status_right, &process_status_right, 0);
            signal(SIGINT, SIG_DFL);
            // printf("status: %i, %i\n", STATUS(process_status_right), STATUS(process_status_left));
            if (STATUS(process_status_right) != 0) {
                // sprintf(exit_status, "%d", STATUS(process_status_right));
                // setenv("?", exit_status, 1);
                return process_status_right;
            }
            // sprintf(exit_status, "%d", STATUS(process_status_left));
            // setenv("?", exit_status, 1);
            return process_status_left;
        }
    }
}

// handle and logic for commands
int andOrCase(const CMD *cmdList, int And) {
    int fork_status = fork();
    if (fork_status == 0) {
        // successful fork, and case.
        if (And == 0) {
            if (process(cmdList->left) != 0 || process(cmdList->right) != 0) {
                setenv("?", "1", 1);
                exit(1);
            }
            else {
                setenv("?", "0", 1);
                exit(0);
            }
        }
        // successful fork, or case.
        else if (And == 1) {
            if (process(cmdList->left) == 0 || process(cmdList->right) == 0) {
                setenv("?", "0", 1);
                exit(0);
            }
            else {
                setenv("?", "1", 1);
                exit(1);
            }
        }
        perror("invalid call to andor");
        return 1;
    }
    else if (fork_status < 0) {
        // case where fork fails
        perror("fork failed");
        return errno;
    }
    else {
        // handle status for parent process
        int child_status;
        signal(SIGINT, SIG_IGN);
        waitpid(fork_status, &child_status, 0);
        signal(SIGINT, SIG_DFL);
        return child_status;
    }
}

int subcommandCase(const CMD *cmdList, bool BG) {
    if (BG) {
        return background_process(cmdList);
    }
    char exit_status[10];
    int error;

    int fork_status = fork();
    if (fork_status == 0) {
        if (cmdList->toType != NONE || cmdList->fromType != NONE) {
            if ((error = redirectionCase(cmdList)) != 0) {
                exit(error); 
            }
        }

        // call process with subcommand
        int process_status = process(cmdList->left);
        sprintf(exit_status, "%d", process_status);
        setenv("?", exit_status, 1);
        exit(process_status);
    }
    else if (fork_status < 0) {
        // fork failed, provide error
        perror("fork failed");
        return errno;
    }
    else {
        // handle status for parent process
        int child_status;
        signal(SIGINT, SIG_IGN);
        waitpid(fork_status, &child_status, 0);
        signal(SIGINT, SIG_DFL);
        return STATUS(child_status);
    }
}

int bgEndCase(const CMD *cmdList, bool BG, bool parentBG) {
    // printf("left: %d, %d\nright: %d, %d\n", cmdList->left->type, BG, cmdList->right->type, parentBG);
    realProcess(cmdList->left, BG);
    if (cmdList->right != NULL)
        return realProcess(cmdList->right, parentBG);
    return 0;
}

int background_process(const CMD *cmdList) {
    int fork_status = fork();
    if (fork_status == 0) {
        int exit_status = STATUS(process(cmdList));
        // printf("exited with %d\n", exit_status);
        exit(exit_status);
    }
    else if (fork_status < 0) {
        perror("fork failed");
        return errno;
    }
    else {
        fprintf(stderr, "Backgrounded: %d\n", fork_status);
    }
    return 0;
}

list* list_create() {
    list* new;
    if ((new = malloc(sizeof(list*))) == NULL) {
        perror("malloc failed");
        return NULL;
    }
    new->size = 0;
    new->lst = NULL;
    directory_stack = new;
    return new;
}
