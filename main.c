//
//  main.c
//  fftest
//
//  Created by 商东洲 on 2019/5/10.
//  Copyright © 2019 商东洲. All rights reserved.
//

#include <stdio.h>
#include <libavutil/md5.h>
#include <string.h>

void Hex2Str(const char *sSrc,  char *sDest, int nSrcLen )
{
    int  i;
    char szTmp[3];
    
    for( i = 0; i < nSrcLen; i++ )
    {
        sprintf( szTmp, "%02X", (unsigned char) sSrc[i] );
        memcpy( &sDest[i * 2], szTmp, 2 );
    }
    return ;
}

static void print_md5(uint8_t *md5)
{
    char result[32]={0x0};
    int i;
    for (i = 0; i < 16; i++){
        snprintf(result,"%02x", md5[i]);
       
    }
    printf("\n");
}

int main(int argc, const char * argv[]) {
   /* uint8_t value[]={"http://113.209.195.29:8181/ipns/720.mp4"};
    uint8_t md5val[16];
    uint8_t md5[256]={0x0};
    int i;
    uint8_t in[1000];
   
    av_md5_sum(md5val, value, strlen(value));
    Hex2Str(md5val,md5,sizeof(md5val));
  //  memcmp(md5[1], md5, strlen(md5));
    for (int i=strlen(md5); i>0; i--) {
        md5[i]=md5[i-1];
    }
    md5[0]='/';
    printf(md5);
    printf("md5 size %d\n",strlen(md5));*/
    
    
   
    return 0;
}
