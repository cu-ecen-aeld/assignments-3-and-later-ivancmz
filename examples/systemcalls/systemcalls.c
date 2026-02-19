#include "systemcalls.h"
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/wait.h>

/**
 * @param cmd the command to execute with system()
 * @return true if the command in @param cmd was executed
 *   successfully using the system() call, false if an error occurred,
 *   either in invocation of the system() call, or if a non-zero return
 *   value was returned by the command issued in @param cmd.
*/
bool do_system(const char *cmd)
{

/*
 * TODO  add your code here
 *  Call the system() function with the command set in the cmd
 *   and return a boolean true if the system() call completed with success
 *   or false() if it returned a failure
*/
    int result = system(cmd);
    if(result == -1)
    {
        int err = errno;
        fprintf(stderr, "Error: system() failed to execute command '%s': %s\n", cmd, strerror(err));
        return false;
    }
    if(cmd == NULL)
    {
        if(result == 0)
        {
            fprintf(stderr, "Error: system() no shell is available\n");
            return false;
        }
        fprintf(stdout, "Info: system() shell is available\n");
        return true;
    }
    return result == 0; 
}

/**
* @param count -The numbers of variables passed to the function. The variables are command to execute.
*   followed by arguments to pass to the command
*   Since exec() does not perform path expansion, the command to execute needs
*   to be an absolute path.
* @param ... - A list of 1 or more arguments after the @param count argument.
*   The first is always the full path to the command to execute with execv()
*   The remaining arguments are a list of arguments to pass to the command in execv()
* @return true if the command @param ... with arguments @param arguments were executed successfully
*   using the execv() call, false if an error occurred, either in invocation of the
*   fork, waitpid, or execv() command, or if a non-zero return value was returned
*   by the command issued in @param arguments with the specified arguments.
*/

bool do_exec(int count, ...)
{
    va_list args;
    va_start(args, count);
    char * command[count+1];
    int i;
    for(i=0; i<count; i++)
    {
        command[i] = va_arg(args, char *);
    }
    command[count] = NULL;
    // this line is to avoid a compile warning before your implementation is complete
    // and may be removed
    command[count] = command[count];

    va_end(args); // I move this line to ensure that the va_list is properly cleaned up after use

/*
 * TODO:
 *   Execute a system command by calling fork, execv(),
 *   and wait instead of system (see LSP page 161).
 *   Use the command[0] as the full path to the command to execute
 *   (first argument to execv), and use the remaining arguments
 *   as second argument to the execv() command.
 *
*/
    fflush(stdout); // Ensure all output is flushed before forking to avoid duplicate output in child process
    pid_t pid = fork();
    if(pid == -1)
    {
        int err = errno;
        fprintf(stderr, "Error: fork() failed: %s\n", strerror(err));
        return false;
    }
    if(pid == 0)
    {
        // Child process
        execv(command[0], command);
        int err = errno;
        fprintf(stderr, "Error: execv() failed to execute command '%s': %s\n", command[0], strerror(err));
        exit(1);
    }
    else if(pid > 0)
    {
        // Parent process
        int status;
        if (wait(&status) == -1) {
            int err = errno;
            fprintf(stderr, "Error: waitpid() failed: %s\n", strerror(err));
            return false;
        }
        if (WIFEXITED(status)) {
            return WEXITSTATUS(status) == 0; // true if command executed successfully
        }
        fprintf(stderr, "Error: Command '%s' did not terminate normally\n", command[0]);
        return false;
    }

    fprintf(stderr, "Error: Unexpected fork() return value: %d\n", pid);
    return false;
}

/**
* @param outputfile - The full path to the file to write with command output.
*   This file will be closed at completion of the function call.
* All other parameters, see do_exec above
*/
bool do_exec_redirect(const char *outputfile, int count, ...)
{
    va_list args;
    va_start(args, count);
    char * command[count+1];
    int i;
    for(i=0; i<count; i++)
    {
        command[i] = va_arg(args, char *);
    }
    command[count] = NULL;
    // this line is to avoid a compile warning before your implementation is complete
    // and may be removed
    command[count] = command[count];

    va_end(args); // I move this line to ensure that the va_list is properly cleaned up after use
/*
 * TODO
 *   Call execv, but first using https://stackoverflow.com/a/13784315/1446624 as a refernce,
 *   redirect standard out to a file specified by outputfile.
 *   The rest of the behaviour is same as do_exec()
 *
*/
// Source - https://stackoverflow.com/a/13784315
// Posted by tmyklebu, modified by community. See post 'Timeline' for change history
// Retrieved 2026-02-19, License - CC BY-SA 3.0

    int fd = open(outputfile, O_WRONLY|O_TRUNC|O_CREAT, 0644);
    if (fd < 0) 
    { 
        int err = errno;
        fprintf(stderr, "Error: could not create file '%s': %s\n", outputfile, strerror(err)); 
        return 1;
    }
    
    fflush(stdout); // Ensure all output is flushed before forking to avoid duplicate output in child process
    pid_t pid = fork();
    if(pid == -1)
    {
        int err = errno;
        fprintf(stderr, "Error: fork() failed: %s\n", strerror(err));
        return false;
    }
    if(pid == 0)
    {
        // Child process
        int ret = dup2(fd, 1);
        int err = errno;
        close(fd);

        if (ret < 0)
        { 
            fprintf(stderr, "Error: dup2() failed: %s\n", strerror(err));
            exit(1);
        }

        execv(command[0], command);
        err = errno;
        fprintf(stderr, "Error: execv() failed to execute command '%s': %s\n", command[0], strerror(err));
        exit(1);
    }
    else if(pid > 0)
    {
        // Parent process
        close(fd);
        int status;
        if (wait(&status) == -1) {
            int err = errno;
            fprintf(stderr, "Error: waitpid() failed: %s\n", strerror(err));
            return false;
        }
        if (WIFEXITED(status)) {
            return WEXITSTATUS(status) == 0; // true if command executed successfully
        }
        fprintf(stderr, "Error: Command '%s' did not terminate normally\n", command[0]);
        return false;
    }

    close(fd);
    fprintf(stderr, "Error: Unexpected fork() return value: %d\n", pid);
    return false;

}
