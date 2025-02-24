/**Assignment 2 submission
Author Kiran Phalak */
#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>

int main(int argc, char *argv[])
{
    openlog(NULL,0,LOG_USER);
    syslog(LOG_INFO,"%d args",argc);
    
    if (argc != 3)
    {
        syslog(LOG_ERR,"%d args",argc);
        exit(1);
    }

    const char *writefile = argv[1];
    const char *writestr = argv[2];

    FILE *file = NULL;
    file = fopen(writefile, "w"); // open in write mode to overwrite
    if (file == NULL)
    {
        syslog(LOG_ERR,"failed to open file");
        exit(1);
    }

    syslog(LOG_DEBUG,"Writing %s to %s",writestr, writefile);
    if (fprintf(file, "%s", writestr) < 0) 
    { 
        syslog(LOG_ERR,"failed to write in file");
    }

    if (fclose(file) != 0) 
    {
        exit(1);
    }
    return 0;
}