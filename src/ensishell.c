/*****************************************************
 * Copyright Grégory Mounié 2008-2015                *
 *           Simon Nieuviarts 2002-2009              *
 * This code is distributed under the GLPv3 licence. *
 * Ce code est distribué sous la licence GPLv3+.     *
 *****************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "variante.h"
#include "readcmd.h"
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <glob.h>
#include <wordexp.h>


#ifndef VARIANTE
#error "Variante non défini !!"
#endif

/* Guile (1.8 and 2.0) is auto-detected by cmake */
/* To disable Scheme interpreter (Guile support), comment the
 * following lines.  You may also have to comment related pkg-config
 * lines in CMakeLists.txt.
 */

#if USE_GUILE == 1
#include <libguile.h>

void execute_cmd(struct cmdline *l);
int question6_executer(char *line)
{
	/* Question 6: Insert your code to execute the command line
	 * identically to the standard execution scheme:
	 * parsecmd, then fork+execvp, for a single command.
	 * pipe and i/o redirection are not required.
	 */

	/* Remove this line when using parsecmd as it will free it */
	struct cmdline *l = parsecmd(&line);
	execute_cmd(l);
	return 0;
}

SCM executer_wrapper(SCM x)
{
        return scm_from_int(question6_executer(scm_to_locale_stringn(x, 0)));
}
#endif
/* background processes*/
struct job{
	pid_t pid;
	char* cmd;
	bool done;
	struct job *next;
};

struct job *jobs = NULL;



void show_jobs(){
    struct job *temp = jobs;
	printf("pid\tcmd \n");
    while(temp != NULL){
		if(!temp->done){
			printf("%i\t%s \n", temp->pid, temp->cmd);
		}
        temp = temp->next;
    }
}


void insert_job(pid_t pid, char* cmd){
    struct job* new_job = malloc(sizeof(struct job));
    new_job->pid = pid;
    new_job->cmd = strdup(cmd);
    new_job->next = NULL;

    if(jobs == NULL) {
        jobs = new_job;
        return;
    }
    struct job* temp = jobs;
    while(temp->next != NULL){
        temp = temp->next;
    }
    temp->next = new_job;
}



void remove_job(pid_t pid){
	struct job *temp = jobs;
	while(temp != NULL){
		if(temp->pid == pid){
			temp->done = true;
			return;
		}
		temp = temp->next;
	}
}
void check_and_remove_finished_jobs(){
	pid_t pid;
	while ((pid = waitpid(-1, NULL, WNOHANG)) > 0) {
        remove_job(pid);
    }
}

void terminate(char *line) {
#if USE_GNU_READLINE == 1
	/* rl_clear_history() does not exist yet in centOS 6 */
	clear_history();
#endif
	if (line)
	  free(line);
	printf("exit\n");
	exit(0);
}

int length(char*** seq){
	int counter = 0;
	for(int i = 0; seq[i]; i++){
		counter++;
	}
	return counter;
}

char** expand_jokers(struct cmdline *l){
	wordexp_t w;
	char** args = l->seq[0];
	for(int i = 0;args[i]; i++){
		int flag;
		if(!i){
			flag = 0;
		}
		else{
			flag = WRDE_APPEND;
		}
		int status = wordexp(args[i], &w, flag);
		if(status != 0){
			printf("error : cannot expand joker");
			exit(1);
		}
	}
	return w.we_wordv;
}


void execute_cmd(struct cmdline *l) {
    pid_t pid;
    char* cmd = NULL;
    char** args = NULL;
    int status;
    int fd[2];
    int len = length(l->seq);
    int previous[2] = {0, 0};
    char** seq = expand_jokers(l);
    l->seq[0] = seq;
	/* pids of all child processes */
    pid_t childs[len];
    for(int i = 0; i < len; i++) {
        cmd = l->seq[i][0];
        args = l->seq[i];
        pipe(fd);
        pid = fork();
        if (pid == -1) {
            printf("error \n");
            exit(1);
        } else if (pid == 0) {
            if(strcmp(cmd, "jobs") == 0){
				/* printing running jobs*/
                show_jobs();
                exit(0);
            }
            close(fd[0]); 
            if (i != 0) {
                dup2(previous[0], 0); 
                close(previous[0]); 
            } else if(l->in){
                int input_fd = open(l->in, O_RDONLY);
                dup2(input_fd, 0);
                close(input_fd);
            }
            /*checking if another command exists*/
            if (l->seq[i+1] != NULL) {
                dup2(fd[1], 1); // redirecting the output of the commande to the next pipe
            } else if(l->out){
                int output_fd = open(l->out, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH) ;
                ftruncate(output_fd, 0);
                dup2(output_fd, 1);
                close(output_fd);
            } else if(i == len-1 && l->out) {
                int output_fd = open(l->out, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH) ;
                ftruncate(output_fd, 0);
                dup2(output_fd, 1);
                close(output_fd);
            }
            execvp(cmd, args);
            exit(1);
        } else {
            /*parent*/
            close(fd[1]); 
            if (previous[0] != 0) {
                close(previous[0]);
            }
            previous[0] = fd[0]; // saving it for the next command
            if (!l->bg) {
                childs[i] = pid;
            } else {
                insert_job(pid, cmd);
            }
        }
    }
    if (!l->bg) {
        for (int i = 0; i < len; i++) {
            waitpid(childs[i], &status, 0);
        }
    }/* removing finished jobs */
    check_and_remove_finished_jobs();
}




int main() {
        printf("Variante %d: %s\n", VARIANTE, VARIANTE_STRING);

#if USE_GUILE == 1
        scm_init_guile();
        /* register "executer" function in scheme */
        scm_c_define_gsubr("executer", 1, 0, 0, executer_wrapper);
#endif

	while (1) {
		struct cmdline *l;
		char *line=0;
		// int i, j;
		char *prompt = "ensishell>";

		/* Readline use some internal memory structure that
		   can not be cleaned at the end of the program. Thus
		   one memory leak per command seems unavoidable yet */
		line = readline(prompt);
		if (line == 0 || ! strncmp(line,"exit", 4)) {
			terminate(line);
		}
		
		
		// execute_cmd(line);

#if USE_GNU_READLINE == 1
		add_history(line);
#endif


#if USE_GUILE == 1
		/* The line is a scheme command */
		if (line[0] == '(') {
			char catchligne[strlen(line) + 256];
			sprintf(catchligne, "(catch #t (lambda () %s) (lambda (key . parameters) (display \"mauvaise expression/bug en scheme\n\")))", line);
			scm_eval_string(scm_from_locale_string(catchligne));
			free(line);
                        continue;
                }
#endif

		/* parsecmd free line and set it up to 0 */
		l = parsecmd( & line);

		/* If input stream closed, normal termination */
		if (!l) {
		  
			terminate(0);
		}
		

		
		if (l->err) {
			/* Syntax error, read another command */
			printf("error: %s\n", l->err);
			continue;
		}

		if (l->in) printf("in: %s\n", l->in);
		if (l->out) printf("out: %s\n", l->out);
		if (l->bg) printf("background (&)\n");
		execute_cmd(l);
		/* Display each command of the pipe */
		// for (i=0; l->seq[i]!=0; i++) {
		// 	char **cmd = l->seq[i];
		// 	printf("seq[%d]: ", i);
        //                 for (j=0; cmd[j]!=0; j++) {
        //                         printf("'%s' ", cmd[j]);
        //                 }
		// 	printf("\n");
		// }
	}

}
