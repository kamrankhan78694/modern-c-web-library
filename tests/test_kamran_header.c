#include "kamran.k"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    const char *signature = weblib_kamran_signature();

    assert(signature != NULL);
    assert(strstr(signature, WEBLIB_AUTHOR_KAMRAN) != NULL);

    printf("kamran.k alias OK\n");
    return 0;
}
