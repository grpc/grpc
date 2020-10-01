#include<stdio.h>
#include<string.h>
#include<stdlib.h>
#include<math.h>
int main()
{
    int a[26],i,j,d;
    char *b[]={"a","b","c","d","e","f","g","h","i","j","k","l","m","n","o","p","q","r","s","t","u","v","w","x","y","z"};
    char c[10];
    for(i=0;i<26;i++)
    {
        scanf("%d",&a[i]);
    }
    scanf("%s",c);
    //printf("%s",c);
    //d=strlen(c);
    //printf("%d",d);
    int e[strlen(c)];
    for(i=0;i<strlen(c);i++)
    {
        for(j=0;j<26;j++)
        {
            if(c[i]==*b[j])
            {
                e[i]=a[j];
                break;
            }
        }
    }
    for(i=0;i<(strlen(c)-1);i++)
    {
        if(e[i]>e[i+1])
        {
            e[i+1]=e[i];
        }
    }
    d=strlen(c)*e[strlen(c)-1]*1;
    printf("%d",d);
    
}