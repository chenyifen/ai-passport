#include "phone_remote_model.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    assert(remote_action(0,1)==REMOTE_PREVIOUS);
    assert(remote_action(1,1)==REMOTE_NEXT);
    assert(remote_action(0,3)==REMOTE_VOLUME_UP);
    assert(remote_action(1,3)==REMOTE_VOLUME_DOWN);
    assert(remote_action(0,0)==REMOTE_NONE);
    assert(remote_action(0,4)==REMOTE_NONE);
    assert(remote_action(2,3)==REMOTE_NONE);
    remote_mouse_t r[67];
    assert(remote_gesture(REMOTE_NEXT,r,66)==0);
    assert(remote_gesture(REMOTE_VOLUME_UP,r,67)==0);
    for(int a=REMOTE_PREVIOUS;a<=REMOTE_NEXT;a++) {
        assert(remote_gesture(a,r,67)==67);
        for(int i=0;i<41;i++)assert(r[i].buttons==0);
        assert(r[41].buttons==1&&r[41].x==0&&r[41].y==0);
        for(int i=42;i<66;i++)assert(r[i].buttons==1&&r[i].y==(a==REMOTE_NEXT?-9:9)&&r[i].wheel==0);
        assert(r[66].buttons==0);
    }
    assert(remote_gesture(REMOTE_HOME,r,67)==41);
    puts("Phone remote actions and complete drag/release ordering: PASS");
}
