#include <iostream>
#include <vector>
#include <glob.h>
#include <fstream>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <sys/wait.h>
#include <signal.h>

using namespace std;

#define MAX_BUF_SIZE 128

int executeCommand(char **tokens, int tokenCount);
int executeExternalCommand(char **tokens, int tokenCount, int in_fd, int out_fd);
char **parsePipes(char *line, int *numCommands);
char **parseCommands(char *command, int *tokenCount);
vector<string> expandGlob(const string &pattern);
int changeDirectory(char **tokens);
int exitWithError(char **s);

bool ctrlC = false, ctrlZ = false;
int currentProcessGroup = 0;
int wildcard = 0;
char homeDirectory[MAX_BUF_SIZE];

char *builtinCommands[] = {
    (char *)"cd",
    (char *)"exit",
};

int (*builtinFunctions[])(char **) =
    {
        &changeDirectory,
        &exitWithError
    };

void interruptHandler(int signo)
{
    if (signo == SIGINT)
    {
        // do nothing
    }
}

int main(int argc, char **argv)
{
    ofstream file2;
    file2.open("history.txt", ios::app | ios::out);
    getcwd(homeDirectory, MAX_BUF_SIZE);
    strcat(homeDirectory, "/history.txt");
    read_history("history.txt");
    signal(SIGINT, interruptHandler);

    char *commandLine = NULL;
    size_t n = 0;
    char **commands;
    char **tokens;
    int numCommands = 0;
    int tokenCount = 0;
    int status;

    do
    {
        wildcard = 0;

        commandLine = readline("\nshell>");

        commands = parsePipes(commandLine, &numCommands);
        if (file2.is_open())
        {
            file2 << commandLine << endl;
        }

        if (numCommands == 1)
        {
            tokens = parseCommands(commandLine, &tokenCount);

            if (tokens != NULL && tokenCount > 0)
                status = executeCommand(tokens, tokenCount);
        }
        else if (numCommands > 1)
        {
            int fd[2];
            int in_fd = 0;
            int pipeError = 0;
            for (int i = 0; i < numCommands - 1; i++)
            {
                if (pipe(fd) == -1)
                {
                    pipeError = 1;
                    break;
                }
                else
                {
                    tokens = parseCommands(commands[i], &tokenCount);
                    if (tokenCount > 0)
                    {
                        status = executeExternalCommand(tokens, tokenCount, in_fd, fd[1]);
                    }
                    else
                    {
                        pipeError = 1;
                        break;
                    }
                    close(fd[1]);
                    in_fd = fd[0];
                }
            }
            if (!pipeError)
            {
                tokens = parseCommands(commands[numCommands - 1], &tokenCount);
                if (tokenCount > 0)
                {
                    status = executeExternalCommand(tokens, tokenCount, in_fd, 1);
                }
                else
                {
                    pipeError = 1;
                    break;
                }
            }
        }
        free(tokens);
        free(commandLine);
        fflush(stdin);
        fflush(stdout);
    } while (status == EXIT_SUCCESS);

    return 0;
}

vector<string> expandGlob(const string &pattern)
{
    using namespace std;
    glob_t glob_result;
    glob(pattern.c_str(), GLOB_TILDE, NULL, &glob_result);
    vector<string> result;
    for (unsigned int i = 0; i < glob_result.gl_pathc; ++i)
    {
        result.push_back(string(glob_result.gl_pathv[i]));
    }
    globfree(&glob_result);
    return result;
}

int executeCommand(char **tokens, int tokenCount)
{
    if (tokens[0] == NULL)
    {
        return EXIT_SUCCESS;
    }
    else
    {
        for (int i = 0; i < 2; i++)
        {
            if (strcmp(tokens[0], builtinCommands[i]) == 0)
            {
                return (*builtinFunctions[i])(tokens);
            }
        }
        return executeExternalCommand(tokens, tokenCount, 0, 1);
    }
}

int executeExternalCommand(char **tokens, int tokenCount, int in_fd, int out_fd)
{
    pid_t childPID, waitPID;
    int status;
    int i = 0;
    childPID = fork();
    char *token;

    if (childPID == 0)
    {
        if (wildcard == 1)
        {
            vector<string> files;
            i = 0;
            printf("Wildcard is 1\n");
            files = expandGlob(tokens[1]);
            for (i = 0; i < files.size(); i++)
            {
                tokens[i + 1] = (char *)files[i].c_str();
            }
            tokenCount = files.size() + 1;
        }
        if (in_fd != 0)
        {
            dup2(in_fd, 0);
            close(in_fd);
        }
        if (out_fd != 1)
        {
            dup2(out_fd, 1);
            close(out_fd);
        }
        int commandIndex = 0;
        int op = 0;

        for (int i = 0; i < tokenCount; i++)
        {
            token = tokens[i];
            if (!op)
            {
                if (strcmp(token, "&") == 0 || strcmp(token, ">") == 0 || strcmp(token, "<") == 0)
                {
                    op = 1;
                    commandIndex = i;
                }
            }
            if (strcmp(token, ">") == 0)
            {
                int redirect_out_fd = open(tokens[i + 1], O_CREAT | O_TRUNC | O_WRONLY, 0666);
                dup2(redirect_out_fd, STDOUT_FILENO);
            }
            if (strcmp(token, "<") == 0)
            {
                int redirect_in_fd = open(tokens[i + 1], O_RDONLY);
                dup2(redirect_in_fd, STDIN_FILENO);
            }
        }
        if (!op)
            commandIndex = tokenCount;

        char **token2 = (char **)malloc((commandIndex + 1) * sizeof(char *));

        if (token2 == NULL)
        {
            fprintf(stderr, "Error: Allocation error\n");
            exit(EXIT_FAILURE);
        }

        int j;
        for (j = 0; j < commandIndex; j++)
        {
            token2[j] = tokens[j];
        }
        token2[j] = NULL;
        if (execvp(token2[0], token2) == -1)
        {
            fprintf(stderr, "Error: Execution failed\n");
            exit(EXIT_FAILURE);
        }
    }
    else if (childPID < 0)
    {
        fprintf(stderr, "Error: Fork failed\n");
        exit(EXIT_FAILURE);
    }

    if (strcmp(tokens[tokenCount - 1], "&") != 0)
    {
        do
        {
            waitPID = waitpid(childPID, &status, WUNTRACED);
        } while (!WIFEXITED(status) && !WIFSIGNALED(status));
    }

    return EXIT_SUCCESS;
}

char **parsePipes(char *line, int *numCommands)
{
    int bufsize = MAX_BUF_SIZE;
    char **commands = (char **)malloc(bufsize * sizeof(char *));
    int lenofcmd = strlen(line);
    int k = 0;
    *numCommands = 0;
    int cmdIndex = 0;
    unsigned char pipeChar = '|';
    unsigned char doubleQuoteChar = '\"';
    unsigned char singleQuoteChar = '\'';

    if (commands == NULL)
    {
        fprintf(stderr, "Error: Allocation error\n");
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < bufsize; i++)
    {
        commands[i] = (char *)malloc(lenofcmd * sizeof(char));
        if (!commands[i])
        {
            fprintf(stderr, "Error: Allocation error\n");
            exit(EXIT_FAILURE);
        }
    }

    for (int i = 0; i < lenofcmd;)
    {
        if (line[i] == pipeChar)
        {
            if (strlen(commands[cmdIndex]) <= 0)
            {
                fprintf(stderr, "Error: Syntax ERROR\n");
                *numCommands = 0;
                return NULL;
            }
            cmdIndex++;
            if (cmdIndex >= bufsize)
            {
                bufsize += MAX_BUF_SIZE;
                commands = (char **)realloc(commands, bufsize * sizeof(char *));
                if (commands == NULL)
                {
                    fprintf(stderr, "Error: Allocation error\n");
                    exit(EXIT_FAILURE);
                }
            }
            k = 0;
            i++;
        }
        else if (line[i] == doubleQuoteChar)
        {
            commands[cmdIndex][k] = line[i];
            i++;
            k++;
            while (line[i] != doubleQuoteChar && i < lenofcmd)
            {
                commands[cmdIndex][k] = line[i];
                i++;
                k++;
            }
            commands[cmdIndex][k] = line[i];
            i++;
            k++;
        }
        else if (line[i] == singleQuoteChar)
        {
            commands[cmdIndex][k] = line[i];
            i++;
            k++;
            while (line[i] != singleQuoteChar && i < lenofcmd)
            {
                commands[cmdIndex][k] = line[i];
                i++;
                k++;
            }
            commands[cmdIndex][k] = line[i];
            i++;
            k++;
        }
        else
        {
            commands[cmdIndex][k] = line[i];
            i++;
            k++;
        }
    }
    *numCommands = cmdIndex + 1;
    commands[*numCommands] = NULL;
    return commands;
}

char **parseCommands(char *command, int *tokenCount)
{
    *tokenCount = 0;
    int bufsize = MAX_BUF_SIZE;
    int cmdLen = strlen(command);
    int i, j, cmdIndex = 0;
    char spaceChar = ' ';
    char newlineChar = '\n';
    char singleQuoteChar = '\'';

    char **tokens = (char **)malloc(bufsize * sizeof(char *));

    if (tokens == NULL)
    {
        fprintf(stderr, "Error: Allocation error\n");
        exit(EXIT_FAILURE);
    }
    for (int i = 0; i < bufsize; i++)
    {
        tokens[i] = (char *)malloc(cmdLen * sizeof(char));
        if (tokens[i] == NULL)
        {
            fprintf(stderr, "Error: Allocation error\n");
            exit(EXIT_FAILURE);
        }
    }

    for (i = 0; i < cmdLen;)
    {
        if (command[i] == spaceChar || command[i] == '\t' || command[i] == '\a' || command[i] == '\r' || command[i] == newlineChar)
        {
            if (strlen(tokens[(*tokenCount)]) > 0)
            {
                (*tokenCount)++;
            }
            cmdIndex = 0;
            i++;
        }
        else if (command[i] == '\"')
        {
            if (tokens[(*tokenCount) - 1][0] == 'a' && tokens[(*tokenCount) - 1][1] == 'w' && tokens[(*tokenCount) - 1][2] == 'k')
            {
                fprintf(stderr, "Error: Syntax ERROR\n");
                return NULL;
            }
            if (command[i] == '?' || command[i] == '*')
                wildcard = 1;
            cmdIndex = 0;
            j = i + 1;
            while (j < cmdLen && command[j] != '\"')
            {
                tokens[(*tokenCount)][cmdIndex] = command[j];
                cmdIndex++;
                j++;
            }
            if (strlen(tokens[(*tokenCount)]) > 0)
            {
                (*tokenCount)++;
            }
            cmdIndex = 0;
            i = j + 1;
        }
        else if (command[i] == singleQuoteChar)
        {
            cmdIndex = 0;
            j = i + 1;
            while (j < cmdLen && command[j] != singleQuoteChar)
            {
                tokens[(*tokenCount)][cmdIndex] = command[j];
                cmdIndex++;
                j++;
            }
            if (strlen(tokens[(*tokenCount)]) > 0)
            {
                (*tokenCount)++;
            }
            cmdIndex = 0;
            i = j + 1;
        }
        else
        {
            if (command[i] == '?' || command[i] == '*')
                wildcard = 1;
            tokens[(*tokenCount)][cmdIndex] = command[i];
            cmdIndex++;
            if (i == cmdLen - 1)
            {
                if (strlen(tokens[(*tokenCount)]) > 0)
                {
                    (*tokenCount)++;
                }
            }
            i++;
        }
        if ((*tokenCount) >= bufsize)
        {
            bufsize += MAX_BUF_SIZE;
            tokens = (char **)realloc(tokens, bufsize * sizeof(char *));
            if (!tokens)
            {
                fprintf(stderr, "ERROR: Allocation error\n");
                exit(EXIT_FAILURE);
            }
        }
    }

    return tokens;
}

int changeDirectory(char **tokens)
{
    if (tokens[1] == NULL)
    {
        printf("Error: No directory given\n");
    }
    else
    {
        if (chdir(tokens[1]) != 0)
        {
            printf("Error: Directory not found\n");
        }
    }
    return EXIT_SUCCESS;
}

int exitWithError(char **s)
{
    return 1;
}
