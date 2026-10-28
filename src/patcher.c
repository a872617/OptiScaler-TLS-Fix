/* patcher.c — OptiScaler 通用 TLS 空指针防护补丁
 * 作者：鱼加面汉化
 *
 * 改法：把所有 "mov rax, gs:[58h]"（读 TLS 指针）替换成
 *          call helper + 4 个 NOP
 *       helper：
 *          rax = gs:[58h]
 *          非空 -> 原样返回
 *          为空 -> 提供"假 TLS 数组"（每项指向数组自身，后面读出来都是 0）
 *       与后续指令形态无关，任何版本都适用。
 *
 * 构建：tcc -o patcher.exe patcher.c
 * 用法：patcher.exe <输入.dll> <输出.dll>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static uint8_t *b; static long blen;

static uint16_t u16(long o){ return *(uint16_t*)(b+o); }
static uint32_t u32(long o){ return *(uint32_t*)(b+o); }
static void     w16(long o,uint16_t v){ *(uint16_t*)(b+o)=v; }
static void     w32(long o,uint32_t v){ *(uint32_t*)(b+o)=v; }
static uint32_t align_up(uint32_t v,uint32_t a){ return (v+a-1)&~(a-1); }

int main(int argc,char**argv)
{
    FILE*f; long i;
    if(argc<3){ printf("usage: patcher.exe <in.dll> <out.dll>\n"); return 1; }
    f=fopen(argv[1],"rb"); if(!f){ printf("cannot open input\n"); return 2; }
    fseek(f,0,SEEK_END); blen=ftell(f); fseek(f,0,SEEK_SET);
    b=(uint8_t*)malloc(blen+0x2000); if(!b){ printf("out of memory\n"); return 2; }
    if(fread(b,1,blen,f)!=(size_t)blen){ printf("read error\n"); return 2; }
    fclose(f);
    if(b[0]!='M'||b[1]!='Z'){ printf("not a PE file\n"); return 3; }

    {
        uint32_t e=u32(0x3C);
        uint16_t nsec=u16(e+6), optsz=u16(e+20);
        uint32_t opt=e+24, secTable=opt+optsz;
        uint32_t sizeOfHeaders=u32(opt+60);
        uint32_t i2, textRPtr=0,textVA=0,textRSize=0,maxEnd=0;
        long sites=0;

        if(b[e]!='P'||b[e+1]!='E'){ printf("invalid PE\n"); return 3; }
        if(secTable+40*(nsec+1) > sizeOfHeaders){ printf("no room for a new section header\n"); return 4; }

        for(i2=0;i2<nsec;i2++){
            uint32_t o=secTable+40*i2;
            uint32_t vs=u32(o+8), va=u32(o+12), rs=u32(o+16), rp=u32(o+20);
            if(va+vs>maxEnd) maxEnd=va+vs;
            if(!memcmp(b+o,".text\0\0\0",8)){ textVA=va; textRSize=rs; textRPtr=rp; }
        }
        if(!textRPtr){ printf("no .text section\n"); return 5; }

        for(i=0;i<(long)textRSize-9;i++){
            uint8_t*p=b+textRPtr+i;
            if(p[0]==0x65&&p[1]==0x48&&p[2]==0x8B&&p[3]==0x04&&p[4]==0x25&&
               p[5]==0x58&&p[6]==0x00&&p[7]==0x00&&p[8]==0x00) sites++;
        }
        if(!sites){ printf("no mov rax,gs:[58h] found (already patched?)\n"); return 6; }

        {
            uint32_t newVA=align_up(maxEnd,0x1000);
            uint32_t newRaw=align_up((uint32_t)blen,0x200);
            uint32_t newSec=secTable+40*nsec;
            uint32_t H=newVA, flag=H+0x200, arr=H+0x208;
            uint8_t*h=b+newRaw;
            long hl=0; uint32_t c; long patched=0;

            memset(b+blen,0,newRaw+0x1000-(size_t)blen);
            blen=newRaw+0x1000;

            h[hl++]=0x41; h[hl++]=0x5B;                            /* pop r11  (返回地址) */
            h[hl++]=0x49; h[hl++]=0x83; h[hl++]=0xC3; h[hl++]=0x04;/* add r11,4 -> site+9 */
            h[hl++]=0x41; h[hl++]=0x53;                            /* push r11 (存回栈上) */
            h[hl++]=0x51;                                          /* push rcx */
            h[hl++]=0x52;                                          /* push rdx */
            { uint8_t t[9]={0x65,0x48,0x8B,0x04,0x25,0x58,0x00,0x00,0x00}; memcpy(h+hl,t,9); hl+=9; }
            h[hl++]=0x48; h[hl++]=0x85; h[hl++]=0xC0;              /* test rax,rax */
            h[hl++]=0x75; h[hl++]=0x33;                            /* jnz done (->75) */
            h[hl++]=0x80; h[hl++]=0x3D;                            /* cmp byte [rip+flag],0 */
            c=(uint32_t)(flag-(H+31)); memcpy(h+hl,&c,4); hl+=4; h[hl++]=0x00;
            h[hl++]=0x75; h[hl++]=0x23;                            /* jne use (->68) */
            h[hl++]=0x4C; h[hl++]=0x8D; h[hl++]=0x1D;              /* lea r11,[rip+arr] */
            c=(uint32_t)(arr-(H+40)); memcpy(h+hl,&c,4); hl+=4;
            h[hl++]=0x48; h[hl++]=0x8D; h[hl++]=0x15;              /* lea rdx,[rip+arr] */
            c=(uint32_t)(arr-(H+47)); memcpy(h+hl,&c,4); hl+=4;
            h[hl++]=0xB9; h[hl++]=0x40; h[hl++]=0x00; h[hl++]=0x00; h[hl++]=0x00; /* mov ecx,64 */
            h[hl++]=0x4C; h[hl++]=0x89; h[hl++]=0x1A;              /* mov [rdx],r11 */
            h[hl++]=0x48; h[hl++]=0x83; h[hl++]=0xC2; h[hl++]=0x08;/* add rdx,8 */
            h[hl++]=0xE2; h[hl++]=0xF7;                            /* loop -9 */
            h[hl++]=0xC6; h[hl++]=0x05;                            /* mov byte [rip+flag],1 */
            c=(uint32_t)(flag-(H+68)); memcpy(h+hl,&c,4); hl+=4; h[hl++]=0x01;
            h[hl++]=0x48; h[hl++]=0x8D; h[hl++]=0x05;              /* lea rax,[rip+arr]  (use:) */
            c=(uint32_t)(arr-(H+75)); memcpy(h+hl,&c,4); hl+=4;
            h[hl++]=0x5A;                                          /* pop rdx  (done:) */
            h[hl++]=0x59;                                          /* pop rcx */
            h[hl++]=0xC3;                                          /* ret */

            memset(b+newSec,0,40);
            memcpy(b+newSec,".osc",5);
            w32(newSec+8 ,0x1000);
            w32(newSec+12,newVA);
            w32(newSec+16,0x1000);
            w32(newSec+20,newRaw);
            w32(newSec+36,0xE0000060);
            w16(e+6,(uint16_t)(nsec+1));
            w32(opt+56,align_up(newVA+0x1000,0x1000));

            for(i=0;i<(long)textRSize-9;i++){
                uint8_t*p=b+textRPtr+i;
                if(p[0]==0x65&&p[1]==0x48&&p[2]==0x8B&&p[3]==0x04&&p[4]==0x25&&
                   p[5]==0x58&&p[6]==0x00&&p[7]==0x00&&p[8]==0x00){
                    uint32_t siteRVA=textVA+(uint32_t)i;
                    int32_t rel=(int32_t)(H-(siteRVA+5));
                    p[0]=0xE8; memcpy(p+1,&rel,4);
                    p[5]=0x90; p[6]=0x90; p[7]=0x90; p[8]=0x90;
                    patched++;
                }
            }
            printf("helper size: %ld bytes\n",hl);
            printf("sites patched: %ld\n",patched);
        }

        f=fopen(argv[2],"wb");
        if(!f){ printf("cannot write output\n"); return 7; }
        fwrite(b,1,blen,f); fclose(f);
        printf("output: %s (%.1f MB)\n",argv[2],blen/1048576.0);
    }
    return 0;
}
