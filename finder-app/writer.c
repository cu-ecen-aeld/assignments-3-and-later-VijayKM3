// C program to create a file and write a string from the command line
#include <stdio.h>
#include <syslog.h>

int main (int argc, char *argv[])
{
 char *writefile;
 char *writestr;
 FILE *fp;
 writefile = argv[1];
 writestr = argv[2];
 
 fp = fopen(writefile, "w"); // "w" creates or truncates for writing
 openlog("my_utility", LOG_PID | LOG_CONS, LOG_USER);
 
 if (argc < 3) {
   syslog(LOG_ERR, "Too few arguments");
//   printf ("Too few arguments\n");
   return 1;
 }
 
 if (fp == NULL) {
   syslog(LOG_ERR, "Error creating file");
 //  printf ("Error creating file\n");
   return 1;
 }
 
 fprintf(fp,"%s \n", writestr);
 syslog(LOG_INFO, "Writing %s to %s", writestr, writefile);

 closelog();
 fclose(fp);
 return 0;

}

