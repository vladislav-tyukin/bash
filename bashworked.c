#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>


//sudo apt install libreadline-devС
//cc bashworked.c -lreadline -lhistory
#include <readline/readline.h>
#include <readline/history.h>

#define BUF_MAX 256

enum Type{
    LOGIC /* && ; ||*/, OPERATION /*date/ls*/, REDIRECT, PIPE
};

enum ExecutionType{
    FOREGROUND, BACKGROUND
};

typedef struct job {
    pid_t pid;
    char* command;
    struct job* next;
} job;


job* create_job(pid_t pid, const char* command) {
    job* new_job = (job*)malloc(sizeof(job));
    if (new_job == NULL) {
        perror("malloc failed");
        exit(EXIT_FAILURE); 
    }
    new_job->pid = pid;
    new_job->command = strdup(command);
    new_job->next = NULL;
    return new_job;
}

int push_job(job** jobs, pid_t pid, const char* command) {
    job* tmp = create_job(pid, command); 
    if (*jobs == NULL) {
        *jobs = tmp;  
    } else {
        job* current = *jobs;
        while (current->next != NULL) {
            current = current->next; 
        }
        current->next = tmp;
    }
    return 0;
}


job* jobs = NULL;

int delete_job(job** jobs, pid_t pid) {
    if (*jobs == NULL) {
        return -1;  
    }

    job* head = *jobs;
    job* tmp = *jobs;
    job* prev = NULL;

    
    while (tmp != NULL && tmp->pid != pid) {
        prev = tmp;
        tmp = tmp->next;
    }

    if (tmp == NULL) {
        return -1;
    }

    if (tmp == head) {
        *jobs = head->next;
    } else {
        prev->next = tmp->next;
    }

    free(tmp);
    return 0;
}

void print_jobs(job* jobs){
    printf("PID | NAME \n");
    while (jobs != NULL){
        printf("%d | %s \n", jobs->pid, jobs -> command);
        jobs = jobs -> next;
  }
  return;
}



void print_prompt() {
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        write(STDOUT_FILENO, "\033[01;35mshell: \033[0m\033[01;37m", 27);
        write(STDOUT_FILENO, cwd, strlen(cwd));
        write(STDOUT_FILENO, "\033[0m$ ", 6);
        printf(">");
    } else {
        perror("getcwd() error");
    }
    fflush(stdout);  
}   




void handle_signal(int sig) {
    printf("signal ignored");
    fflush(stdout);
}


void handle_sigchld(int sig) {
    while (waitpid(-1, NULL, WNOHANG) > 0);
}


void setup_signal_handlers() {
    struct sigaction sa;
    
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;

    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("sigaction SIGINT");
        exit(EXIT_FAILURE);
    }
    if (sigaction(SIGTSTP, &sa, NULL) == -1) {
        perror("sigaction SIGTSTP");
        exit(EXIT_FAILURE);
    }
    if (sigaction(SIGQUIT, &sa, NULL) == -1) {
        perror("sigaction SIGQUIT");
        exit(EXIT_FAILURE);
    }
     sa.sa_handler = handle_sigchld;
    sigaction(SIGCHLD, &sa, NULL);
}



void reset_signal_handlers() {
    struct sigaction sa;
    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTSTP, &sa, NULL);
    sigaction(SIGQUIT, &sa, NULL);
}






typedef struct node{
    enum Type type;
    enum ExecutionType execution;
    char* op;
    char* command;
    struct node* left;
    struct node* right;
}node;

node* create_node(enum Type type, enum ExecutionType execution, const char* op, const char* command, node* left, node* right){
    node* tmp = (node*)malloc(sizeof(node));
    tmp -> type = type;
    tmp -> execution = execution;
    tmp -> op = op ? strdup(op) : NULL;
    tmp -> command = command ? strdup(command) : NULL;
    tmp -> left = left;
    tmp -> right = right;
    return tmp;

}

/*

    &&
    ||
    ;
    >
    <
    ""
    */

/*
prioritets:
bolee nizkiy vishe, bolee visokiy nizhee
"" ''
<, >, <<, >>
|
&&, ||
;
&
*/


node* parse_sequence_expr(int* i, char** tokens);
node* parse_or_expr(int* i, char** tokens);
node* parse_and_expr(int* i, char** tokens);
node* parse_pipe_expr(int* i, char** tokens);
node* parse_redirect_expr(int* i, char** tokens);
node* parse_command_expr(int* i, char** tokens);

node* parse_sequence_expr(int* i, char** tokens) {
    node* left = parse_or_expr(i, tokens);
    while (tokens[*i] != NULL && strcmp(tokens[*i], ";") == 0) {
        (*i)++;
        node* right = parse_sequence_expr(i, tokens);
        left = create_node(LOGIC, FOREGROUND, ";", NULL, left, right);
    }
    return left;
}

node* parse_or_expr(int* i, char** tokens) {
    node* left = parse_and_expr(i, tokens);
    while (tokens[*i] != NULL && strcmp(tokens[*i], "||") == 0) {
        (*i)++;
        node* right = parse_or_expr(i, tokens);
        left = create_node(LOGIC, FOREGROUND, "||", NULL, left, right);
    }
    return left;
}

node* parse_and_expr(int* i, char** tokens) {
    node* left = parse_pipe_expr(i, tokens);
    while (tokens[*i] != NULL && strcmp(tokens[*i], "&&") == 0) {
        (*i)++;
        node* right = parse_and_expr(i, tokens);
        left = create_node(LOGIC, FOREGROUND, "&&", NULL, left, right);
    }
    return left;
}

node* parse_pipe_expr(int* i, char** tokens) {
    node* left = parse_redirect_expr(i, tokens);
    while (tokens[*i] != NULL && strcmp(tokens[*i], "|") == 0) {
        char* op = strdup(tokens[*i]);
        (*i)++;
        node* right = parse_redirect_expr(i, tokens);
        left = create_node(PIPE, FOREGROUND, op, NULL, left, right);
    }
    return left;
}

int is_redirect(const char* c) {
    return !strcmp(c, ">") || !strcmp(c, "<") || !strcmp(c, ">>") || !strcmp(c, "<<");
}

node* parse_redirect_expr(int *i, char** tokens) {
    node* left = parse_command_expr(i, tokens);
    while (tokens[*i] != NULL && is_redirect(tokens[*i])) {
        char* op = strdup(tokens[*i]);
        (*i)++;
        node* right = parse_command_expr(i, tokens);
        left = create_node(REDIRECT, FOREGROUND, op, NULL, left, right);
    }
    return left;
}

node* parse_command_expr(int* i, char** tokens) {
    if (tokens[*i] == NULL) return NULL;
    char* command = strdup(tokens[*i]);
    (*i)++;
    
    while (tokens[*i] != NULL && tokens[*i][0] != '&' && tokens[*i][0] != '|' &&
           tokens[*i][0] != ';' && !is_redirect(tokens[*i])) {
        command = realloc(command, strlen(command) + strlen(tokens[*i]) + 2);
        strcat(command, " ");
        strcat(command, tokens[*i]);
        (*i)++;
    }

    node* command_node = create_node(OPERATION, FOREGROUND, NULL, command, NULL, NULL);

   
    if (tokens[*i] != NULL && !strcmp(tokens[*i], "&")) {
        command_node-> execution = BACKGROUND;
        (*i)++;
    }

    return command_node;
}



node* parse(char** tokens) {
    int i = 0;
    return parse_sequence_expr(&i, tokens);
}






int execute_tree(node* root);
int execute_pipe(node* root){
    int save_stdin;
    int save_stdout;
    save_stdin = dup(STDIN_FILENO);
    save_stdout = dup(STDOUT_FILENO);
    int pipefd[2];
    
    
    if (pipe(pipefd) != 0){
        perror("execute pipe error");
    }
    
    pid_t cpid = fork();
    int status;   
    
    if (cpid == 0){ // child
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);
        execute_tree(root->left);
        exit(1);
    
    } else { // parent
        close(pipefd[1]);
        dup2(pipefd[0], STDIN_FILENO);  
        close(pipefd[0]);
        execute_tree(root->right);
        waitpid(cpid, &status, 0);
        dup2(save_stdin, STDIN_FILENO);
        dup2(save_stdout, STDOUT_FILENO);
        return status;
      }
}


int execute_command(node* root) {
    if (root == NULL) {
        fprintf(stderr, "root == null/n");
        return -1;
    }

    if (root->type == OPERATION) {
        char* args[BUF_MAX];
        char* token = strtok(root->command, " ");
        int index = 0;
        while (token != NULL) {
            args[index++] = token;
            token = strtok(NULL, " ");
        }
        args[index] = NULL;

        if ((strcmp(args[0], "exit") == 0) || (strcmp(args[0], "q") == 0)) {
            printf("end bash-on-c...\n");
            exit(0);
        }

        if (strcmp(args[0], "cd") == 0) {
            const char* path = args[1] ? args[1] : getenv("HOME");
            if (chdir(path) != 0) {
                perror("error cd");
            }
            return 0;
        }
        
        if(strcmp(args[0], "ps") == 0){
            print_jobs(jobs);
            return 0; 
        }

        if (strcmp(args[0], "kill") == 0) {
            if (args[1] == NULL) {
                fprintf(stderr, "kill error no pid\n");
                return -1;
            }
                pid_t pid = atoi(args[1]);
                kill(pid, SIGKILL);
                delete_job(&jobs, pid);
                return 0;
        }

    if (strcmp(args[0], "fg") == 0) {
        pid_t pid = atoi(args[1]);  
        if (pid > 0) {
            kill(pid, SIGCONT);        
            int status;
            waitpid(pid, &status, 0);
            delete_job(&jobs, pid);
            return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    }         // проверяет завершился ли нормально процесс /  извлекает код возврата завершившегося процесса из переменной / -1 в другом случае
        return -1; 
    }


        

        pid_t pid = fork();
        if (pid == 0) {
            if (root->execution == BACKGROUND) {
                int dev_null = open("/dev/null", O_RDWR);
                if (dev_null >= 0) {
                    dup2(dev_null, STDIN_FILENO);
                    dup2(dev_null, STDOUT_FILENO);
                    dup2(dev_null, STDERR_FILENO);
                    close(dev_null);
                }
            }
            reset_signal_handlers();
            execvp(args[0], args);
            perror("Ошибка execvp");
            exit(EXIT_FAILURE);
        } else if (pid < 0) {
            perror("Ошибка fork");
            return -1;
        } else {
            if (root->execution == BACKGROUND) {
                push_job(&jobs, pid, root->command);
            } else {
                int status;
                waitpid(pid, &status, 0);
                if (WIFEXITED(status)) {
                    return WEXITSTATUS(status);
                } else {
                    return -1;
                }
            }
        }
    
     } else if (root->type == REDIRECT) {
        int fd;
        if (strcmp(root->op, ">") == 0) {
            fd = open(root->right->command, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd == -1) {
                perror("open for writing execute command error");
                
            }
            if (fork() == 0) {
                dup2(fd, STDOUT_FILENO);
                close(fd);
                execute_command(root->left);
                exit(1);
            } else {
                wait(NULL);
                close(fd);
            }
        } else if (strcmp(root->op, ">>") == 0) {
            fd = open(root->right->command, O_WRONLY | O_CREAT | O_APPEND, 0644);
            if (fd == -1) {
                perror("open for adding execute command error");
                
            }
            if (fork() == 0) {
                dup2(fd, STDOUT_FILENO);
                close(fd);
                execute_command(root->left);
                exit(1);
            } else {
                wait(NULL);
                close(fd);
            }
        } else if (strcmp(root->op, "<") == 0) {
            fd = open(root->right->command, O_RDONLY);
            if (fd == -1) {
                perror("open for reading execute command error");
                
            }
            if (fork() == 0) {
                dup2(fd, STDIN_FILENO);
                close(fd);
                execute_command(root->left);
                exit(1);
            } else {
                wait(NULL);
                close(fd);
            }
        }
}


}
    



int execute_tree(node* root) {
    if (root == NULL) return 0;

    if (root->type == LOGIC) {
        int left_stat = execute_tree(root->left);
        
        if (!strcmp(root->op, "&&")) {
            if (left_stat == 0) {
                return execute_tree(root->right);
            } else {
                return left_stat; 
            }
        } else if (!strcmp(root->op, "||")) {
            if (left_stat != 0) { 
                return execute_tree(root->right);
            } else {    
                return left_stat;
            }
        } else if (!strcmp(root->op, ";")) {
            execute_tree(root->right); 
         
        }
    } else if (root->type == REDIRECT) {
        return execute_command(root);
    } else if (root->type == OPERATION) {
        return execute_command(root);
    } else if (root->type == PIPE) {
        return execute_pipe(root);
    }

    return 0;
}


int is_single_operator(char c) {
    return c == '|' || c == '&' || c == ';' || c == '>' || c == '<';
}

int is_double_operator(const char *s, int i) {
    return (s[i] == '&' && s[i+1] == '&') || 
           (s[i] == '|' && s[i+1] == '|') || 
           (s[i] == '>' && s[i+1] == '>') || 
           (s[i] == '<' && s[i+1] == '<');
}

char** split_bash(const char* s) {
    int tmp_size = BUF_MAX, tmp_i = 0;
    char* tmp = malloc(tmp_size);
    char** array = NULL;
    int array_i = 0;
    int in_quotes = 0;
    int length = strlen(s);

    for (int i = 0; i < length; i++) {
        // пропускаем пробелы, если мы не внутри строки с кавычками
        if (s[i] == ' ' && !in_quotes) {
            if (tmp_i > 0) {
                tmp[tmp_i] = '\0';
                array = (char**)realloc(array, (array_i + 1) * sizeof(char*));
                array[array_i] = strdup(tmp);
                array_i++;
                tmp_i = 0;
            }
            continue;
        }

        // обработка кавычек
        if (s[i] == '\'' || s[i] == '"') {
            in_quotes = in_quotes == 0 ? s[i] : 0;
            continue;
        }

        // обработка двойных операторов
        if (!in_quotes && (s[i] == '&' && s[i+1] == '&' || s[i] == '|' && s[i+1] == '|' || s[i] == '>' && s[i+1] == '>' || s[i] == '<' && s[i+1] == '<')) {
            if (tmp_i > 0) {
                tmp[tmp_i] = '\0';
                array = (char**)realloc(array, (array_i + 1) * sizeof(char*));
                array[array_i] = strdup(tmp);
                array_i++;
                tmp_i = 0;
            }
            tmp[0] = s[i];
            tmp[1] = s[i+1];
            tmp[2] = '\0';
            i++;  // пропускаем второй символ оператора
            array = (char**)realloc(array, (array_i + 1) * sizeof(char*));
            array[array_i] = strdup(tmp);
            array_i++;
            continue;
        }

        // обработка одиночных операторов
        if (!in_quotes && (s[i] == '|' || s[i] == '&' || s[i] == ';' || s[i] == '>' || s[i] == '<')) {
            if (tmp_i > 0) {
                tmp[tmp_i] = '\0';
                array = (char**)realloc(array, (array_i + 1) * sizeof(char*));
                array[array_i] = strdup(tmp);
                array_i++;
                tmp_i = 0;
            }
            tmp[0] = s[i];
            tmp[1] = '\0';
            array = (char**)realloc(array, (array_i + 1) * sizeof(char*));
            array[array_i] = strdup(tmp);
            array_i++;
            continue;
        }

        tmp[tmp_i++] = s[i];
        if (tmp_i >= tmp_size) {
            tmp_size += BUF_MAX;
            tmp = (char*)realloc(tmp, tmp_size);
        }
    }

    if (tmp_i > 0) {
        tmp[tmp_i] = '\0';
        array = (char**)realloc(array, (array_i + 1) * sizeof(char*));
        array[array_i] = strdup(tmp);
        array_i++;
    }

    array = (char**)realloc(array, (array_i + 1) * sizeof(char*));
    array[array_i] = NULL;
    free(tmp);
    return array;
}


char* readline_input() {
    int n = BUF_MAX, i = 0, c;
    char* buf = (char*)malloc(n * sizeof(char));

    while ((c = getchar()) != '\n') {
        if (i == n - 1) {
            n *= 2;
            buf = (char*)realloc(buf, n * sizeof(char));
        }
        buf[i++] = c;
    }

    buf[i] = '\0';
    return buf;
}





void print_tree(node* root) {
    if (root == NULL) return;

    if (root->type == LOGIC || root->type == REDIRECT) {
        printf("(");
        print_tree(root->left);
        printf(" %s ", root->op);
        print_tree(root->right);
        printf(")");
    } else if (root->type == OPERATION) {
        printf("%s", root->command);
    }
}

void free_tree(node* root) {
    if (root == NULL) return;
    free_tree(root->left);
    free_tree(root->right);
    free(root->op);
    free(root->command);
    free(root);
}



int main() {
    read_history("history.txt");
    jobs = create_job(getpid(), "bash");
    setup_signal_handlers();
    signal(SIGCHLD, handle_sigchld);

    while (1) {
        char cwd[PATH_MAX];
        if (getcwd(cwd, sizeof(cwd)) != NULL) {
            printf("\033[01;35mshell: \033[0m\033[01;37m%s\033[0m$ ", cwd); 
        } else {
            perror("getcwd() error"); 
            continue; 
        }
        char* input = readline("> ");
        if (input == NULL){
            break; 
        }
        if (*input) {
            add_history(input);
            append_history(1, "history.txt");
        }
        char** result = split_bash(input);

        /*for (int i = 0; result[i] != NULL; i++) {
            printf("[%s]\n", result[i]);
            free(result[i]);
        }*/



        node* tree = parse(result);
        //print_tree(tree);
        execute_tree(tree);
        free_tree(tree);
        free(result);
        free(input);
    }

    return 0;
}