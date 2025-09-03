#include <stdio.h>

int main() {
    int n,t,b=0; 
    if (scanf("%d",&n)!=1) return 0;
    while (n-- && scanf("%d",&t)==1) {
        int x=t, y=b;
        if (x<0) x=-x;
        if (y<0) y=-y;
        if (!b || x<y || (x==y && t>b)) b=t;
    }
    printf("%d\n", b);
    return 0;
}
