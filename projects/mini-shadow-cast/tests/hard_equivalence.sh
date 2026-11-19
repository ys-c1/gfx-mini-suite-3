#!/bin/sh
# Prove the hard shadow path is byte-identical before/after the soft changes:
# build HEAD's original shadow.c against the current hard-path test harness,
# dump both functions over a fixed input grid as raw floats, and cmp them.
set -e
cd "$(dirname "$0")/.."
ROOT=$(pwd)
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

git show HEAD:projects/mini-shadow-cast/src/shadow.c > "$TMP/shadow_old.c"
cp src/shadow.h "$TMP/"

cat > "$TMP/dump.c" <<'EOF'
#include "shadow.h"
#include <stdio.h>
#include <stdlib.h>
int main(void){
    FILE* f=fopen(getenv("DUMP_OUT"),"wb");
    float grid[6]={-5.f,0.f,5.f,10.f,20.f,40.f};
    for(int ia=0;ia<6;++ia)for(int ib=0;ib<6;++ib)
      for(int il=0;il<6;++il)for(int ir=0;ir<4;++ir){
        float x1=grid[ia], y1=10.f, x2=grid[ib], y2=12.f;
        float lx=grid[il], ly=0.f, reach=5.f+ir*7.f;
        float o1x,o1y,o2x,o2y;
        int rc=shadow_project(x1,y1,x2,y2,lx,ly,reach,&o1x,&o1y,&o2x,&o2y);
        fwrite(&rc,sizeof(rc),1,f);
        if(rc==0){ fwrite(&o1x,4,1,f); fwrite(&o1y,4,1,f);
                   fwrite(&o2x,4,1,f); fwrite(&o2y,4,1,f); }
      }
    float segs[8]={0,10,10,10, 20,5,30,15};
    for(int n=1;n<=2;++n)
      for(int ip=0;ip<6;++ip)for(int jp=0;jp<6;++jp){
        int r=shadow_point_lit(grid[ip],grid[jp],5,0,segs,n);
        fwrite(&r,sizeof(r),1,f);
      }
    fclose(f);
    return 0;
}
EOF

gcc -O2 -Wall -Wextra -std=c11 "$TMP/shadow_old.c" "$TMP/dump.c" -I"$TMP" -lm -o "$TMP/old"
gcc -O2 -Wall -Wextra -std=c11 src/shadow.c   "$TMP/dump.c" -I"$TMP" -lm -o "$TMP/new"
DUMP_OUT="$TMP/old.bin" "$TMP/old"
DUMP_OUT="$TMP/new.bin" "$TMP/new"
cmp "$TMP/old.bin" "$TMP/new.bin" && echo "HARD PATH BYTE-IDENTICAL ($(wc -c < "$TMP/old.bin") bytes)"
