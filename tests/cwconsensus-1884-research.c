#include <stdint.h>
#include <stdio.h>
#include "cwconsensus.h"
int main(void) {
 uint8_t a[16]={1},b[16]={2}; const uint8_t *loser=0; int r;
 cwconsensus_init(); cwconsensus_verdict(0x1884,0x1136,123,0x80,b,1,0,1000,600,&loser);
 r=cwconsensus_verdict(0x1884,0x1136,123,0x80,a,2,1,1100,600,&loser);
 printf("winner=%d loser_reported=%d\n",r,loser!=0);
 r=cwconsensus_verdict(0x1884,0x1136,123,0x80,b,3,1,62000,600,&loser);
 printf("same rejected candidate after 61s=%d (REFUSE=%d)\n",r,CWC_REFUSE);
 cwconsensus_init(); cwconsensus_set_min_sources(3);
 cwconsensus_verdict(0x1884,0x1136,456,0x80,a,1,1,1000,600,&loser);
 cwconsensus_verdict(0x1884,0x1136,456,0x80,a,2,1,1100,600,&loser);
 r=cwconsensus_verdict(0x1884,0x1136,456,0x80,b,3,1,1200,600,&loser);
 printf("one conflicting vote against 2=%d (REFUSE=%d)\n",r,CWC_REFUSE);
 return 0;
}
