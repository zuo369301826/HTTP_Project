#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <strings.h>
#include <stdlib.h>

#define MAX 1024


void Print(char *buf)
{
  char str[MAX];
  sscanf(buf, "query=%s", str);

  printf("<!DOCTYPE HTML>\n");
  
  printf("<html>\n");
 
  printf("<head>\n");
  printf( "<meta charset=\"utf-8\" />\n");
  printf( "<link rel=\"shortcut icon\" href=\" ../image/favicon.ico\" /> \n");
  printf("</head>\n");

  printf("<body>\n");
  printf("<h1>");
  printf("[%s]",str);
  printf("</h1>\n");
  printf(" <a href=\"http://192.168.183.131:9090/index.html\"><h3>返回</h3></a>");
  printf("</body>\n");
  
  printf("</html>\n");

}

int main()
{
  char buff[MAX]={0};
  if(getenv("METHOD"))
  {
    if(strcasecmp(getenv("METHOD"),"GET") == 0)
    {
      strcpy(buff, getenv("QUERY_STRING"));
    }
    else
    {
     int content_length = atoi(getenv("CONTENT_LENGTH"));
     int i = 0 ;
     char c;
     
     for(; i < content_length; ++i)
     {
        read(0, &c, 1);
        buff[i] = c;
     }
     buff[i] = '\0';
    }
  }

  Print(buff);
  return 0;

}
