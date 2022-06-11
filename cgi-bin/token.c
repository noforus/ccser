
#include "../ws.h"
#define MAXCHARS 50
#include<stdbool.h>
#include "../urlcode.h"
int main()
{
    int i;
    char *str,color[10],*str2,*color2;
    bool test=true;
    str = getenv("QUERY_STRING");
    str2= getenv("Content_Length");
    i=atoi(str2);
    for(int loop=6;loop<i;loop++){
       color[loop-6]=str[loop];
    
    }
    color2=url_decode(color);
    printf("\r\n");
    printf("hi");
    printf("\r\n");
    printf("<div style=\"background-color:%s;height:100px;width:100px;\"></div>",color);
    printf("this is %s",color2);
    return 0;
}

