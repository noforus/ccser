#include "../ws.h"

int main()
{
    char *str;
    str = getenv("QUERY_STRING");
    time_t cur;
    cur = time(NULL);
    printf("\r\n");
    printf("<h1>%s</h1>",str);
    printf("<h3>time is :%s</h3>", ctime(&cur));
}
